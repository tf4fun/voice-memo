// main/memo_spool.c —— SPIFFS 上的录音暂存目录。
#include "memo_spool.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdatomic.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_partition.h"

#include "memo_container.h"

static const char *TAG = "memo_spool";
static bool s_mounted;
static atomic_bool s_recovery_blocked;

bool memo_spool_recovery_blocked(void) { return s_recovery_blocked; }

static const char *extension_of(memo_spool_state_t state)
{
    switch (state) {
    case MEMO_SPOOL_SENT: return "sent";
    case MEMO_SPOOL_BAD: return "bad";
    case MEMO_SPOOL_LEGACY_KEPT: return "keep";
    case MEMO_SPOOL_RECORDING: return "rec";
    case MEMO_SPOOL_DRAFT: return "draft";
    default: return "memo";
    }
}

static void path_for(char *dst, size_t cap, const char *id, memo_spool_state_t state)
{
    snprintf(dst, cap, "%s/%s.%s", MEMO_SPOOL_MOUNT_POINT, id, extension_of(state));
}

/* Only an entirely erased partition may be initialised automatically. A failed
 * mount with any existing bytes is a storage fault, never permission to erase. */
static bool partition_erased(void)
{
    const esp_partition_t *partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "memo");
    if (partition == NULL || partition->size == 0) return false;
    uint8_t bytes[256];
    for (size_t offset = 0; offset < partition->size; offset += sizeof(bytes)) {
        size_t len = partition->size - offset;
        if (len > sizeof(bytes)) len = sizeof(bytes);
        if (esp_partition_read(partition, offset, bytes, len) != ESP_OK) return false;
        for (size_t i = 0; i < len; i++) if (bytes[i] != 0xff) return false;
    }
    return true;
}

esp_err_t memo_spool_mount(void)
{
    if (s_mounted) return ESP_OK;

    esp_vfs_spiffs_conf_t conf = {
        .base_path = MEMO_SPOOL_MOUNT_POINT,
        .partition_label = "memo",
        .max_files = 4,
        .format_if_mount_failed = false,
    };
    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err == ESP_FAIL && partition_erased()) {
        ESP_LOGI(TAG, "initialising verified erased spool partition");
        err = esp_spiffs_format("memo");
        if (err == ESP_OK) err = esp_vfs_spiffs_register(&conf);
    }
    if (err != ESP_OK) {
        s_recovery_blocked = true;
        ESP_LOGE(TAG, "spiffs mount failed; existing data preserved: %s", esp_err_to_name(err));
        return err;
    }
    s_mounted = true;
    s_recovery_blocked = false;

    size_t total = 0, used = 0;
    if (esp_spiffs_info(conf.partition_label, &total, &used) == ESP_OK) {
        ESP_LOGI(TAG, "spool mounted: %u/%u bytes used", (unsigned)used, (unsigned)total);
    }
    return ESP_OK;
}

esp_err_t memo_spool_create(const char *id, const uint8_t header[80], void **file_out)
{
    if (id == NULL || file_out == NULL) return ESP_ERR_INVALID_ARG;
    if (!s_mounted || s_recovery_blocked) return ESP_ERR_INVALID_STATE;

    char path[64];
    path_for(path, sizeof(path), id, MEMO_SPOOL_RECORDING);

    /* SPIFFS 的对象名上限由 CONFIG_SPIFFS_OBJ_NAME_LEN 决定，超了只会得到 ENAMETOOLONG。
       先自己判一次，日志里就能直接看出是名字太长还是别的原因。 */
    const char *base = strrchr(path, '/') + 1;
    if (strlen(base) >= CONFIG_SPIFFS_OBJ_NAME_LEN) {
        ESP_LOGE(TAG, "file name too long for SPIFFS (%u >= %d): %s",
                 (unsigned)strlen(base), CONFIG_SPIFFS_OBJ_NAME_LEN, base);
        return ESP_ERR_INVALID_SIZE;
    }

    /* 必须是读写模式：收尾时要先读回头部再回填帧数与时长，只写模式会让
       fread 失败，表现为"录音存不下来"。 */
    int fd = open(path, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd < 0) return ESP_FAIL;
    /* ESP-IDF 的 SPIFFS VFS 没有 fcntl，fdopen 内部的 F_GETFL 会以 ENOSYS 失败；
       独占创建之后关掉描述符，再用 fopen 以读写模式打开同一个文件。 */
    close(fd);
    FILE *file = fopen(path, "r+b");
    if (file == NULL) {
        ESP_LOGE(TAG, "cannot open %s: errno=%d", path, errno);
        remove(path);
        return ESP_FAIL;
    }
    if (fwrite(header, 1, 80, file) != 80 || fflush(file) != 0 || fsync(fileno(file)) != 0) {
        fclose(file);
        remove(path);
        return ESP_FAIL;
    }
    *file_out = file;
    return ESP_OK;
}

