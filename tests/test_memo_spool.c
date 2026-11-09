#define _DARWIN_C_SOURCE
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
static char *spool_root;
#define MEMO_SPOOL_MOUNT_POINT spool_root
#define CONFIG_SPIFFS_OBJ_NAME_LEN 48
#include "../main/memo_spool.c"

static int mount_failures, formats;
static bool erased, read_failed;
const esp_partition_t *esp_partition_find_first(int type, int subtype, const char *label)
{ (void)type; (void)subtype; assert(strcmp(label, "memo") == 0);
  static const esp_partition_t partition = {.size = 1031}; return &partition; }
esp_err_t esp_partition_read(const esp_partition_t *p, size_t off, void *out, size_t len)
{ assert(off + len <= p->size); memset(out, 0xff, len);
  if (!erased && off + len == p->size) ((uint8_t *)out)[len - 1] = 0;
  return read_failed ? ESP_FAIL : ESP_OK; }
esp_err_t esp_spiffs_format(const char *label)
{ assert(strcmp(label, "memo") == 0 && erased && !read_failed); formats++; return ESP_OK; }
esp_err_t esp_vfs_spiffs_register(const esp_vfs_spiffs_conf_t *conf)
{
    assert(strcmp(conf->base_path, spool_root) == 0);
    assert(strcmp(conf->partition_label, "memo") == 0);
    assert(!conf->format_if_mount_failed);
    return mount_failures-- > 0 ? ESP_FAIL : ESP_OK;
}
esp_err_t esp_spiffs_info(const char *label, size_t *total, size_t *used)
{ (void)label; *total = 1048576; *used = 0; return ESP_OK; }

