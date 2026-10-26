// main/memo_spool.c —— SPIFFS 上的录音暂存目录。
#include "memo_spool.h"

#include <dirent.h>
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

    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        ESP_LOGE(TAG, "cannot create %s", path);
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
    long size = ftell(f);
    if (size_out != NULL) *size_out = (uint32_t)size;
    if (fclose(f) != 0) return ESP_FAIL;
    ESP_LOGI(TAG, "saved %u frames, %u ms, %ld bytes",
             (unsigned)frame_count, (unsigned)duration_ms, size);
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

        if (out != NULL && (size_t)count < max_entries) {
            out[count] = entry;
        }
        count++;
    }
    closedir(dir);
    return count;
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