esp_err_t memo_spool_append(void *file, const void *data, size_t len)
{
    if (file == NULL || data == NULL || len == 0) return ESP_ERR_INVALID_ARG;
    FILE *f = (FILE *)file;
    if (fwrite(data, 1, len, f) != len || fflush(f) != 0 || fsync(fileno(f)) != 0) {
        ESP_LOGE(TAG, "spool write failed (%u bytes)", (unsigned)len);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t memo_spool_finalize(void *file, const char *id, uint32_t frame_count, uint32_t duration_ms,
                              uint32_t *size_out)
{
    if (file == NULL || id == NULL) return ESP_ERR_INVALID_ARG;
    FILE *f = (FILE *)file;

    uint8_t header[80];
    if (fseek(f, 0, SEEK_SET) != 0 || fread(header, 1, sizeof(header), f) != sizeof(header)) {
        fclose(f);
        return ESP_FAIL;
    }
    memo_container_patch(header, frame_count, duration_ms);

    if (fseek(f, 0, SEEK_SET) != 0 || fwrite(header, 1, sizeof(header), f) != sizeof(header)) {
        fclose(f);
        return ESP_FAIL;
    }
    /* ftell 返回的是当前位置，不是文件长度：刚在偏移 0 写完头部，它就等于 80。
       必须 seek 到末尾再取长度。 */
    uint32_t size = 0;
    if (fseek(f, 0, SEEK_END) == 0) {
        long end = ftell(f);
        if (end > 0) size = (uint32_t)end;
    }
    if (size_out != NULL) *size_out = size;
    bool synced = fflush(f) == 0 && fsync(fileno(f)) == 0;
    if (fclose(f) != 0 || !synced) return ESP_FAIL;
    ESP_LOGI(TAG, "saved %u frames, %u ms, %u bytes",
             (unsigned)frame_count, (unsigned)duration_ms, (unsigned)size);
    return memo_spool_move(id, MEMO_SPOOL_RECORDING, MEMO_SPOOL_DRAFT);
}

esp_err_t memo_spool_discard(void *file, const char *id)
{
    esp_err_t err = ESP_OK;
    if (file != NULL && fclose((FILE *)file) != 0) err = ESP_FAIL;
    if (id != NULL) {
        char path[64];
        path_for(path, sizeof(path), id, MEMO_SPOOL_RECORDING);
        if (remove(path) != 0 && errno != ENOENT) err = ESP_FAIL;
    }
    return err;
}

static bool parse_name(const char *name, memo_spool_entry_t *entry)
{
    const char *dot = strrchr(name, '.');
    if (dot == NULL) return false;

    size_t id_len = (size_t)(dot - name);
    /* 只认 36 字符的带连字符 UUID：/spool 下的临时文件（play.memo 等）不是录音。 */
    if (id_len != 36 || id_len >= sizeof(entry->id)) return false;
    memcpy(entry->id, name, id_len);
    entry->id[id_len] = '\0';

    if (strcmp(dot + 1, "memo") == 0) {
        entry->state = MEMO_SPOOL_PENDING;
    } else if (strcmp(dot + 1, "sent") == 0) {
        entry->state = MEMO_SPOOL_SENT;
    } else if (strcmp(dot + 1, "bad") == 0) {
        entry->state = MEMO_SPOOL_BAD;
    } else if (strcmp(dot + 1, "keep") == 0) {
        entry->state = MEMO_SPOOL_LEGACY_KEPT;
    } else if (strcmp(dot + 1, "rec") == 0) {
        entry->state = MEMO_SPOOL_RECORDING;
    } else if (strcmp(dot + 1, "draft") == 0) {
        entry->state = MEMO_SPOOL_DRAFT;
    } else {
        return false;
    }
    return true;
}

static unsigned scan_priority(memo_spool_state_t state)
{
    // Bounded readers must always see an interrupted/unconfirmed capture or an
    // active upload before compatibility archives, regardless of directory order.
    if (state == MEMO_SPOOL_RECORDING || state == MEMO_SPOOL_DRAFT) return 0;
    if (state == MEMO_SPOOL_PENDING) return 1;
    if (state == MEMO_SPOOL_SENT) return 3;
    return 2;
}

int memo_spool_scan(memo_spool_entry_t *out, size_t max_entries)
{
    if (!s_mounted) return 0;

    DIR *dir = opendir(MEMO_SPOOL_MOUNT_POINT);
    if (dir == NULL) { s_recovery_blocked = true; return 0; }

    int count = 0;
    struct dirent *item;
    for (;;) {
        errno = 0;
        item = readdir(dir);
        if (item == NULL) {
            if (errno != 0) s_recovery_blocked = true;
            break;
        }
        memo_spool_entry_t entry = {0};
        if (!parse_name(item->d_name, &entry)) continue;

        char path[64];
        path_for(path, sizeof(path), entry.id, entry.state);
        struct stat info;
        entry.size = (stat(path, &info) == 0) ? (uint32_t)info.st_size : 0;

        /* 头部里有录音时刻与时长，列表直接用它，不必再解析一次。 */
        if (entry.state != MEMO_SPOOL_SENT && entry.size >= MEMO_CONTAINER_HEADER_SIZE) {
            FILE *file = fopen(path, "rb");
            if (file != NULL) {
                uint8_t raw[MEMO_CONTAINER_HEADER_SIZE];
                if (fread(raw, 1, sizeof(raw), file) == sizeof(raw)) {
                    memo_container_header_t header;
                    if (memo_container_decode(raw, &header)) {
                        /* 2020 年之前的时间只能是"没对时"，显示成未知，
                           别把 1970 年当成真实录音时刻。 */
                        entry.recorded_at = (header.recorded_at > 1577836800ULL)
                                                ? header.recorded_at
                                                : 0;
                        entry.duration_ms = header.duration_ms;
                        entry.frame_count = header.frame_count;
                    }
                }
                fclose(file);
            }
        }

        if (out != NULL && max_entries > 0) {
            const size_t used = (size_t)count < max_entries ? (size_t)count : max_entries;
            size_t at = 0;
            while (at < used && scan_priority(out[at].state) <= scan_priority(entry.state)) at++;
            if (at < max_entries) {
                const size_t end = used < max_entries ? used : max_entries-1;
                memmove(out+at+1, out+at, (end-at)*sizeof(*out));
                out[at] = entry;
            }
        }
        count++;
    }
    if (closedir(dir) != 0) s_recovery_blocked = true;
    return count;
}

int memo_spool_recover(void)
{
    if (!s_mounted) return 0;
    s_recovery_blocked = false;
    memo_spool_entry_t entries[MEMO_SPOOL_MAX_ENTRIES];
    int count = memo_spool_scan(entries, MEMO_SPOOL_MAX_ENTRIES), recovered = 0;
    for (int i = 0; i < count && i < MEMO_SPOOL_MAX_ENTRIES; i++) {
        memo_spool_entry_t *e = &entries[i];
        if (e->state != MEMO_SPOOL_RECORDING && e->state != MEMO_SPOOL_DRAFT &&
            e->state != MEMO_SPOOL_PENDING) continue;
        char path[64]; path_for(path, sizeof(path), e->id, e->state);
        FILE *file = fopen(path, "r+b");
        if (file == NULL) { s_recovery_blocked = true; continue; }
        uint8_t raw[80]; memo_container_header_t header;
        bool valid = fread(raw, 1, sizeof(raw), file) == sizeof(raw) &&
                     memo_container_decode(raw, &header) && header.frame_ms > 0 &&
                     header.frame_ms <= 60 && header.sample_rate == 16000 && header.channels == 1;
        uint32_t frames = 0, end = 80;
        while (valid && end + 2 <= e->size) {
            uint8_t length[2];
            if (fread(length, 1, 2, file) != 2) break;
            uint32_t len = length[0] | ((uint32_t)length[1] << 8);
            if (len == 0 || len > e->size - end - 2) break;
            if (fseek(file, len, SEEK_CUR) != 0) break;
            end += 2 + len; frames++;
        }
        if (!valid || frames == 0) {
            fclose(file);
            if (memo_spool_move(e->id, e->state, MEMO_SPOOL_BAD) == ESP_OK) recovered++;
            else s_recovery_blocked = true;
            continue;
        }
        bool damaged = end != e->size || header.frame_count != frames ||
                       header.duration_ms != frames * header.frame_ms;
        bool draft = e->state == MEMO_SPOOL_RECORDING || damaged;
        if (draft) {
            /* Revoke upload eligibility before touching a damaged confirmed
             * file. If any recovery I/O fails, the uploader stays held. */
            if (e->state == MEMO_SPOOL_PENDING) {
                fclose(file);
                if (memo_spool_move(e->id, e->state, MEMO_SPOOL_DRAFT) != ESP_OK) {
                    s_recovery_blocked = true;
                    continue;
                }
                e->state = MEMO_SPOOL_DRAFT;
                path_for(path, sizeof(path), e->id, e->state);
                file = fopen(path, "r+b");
                if (file == NULL) { s_recovery_blocked = true; continue; }
            }
            memo_container_patch(raw, frames, frames * header.frame_ms);
            bool ok = fseek(file, 0, SEEK_SET) == 0 && fwrite(raw, 1, 80, file) == 80 &&
                      fflush(file) == 0 && ftruncate(fileno(file), end) == 0 &&
                      fsync(fileno(file)) == 0;
            if (fclose(file) != 0) ok = false;
            if (ok && (e->state == MEMO_SPOOL_DRAFT ||
                       memo_spool_move(e->id, e->state, MEMO_SPOOL_DRAFT) == ESP_OK)) recovered++;
            else s_recovery_blocked = true;
        } else {
            fclose(file);
        }
    }
    return recovered;
}

int memo_spool_drop_empty(void) { return memo_spool_recover(); }

esp_err_t memo_spool_set_state(const char *id, memo_spool_state_t state)
{
    return memo_spool_move(id, MEMO_SPOOL_PENDING, state);
}

esp_err_t memo_spool_move(const char *id, memo_spool_state_t source, memo_spool_state_t state)
{
    if (id == NULL) return ESP_ERR_INVALID_ARG;

    char from[64], to[64];
    path_for(from, sizeof(from), id, source);
    path_for(to, sizeof(to), id, state);
    struct stat existing;
    if (stat(to, &existing) == 0 || errno != ENOENT) return ESP_FAIL;
    if (rename(from, to) != 0) {
        ESP_LOGW(TAG, "rename %s -> %s failed", from, to);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t memo_spool_remove(const char *id, memo_spool_state_t state)
{
    if (id == NULL) return ESP_ERR_INVALID_ARG;
    char path[64];
    path_for(path, sizeof(path), id, state);
    return (remove(path) == 0) ? ESP_OK : ESP_FAIL;
}

size_t memo_spool_free_bytes(void)
{
    if (!s_mounted) return 0;

    size_t total = 0, used = 0;
    if (esp_spiffs_info("memo", &total, &used) != ESP_OK) return 0;
    return (total > used) ? (total - used) : 0;
}
