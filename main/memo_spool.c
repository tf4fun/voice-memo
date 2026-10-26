// main/memo_spool.c —— SPIFFS 上的录音暂存目录。
#include "memo_spool.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_log.h"
#include "esp_spiffs.h"

#include "memo_container.h"

static const char *TAG = "memo_spool";
static bool s_mounted;

static const char *extension_of(memo_spool_state_t state)
{
    switch (state) {
    case MEMO_SPOOL_SENT: return "sent";
    case MEMO_SPOOL_BAD: return "bad";
    default: return "memo";
    }
}

static void path_for(char *dst, size_t cap, const char *id, memo_spool_state_t state)
{
    snprintf(dst, cap, "%s/%s.%s", MEMO_SPOOL_MOUNT_POINT, id, extension_of(state));
}

esp_err_t memo_spool_mount(void)
{
    if (s_mounted) return ESP_OK;

    esp_vfs_spiffs_conf_t conf = {
        .base_path = MEMO_SPOOL_MOUNT_POINT,
        .partition_label = "memo",
        .max_files = 4,
        .format_if_mount_failed = true,
    };
    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spiffs mount failed: %s", esp_err_to_name(err));
        return err;
    }
    s_mounted = true;

    size_t total = 0, used = 0;
    if (esp_spiffs_info(conf.partition_label, &total, &used) == ESP_OK) {
        ESP_LOGI(TAG, "spool mounted: %u/%u bytes used", (unsigned)used, (unsigned)total);
    }
    return ESP_OK;
}