int main(void)
{
    char directory[] = "/tmp/ms.XXXXXX";
    spool_root = mkdtemp(directory);
    assert(spool_root != NULL);
    mount_failures = 1;
    assert(memo_spool_mount() == ESP_FAIL && formats == 0 && memo_spool_recovery_blocked());
    erased = true; read_failed = true; mount_failures = 1;
    assert(memo_spool_mount() == ESP_FAIL && formats == 0 && memo_spool_recovery_blocked());
    read_failed = false; mount_failures = 1;
    assert(memo_spool_mount() == ESP_OK && formats == 1 && !memo_spool_recovery_blocked());
    assert(memo_spool_mount() == ESP_OK && formats == 1);
    const char *id = "00000000-0000-4000-8000-000000000001";
    memo_container_header_t header = {
        .sample_rate=16000, .channels=1, .frame_ms=60, .recorded_at=1700000000,
    };
    snprintf(header.memo_id, sizeof(header.memo_id), "%s", id);
    uint8_t raw[80];
    memo_container_encode(&header, raw);
    void *file;
    assert(memo_spool_create(id, raw, &file) == ESP_OK);
    assert(memo_spool_discard(file,id)==ESP_OK && memo_spool_scan(NULL,0)==0);
    assert(memo_spool_discard(NULL,id)==ESP_OK); // Cancel is idempotent after deletion.
    char blocked[64];path_for(blocked,sizeof(blocked),id,MEMO_SPOOL_RECORDING);
    assert(mkdir(blocked,0700)==0);
    char child[80];snprintf(child,sizeof(child),"%s/child",blocked);
    FILE *blocker=fopen(child,"wb");assert(blocker);assert(fclose(blocker)==0);
    assert(memo_spool_discard(NULL,id)==ESP_FAIL); // Never report failed removal as success.
    assert(remove(child)==0 && rmdir(blocked)==0);
    assert(memo_spool_create(id, raw, &file) == ESP_OK);
    const uint8_t packet[] = {1, 0, 42};
    assert(memo_spool_append(file, packet, sizeof(packet)) == ESP_OK);
    uint32_t size;
    assert(memo_spool_finalize(file, id, 1, 60, &size) == ESP_OK && size == 83);
    memo_spool_entry_t draft;
    assert(memo_spool_scan(&draft, 1) == 1 && draft.state == MEMO_SPOOL_DRAFT);
    assert(memo_spool_move(id, MEMO_SPOOL_DRAFT, MEMO_SPOOL_PENDING) == ESP_OK);
    assert(memo_spool_set_state(id, MEMO_SPOOL_BAD) == ESP_OK);
    memo_spool_entry_t e;
    assert(memo_spool_scan(&e, 1) == 1 && e.state == MEMO_SPOOL_BAD);
    assert(e.duration_ms == 60 && e.recorded_at == 1700000000 && e.size == 83);
    assert(memo_spool_move(id, MEMO_SPOOL_BAD, MEMO_SPOOL_PENDING) == ESP_OK);
    char pending_path[80], legacy_path[80];
    snprintf(pending_path, sizeof(pending_path), "%s/%s.memo", spool_root, id);
    snprintf(legacy_path, sizeof(legacy_path), "%s/%s.keep", spool_root, id);
    assert(rename(pending_path, legacy_path) == 0); /* fixture from superseded firmware */
    assert(memo_spool_scan(&e, 1) == 1 && e.state == MEMO_SPOOL_LEGACY_KEPT);
    assert(e.duration_ms == 60 && e.recorded_at == 1700000000 && e.size == 83);
    assert(memo_spool_drop_empty() == 0);
    char path[80];
    snprintf(path, sizeof(path), "%s/%s.keep", spool_root, id);
    FILE *saved = fopen(path, "rb");
    assert(saved != NULL);
    assert(fread(raw, 1, sizeof(raw), saved) == 80);
    memo_container_header_t decoded;
    assert(memo_container_decode(raw, &decoded) && decoded.frame_count == 1);
    uint8_t read_packet[3];
    assert(fread(read_packet, 1, sizeof(read_packet), saved) == sizeof(packet));
    assert(memcmp(packet, read_packet, sizeof(packet)) == 0);
    fclose(saved);
    assert(memo_spool_move(id, MEMO_SPOOL_BAD, MEMO_SPOOL_PENDING) == ESP_FAIL);
    assert(memo_spool_scan(&e, 1) == 1 && e.state == MEMO_SPOOL_LEGACY_KEPT);
    assert(memo_spool_move(id, MEMO_SPOOL_LEGACY_KEPT, MEMO_SPOOL_PENDING) == ESP_OK);
    assert(memo_spool_scan(&e, 1) == 1 && e.state == MEMO_SPOOL_PENDING && e.size == 83);
    assert(memo_spool_remove(id, MEMO_SPOOL_PENDING) == ESP_OK);
    assert(memo_spool_scan(&e, 1) == 0);
    /* Abrupt close before finalisation: placeholder header and complete packets
     * recover as an unconfirmed draft; incomplete tail is never uploaded. */
    memo_container_encode(&header, raw);
    assert(memo_spool_create(id, raw, &file) == ESP_OK);
    assert(memo_spool_append(file, packet, sizeof(packet)) == ESP_OK);
    const uint8_t torn[] = {4, 0, 1};
    assert(memo_spool_append(file, torn, sizeof(torn)) == ESP_OK);
    assert(fclose(file) == 0);
    assert(memo_spool_recover() == 1);
    assert(memo_spool_scan(&e, 1) == 1 && e.state == MEMO_SPOOL_DRAFT);
    assert(e.frame_count == 1 && e.duration_ms == 60 && e.size == 83);
    assert(memo_spool_recover() == 0); /* Idempotent reboot. */
    assert(memo_spool_remove(id, MEMO_SPOOL_DRAFT) == ESP_OK); /* Explicit cancel. */

    /* Empty/invalid interrupted captures are quarantined, never erased on boot. */
    assert(memo_spool_create(id, raw, &file) == ESP_OK);
    assert(fclose(file) == 0);
    assert(memo_spool_recover() == 1);
    assert(memo_spool_scan(&e, 1) == 1 && e.state == MEMO_SPOOL_BAD && e.size == 80);
    assert(memo_spool_recover() == 0);
    assert(memo_spool_remove(id, MEMO_SPOOL_BAD) == ESP_OK);

    /* A damaged legacy pending file loses automatic-upload eligibility. */
    assert(memo_spool_create(id, raw, &file) == ESP_OK);
    assert(memo_spool_append(file, packet, sizeof(packet)) == ESP_OK);
    assert(fclose(file) == 0);
    assert(memo_spool_move(id, MEMO_SPOOL_RECORDING, MEMO_SPOOL_PENDING) == ESP_OK);
    assert(memo_spool_recover() == 1);
    assert(memo_spool_scan(&e, 1) == 1 && e.state == MEMO_SPOOL_DRAFT && e.frame_count == 1);
    assert(memo_spool_remove(id, MEMO_SPOOL_DRAFT) == ESP_OK);
    /* A failed state rename cannot leave damaged .memo eligible for upload. */
    assert(memo_spool_create(id, raw, &file) == ESP_OK);
    assert(memo_spool_append(file, packet, sizeof(packet)) == ESP_OK);
    assert(fclose(file) == 0);
    assert(memo_spool_move(id, MEMO_SPOOL_RECORDING, MEMO_SPOOL_PENDING) == ESP_OK);
    char collision[80];
    snprintf(collision, sizeof(collision), "%s/%s.draft", spool_root, id);
    FILE *occupied = fopen(collision, "wb");
    uint8_t saved_header[80];
    memcpy(saved_header, raw, sizeof(saved_header));
    memo_container_patch(saved_header, 1, 60);
    assert(occupied != NULL && fwrite(saved_header, 1, 80, occupied) == 80);
    assert(fwrite(packet, 1, sizeof(packet), occupied) == sizeof(packet) && fclose(occupied) == 0);
    assert(memo_spool_recover() >= 0 && memo_spool_recovery_blocked());
    assert(access(pending_path, F_OK) == 0);
    occupied = fopen(collision, "rb");
    uint8_t unchanged[83];
    assert(occupied != NULL && fread(unchanged, 1, 83, occupied) == 83);
    assert(memcmp(unchanged, saved_header, 80) == 0 && memcmp(unchanged + 80, packet, 3) == 0);
    assert(fclose(occupied) == 0);
    assert(remove(collision) == 0); /* Explicit fixture cleanup. */
    assert(memo_spool_recover() == 1 && !memo_spool_recovery_blocked());
    assert(memo_spool_remove(id, MEMO_SPOOL_DRAFT) == ESP_OK);
    // Legacy acknowledged files must not hide the single unresolved recording
    // beyond a bounded display/worker snapshot.
    char sent[80];
    for(unsigned i=0;i<20;i++){
        snprintf(sent,sizeof(sent),"%s/00000000-0000-4000-8000-%012u.sent",spool_root,i+100);
        FILE *old=fopen(sent,"wb");assert(old && fputc(0,old)!=EOF && fclose(old)==0);
    }
    assert(memo_spool_create(id,raw,&file)==ESP_OK);
    assert(memo_spool_append(file,packet,sizeof(packet))==ESP_OK);
    assert(memo_spool_scan(&e,1)==21 && e.state==MEMO_SPOOL_RECORDING);
    assert(memo_spool_recover()==1 && !memo_spool_recovery_blocked());
    assert(memo_spool_scan(&e,1)==21 && e.state==MEMO_SPOOL_DRAFT);
    assert(memo_spool_move(id,MEMO_SPOOL_DRAFT,MEMO_SPOOL_PENDING)==ESP_OK);
    assert(memo_spool_scan(&e,1)==21 && e.state==MEMO_SPOOL_PENDING);
    assert(memo_spool_remove(id,MEMO_SPOOL_PENDING)==ESP_OK);
    for(unsigned i=0;i<20;i++){
        snprintf(sent,sizeof(sent),"%s/00000000-0000-4000-8000-%012u.sent",spool_root,i+100);
        assert(remove(sent)==0); // Only this test's fixtures.
    }
    char absent[80];snprintf(absent,sizeof(absent),"%s/missing",spool_root);
    char *valid_root=spool_root;spool_root=absent;
    assert(memo_spool_scan(&e,1)==0 && memo_spool_recovery_blocked());
    spool_root=valid_root;
    assert(memo_spool_create(id,raw,&file)==ESP_ERR_INVALID_STATE);
    assert(memo_spool_recover()==0 && !memo_spool_recovery_blocked());
    assert(rmdir(spool_root) == 0);
    puts("memo spool retry/legacy/metadata/audio preservation: PASS");
    return 0;
}