esp_err_t memo_spool_create(const char *id, const uint8_t header[80], void **file_out)
{
    if (id == NULL || file_out == NULL) return ESP_ERR_INVALID_ARG;
    if (!s_mounted) return ESP_ERR_INVALID_STATE;

    char path[64];
    path_for(path, sizeof(path), id, MEMO_SPOOL_PENDING);

    /* SPIFFS 的对象名上限由 CONFIG_SPIFFS_OBJ_NAME_LEN 决定，超了只会得到 ENAMETOOLONG。
       先自己判一次，日志里就能直接看出是名字太长还是别的原因。 */
    const char *base = path + sizeof(MEMO_SPOOL_MOUNT_POINT);
    if (strlen(base) >= CONFIG_SPIFFS_OBJ_NAME_LEN) {
        ESP_LOGE(TAG, "file name too long for SPIFFS (%u >= %d): %s",
                 (unsigned)strlen(base), CONFIG_SPIFFS_OBJ_NAME_LEN, base);
        return ESP_ERR_INVALID_SIZE;
    }

    /* 必须是读写模式：收尾时要先读回头部再回填帧数与时长，只写模式会让
       fread 失败，表现为"录音存不下来"。 */
    FILE *file = fopen(path, "w+b");
    if (file == NULL) {
        ESP_LOGE(TAG, "cannot create %s: errno=%d", path, errno);
        return ESP_FAIL;
    }
    if (fwrite(header, 1, 80, file) != 80) {
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
    if (fwrite(data, 1, len, f) != len) {
        ESP_LOGE(TAG, "spool write failed (%u bytes)", (unsigned)len);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t memo_spool_finalize(void *file, uint32_t frame_count, uint32_t duration_ms,
                              uint32_t *size_out)
{
    if (file == NULL) return ESP_ERR_INVALID_ARG;
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
    if (fclose(f) != 0) return ESP_FAIL;
    ESP_LOGI(TAG, "saved %u frames, %u ms, %u bytes",
             (unsigned)frame_count, (unsigned)duration_ms, (unsigned)size);
    return ESP_OK;
}

esp_err_t memo_spool_discard(void *file, const char *id)
{
    if (file != NULL) fclose((FILE *)file);
    if (id != NULL) {
        char path[64];
        path_for(path, sizeof(path), id, MEMO_SPOOL_PENDING);
        remove(path);
    }
    return ESP_OK;
}

static bool parse_name(const char *name, memo_spool_entry_t *entry)
{
    const char *dot = strrchr(name, '.');
    if (dot == NULL) return false;

    size_t id_len = (size_t)(dot - name);
    if (id_len == 0 || id_len >= sizeof(entry->id)) return false;
    memcpy(entry->id, name, id_len);
    entry->id[id_len] = '\0';

    if (strcmp(dot + 1, "memo") == 0) {
        entry->state = MEMO_SPOOL_PENDING;
    } else if (strcmp(dot + 1, "sent") == 0) {
        entry->state = MEMO_SPOOL_SENT;
    } else if (strcmp(dot + 1, "bad") == 0) {
        entry->state = MEMO_SPOOL_BAD;
    } else {
        return false;
    }
    return true;
}

int memo_spool_scan(memo_spool_entry_t *out, size_t max_entries)
{
    if (!s_mounted) return 0;

    DIR *dir = opendir(MEMO_SPOOL_MOUNT_POINT);
    if (dir == NULL) return 0;

    int count = 0;
    struct dirent *item;
    while ((item = readdir(dir)) != NULL) {
        memo_spool_entry_t entry = {0};
        if (!parse_name(item->d_name, &entry)) continue;

        char path[64];
        path_for(path, sizeof(path), entry.id, entry.state);
        struct stat info;
        entry.size = (stat(path, &info) == 0) ? (uint32_t)info.st_size : 0;

        /* 头部里有录音时刻与时长，列表直接用它，不必再解析一次。 */
        if (entry.state == MEMO_SPOOL_PENDING && entry.size >= MEMO_CONTAINER_HEADER_SIZE) {
            FILE *file = fopen(path, "rb");
            if (file != NULL) {
                uint8_t raw[MEMO_CONTAINER_HEADER_SIZE];
                if (fread(raw, 1, sizeof(raw), file) == sizeof(raw)) {
                    memo_container_header_t header;
                    if (memo_container_decode(raw, &header)) {
                        entry.recorded_at = header.recorded_at;
                        entry.duration_ms = header.duration_ms;
                    }
                }
                fclose(file);
            }
        }

        if (out != NULL && (size_t)count < max_entries) {
            out[count] = entry;
        }
        count++;
    }
    closedir(dir);
    return count;
}

int memo_spool_drop_empty(void)
{
    if (!s_mounted) return 0;

    memo_spool_entry_t entries[MEMO_SPOOL_MAX_ENTRIES];
    int total = memo_spool_scan(entries, MEMO_SPOOL_MAX_ENTRIES);
    int dropped = 0;

    for (int i = 0; i < total && i < MEMO_SPOOL_MAX_ENTRIES; i++) {
        if (entries[i].state != MEMO_SPOOL_PENDING) continue;
        if (entries[i].size <= MEMO_CONTAINER_HEADER_SIZE) {
            /* 只有头部、没有任何帧：上一次录音在中途被打断。 */
            ESP_LOGW(TAG, "dropping empty stub %s (%u bytes)",
                     entries[i].id, (unsigned)entries[i].size);
            if (memo_spool_remove(entries[i].id, MEMO_SPOOL_PENDING) == ESP_OK) dropped++;
            continue;
        }

        char path[64];
        path_for(path, sizeof(path), entries[i].id, MEMO_SPOOL_PENDING);
        FILE *file = fopen(path, "rb");
        if (file == NULL) continue;
        uint8_t header[MEMO_CONTAINER_HEADER_SIZE];
        bool ok = fread(header, 1, sizeof(header), file) == sizeof(header);
        fclose(file);

        memo_container_header_t parsed;
        if (ok && memo_container_decode(header, &parsed) && parsed.frame_count == 0) {
            ESP_LOGW(TAG, "dropping zero-frame recording %s", entries[i].id);
            if (memo_spool_remove(entries[i].id, MEMO_SPOOL_PENDING) == ESP_OK) dropped++;
        }
    }
    return dropped;
}

esp_err_t memo_spool_set_state(const char *id, memo_spool_state_t state)
{
    if (id == NULL) return ESP_ERR_INVALID_ARG;

    char from[64], to[64];
    path_for(from, sizeof(from), id, MEMO_SPOOL_PENDING);
    path_for(to, sizeof(to), id, state);
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
