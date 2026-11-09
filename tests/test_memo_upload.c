/* Execute the actual uploader with deterministic HTTP/storage/task boundaries. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
static FILE *upload_file(const char *path, const char *mode);
#define fopen upload_file
#include "../main/memo_upload.c"
#undef fopen

static const char *ID = "00000000-0000-4000-8000-000000000001";
static bool file_exists, io_failure, rename_failure, transport_failure, online;
static bool capture_during_probe;
static bool playback_during_response;
static bool stop_timeout, recovery_blocked, scan_failure;
static bool extra_exists;
static memo_spool_state_t extra_state;
static const char *EXTRA_ID = "00000000-0000-4000-8000-000000000099";
static memo_spool_state_t file_state;
static int code, posts, deletes, moves, delays;
static int probes, invalidations;
static bool request_during_probe, probe_failure;
static uint32_t max_ms, max_bytes;

SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void *)1; }
SemaphoreHandle_t xSemaphoreCreateBinary(void) { return (void *)2; }
int xSemaphoreTake(SemaphoreHandle_t s, unsigned t) { (void)t; return s != NULL && !(s == (void *)2 && stop_timeout); }
int xSemaphoreGive(SemaphoreHandle_t s) { return s != NULL; }
int xTaskCreate(void (*fn)(void *), const char *n, unsigned stack, void *arg,
                unsigned prio, TaskHandle_t *out)
{ (void)fn; (void)n; (void)stack; (void)arg; (void)prio; *out = (void *)3; return pdPASS; }
void vTaskDelay(unsigned t) { (void)t; if (++delays >= 3) s_running = false; }
void vTaskDelete(TaskHandle_t t) { assert(t!=NULL); }
void vTaskSuspend(TaskHandle_t t) { assert(t==NULL); }
esp_err_t esp_read_mac(uint8_t *mac, int t) { (void)t; memset(mac, 0, 6); return ESP_OK; }
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *c)
{ assert(c->method == HTTP_METHOD_POST); return (void *)4; }
esp_err_t esp_http_client_set_header(esp_http_client_handle_t c, const char *n, const char *v)
{ (void)c; (void)n; (void)v; return ESP_OK; }
esp_err_t esp_http_client_open(esp_http_client_handle_t c, int n)
{ (void)c; assert(n == 80); posts++; return transport_failure ? ESP_FAIL : ESP_OK; }
int esp_http_client_write(esp_http_client_handle_t c, const char *d, int n)
{ (void)c; (void)d; return n; }
int esp_http_client_fetch_headers(esp_http_client_handle_t c) { (void)c; if(playback_during_response)memo_upload_set_playback_busy(true); return 0; }
int esp_http_client_get_status_code(esp_http_client_handle_t c) { (void)c; return code; }
esp_err_t esp_http_client_close(esp_http_client_handle_t c) { (void)c; return ESP_OK; }
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t c) { (void)c; return ESP_OK; }
bool memo_net_has_ip(void) { return online; }
memo_discovery_state_t memo_discovery_state(void) { return MEMO_DISCOVERY_FOUND; }
esp_err_t memo_discovery_search(uint32_t t) { (void)t; return ESP_OK; }
bool memo_discovery_endpoint(memo_endpoint_t *out)
{ *out = (memo_endpoint_t){.host="127.0.0.1", .port=8080, .confirmed=true}; return true; }
void memo_discovery_invalidate(const memo_endpoint_t *endpoint) { (void)endpoint; invalidations++; }

esp_err_t memo_api_probe(void)
{
    probes++;
    if (capture_during_probe) memo_upload_set_capture_busy(true);
    if (request_during_probe) {
        request_during_probe = false;
        memo_upload_request_discovery();
    }
    return probe_failure ? ESP_FAIL : ESP_OK;
}
esp_err_t memo_api_refresh(void) { return ESP_OK; }
void memo_api_request_refresh(void) {}
void memo_api_poll_text(void) {}
bool memo_api_take_refresh_request(void) { return false; }
bool memo_api_all_terminal(void) { return true; }
uint32_t memo_api_max_memo_ms(void) { return max_ms; }
uint32_t memo_api_max_bytes(void) { return max_bytes; }
bool memo_spool_recovery_blocked(void) { return recovery_blocked; }
int memo_spool_scan(memo_spool_entry_t *out, size_t cap)
{
    if(scan_failure){recovery_blocked=true;return 0;}
    int count = 0;
    if (file_exists) {
        if (out != NULL && cap > 0) {
            *out = (memo_spool_entry_t){.state=file_state, .size=80, .duration_ms=60,
                                       .recorded_at=1700000000};
            snprintf(out->id, sizeof(out->id), "%s", ID);
        }
        count++;
    }
    if (extra_exists) {
        if (out != NULL && (size_t)count < cap) {
            out[count] = (memo_spool_entry_t){.state=extra_state, .size=80, .duration_ms=60,
                                             .recorded_at=1700000000};
            snprintf(out[count].id, sizeof(out[count].id), "%s", EXTRA_ID);
        }
        count++;
    }
    return count;
}
esp_err_t memo_spool_remove(const char *id, memo_spool_state_t state)
{
    if (extra_exists && strcmp(id, EXTRA_ID) == 0) {
        assert(extra_state == state);
        if (io_failure) return ESP_FAIL;
        deletes++; extra_exists = false;
        return ESP_OK;
    }
    assert(strcmp(id, ID) == 0 && file_exists && file_state == state);
    if (io_failure) return ESP_FAIL;
    deletes++;
    file_exists = false;
    return ESP_OK;
}
esp_err_t memo_spool_move(const char *id, memo_spool_state_t from, memo_spool_state_t to)
{
    if (extra_exists && strcmp(id, EXTRA_ID) == 0) {
        assert(extra_state == from);
        if (io_failure || rename_failure) return ESP_FAIL;
        extra_state = to; moves++;
        return ESP_OK;
    }
    assert(strcmp(id, ID) == 0 && file_exists && file_state == from);
    if (io_failure || rename_failure) return ESP_FAIL;
    file_state = to;
    moves++;
    return ESP_OK;
}
esp_err_t memo_spool_set_state(const char *id, memo_spool_state_t to)
{ return memo_spool_move(id, MEMO_SPOOL_PENDING, to); }
static FILE *upload_file(const char *path, const char *mode)
{
    assert((strstr(path, ID) != NULL || strstr(path, EXTRA_ID) != NULL) && strcmp(mode, "rb") == 0);
    FILE *f = tmpfile();
    assert(f != NULL);
    char bytes[80] = {0};
    assert(fwrite(bytes, 1, sizeof(bytes), f) == sizeof(bytes));
    rewind(f);
    return f;
}
static memo_spool_entry_t entry(void)
{
    memo_spool_entry_t e;
    assert(memo_spool_scan(&e, 1) == 1);
    return e;
}
static void reset(void)
{
    file_exists = online = true;
    file_state = MEMO_SPOOL_PENDING;
    io_failure = rename_failure = transport_failure = false;
    capture_during_probe = playback_during_response = false;
    extra_exists = false; extra_state = MEMO_SPOOL_LEGACY_KEPT;
    posts = deletes = moves = delays = 0;
    probes = invalidations = 0;
    s_rediscover_requested = request_during_probe = probe_failure = false;
    max_ms = max_bytes = 0; code = 400;
    s_review = (memo_upload_rejection_t){0};
    s_attempt_id[0] = '\0';
    s_policy = (memo_upload_policy_t){0};
    s_running = s_capture_busy = s_playback_busy = false;
    s_review_lock = NULL; s_done = NULL; s_task = NULL; stop_timeout = false; recovery_blocked = scan_failure = false;
    assert(memo_upload_start() == ESP_OK);
}
static void exhaust(void)
{
    memo_spool_entry_t e = entry();
    assert(!upload_once(&e));
    assert(!upload_once(&e));
    assert(upload_once(&e));
    assert(posts == 3 && deletes == 0 && file_state == MEMO_SPOOL_BAD);
    assert(review_rejected());
    memo_upload_rejection_t r;
    assert(memo_upload_rejection(&r) && !r.deciding);
    assert(r.duration_ms == 60 && r.recorded_at == 1700000000);
}
static void test_three_rejections_wait_for_explicit_delete(void)
{
    reset(); exhaust();
    upload_task(NULL); /* multiple idle cycles must not make a fourth POST */
    assert(posts == 3 && deletes == 0 && file_exists);
    assert(memo_upload_pending());
    assert(memo_upload_choose("wrong-id", true) == ESP_ERR_INVALID_STATE);
    assert(memo_upload_choose(ID, true) == ESP_OK);
    assert(deletes == 0); /* queuing a choice is not a disk operation */
    assert(memo_upload_choose(ID, true) == ESP_ERR_INVALID_STATE);
    assert(review_rejected());
    assert(deletes == 1 && !file_exists && !memo_upload_pending());
    memo_upload_rejection_t r;
    assert(!memo_upload_rejection(&r));
}
static void test_retry_round_and_failed_choices(void)
{
    reset(); exhaust();
    io_failure = true;
    assert(memo_upload_choose(ID, true) == ESP_OK);
    assert(review_rejected());
    memo_upload_rejection_t r;
    assert(memo_upload_rejection(&r) && r.operation_failed && !r.deciding);
    assert(file_exists && deletes == 0 && file_state == MEMO_SPOOL_BAD);
    io_failure = true;
    assert(memo_upload_choose(ID, false) == ESP_OK);
    assert(review_rejected());
    assert(memo_upload_rejection(&r) && r.operation_failed && !r.deciding);
    assert(file_exists && deletes == 0 && file_state == MEMO_SPOOL_BAD);
    io_failure = false;
    assert(memo_upload_choose(ID, false) == ESP_OK);
    assert(review_rejected());
    assert(file_exists && file_state == MEMO_SPOOL_PENDING && deletes == 0);
    assert(memo_upload_pending() && !review_rejected());
    assert(!memo_upload_rejection(&r));
    assert(s_policy.rejections == 0 && s_backoff_ms == UPLOAD_RETRY_MIN_MS);
    memo_spool_entry_t e = entry();
    assert(!upload_once(&e));
    assert(!upload_once(&e));
    assert(upload_once(&e));
    assert(posts == 6 && deletes == 0 && file_state == MEMO_SPOOL_BAD);
    assert(memo_upload_rejection(&r) && strcmp(r.id, ID) == 0);
    assert(memo_upload_pending());
    assert(memo_upload_choose(ID, false) == ESP_OK && review_rejected());
    code = 201; e = entry();
    assert(upload_once(&e));
    assert(posts == 7 && deletes == 1 && !memo_upload_pending());
}
static void test_reboot_recovers_review_without_network(void)
{
    reset(); exhaust();
    s_review = (memo_upload_rejection_t){0}; /* persisted .bad, fresh RAM */
    online = false;
    assert(review_rejected());
    memo_upload_rejection_t r;
    assert(memo_upload_rejection(&r));
    assert(strcmp(r.id, ID) == 0 && !r.deciding && file_exists && deletes == 0);
}
static void test_transport_and_server_errors_do_not_discard(void)
{
    reset(); memo_spool_entry_t e = entry();
    transport_failure = true;
    for (int i = 0; i < 6; i++) assert(!upload_once(&e));
    assert(s_policy.rejections == 0 && deletes == 0 && moves == 0);
    transport_failure = false; code = 500;
    for (int i = 0; i < 6; i++) assert(!upload_once(&e));
    assert(s_policy.rejections == 0 && deletes == 0 && moves == 0);
    code = 400;
    assert(!upload_once(&e)); assert(!upload_once(&e));
    code = 201;
    assert(upload_once(&e) && deletes == 1 && s_policy.rejections == 0);
}
static void test_rename_retry_does_not_post_again(void)
{
    reset(); memo_spool_entry_t e = entry();
    assert(!upload_once(&e)); assert(!upload_once(&e));
    rename_failure = true;
    assert(!upload_once(&e));
    assert(!upload_once(&e));
    assert(posts == 3 && file_exists && deletes == 0);
    rename_failure = false;
    assert(upload_once(&e) && posts == 3 && file_state == MEMO_SPOOL_BAD);
}
static void test_capture_and_confirmation_block_upload(void)
{
    reset(); memo_upload_set_capture_busy(true);
    upload_task(NULL);
    assert(posts == 0 && deletes == 0 && moves == 0);
    memo_upload_set_capture_busy(false);
    assert(!s_capture_busy);
    for (int state = MEMO_SPOOL_RECORDING; state <= MEMO_SPOOL_DRAFT; state++) {
        reset(); file_state = (memo_spool_state_t)state;
        assert(memo_upload_pending());
        upload_task(NULL);
        assert(posts == 0 && deletes == 0 && moves == 0);
    }
    reset(); recovery_blocked = true;
    assert(memo_upload_pending());
    upload_task(NULL);
    assert(posts == 0 && deletes == 0 && moves == 0);
    reset(); capture_during_probe = true;
    upload_task(NULL);
    assert(posts == 0 && deletes == 0 && moves == 0);
}
static void test_local_limit_and_429_use_same_budget(void)
{
    reset(); max_ms = 30;
    memo_spool_entry_t e = entry();
    assert(!upload_once(&e)); assert(!upload_once(&e)); assert(upload_once(&e));
    assert(posts == 0 && file_state == MEMO_SPOOL_BAD && deletes == 0);
    reset(); max_bytes = 40; e = entry();
    assert(!upload_once(&e)); assert(!upload_once(&e)); assert(upload_once(&e));
    assert(posts == 0 && file_state == MEMO_SPOOL_BAD);
    reset(); code = 429; exhaust();
}

static void test_next_rejected_file_rejects_stale_choice(void)
{
    reset(); exhaust();
    const char *previous_id = ID;
    assert(memo_upload_choose(ID, false) == ESP_OK);
    assert(review_rejected());
    ID = "00000000-0000-4000-8000-000000000002";
    file_state = MEMO_SPOOL_BAD; /* another recovered recording */
    assert(review_rejected());
    assert(memo_upload_choose(previous_id, true) == ESP_ERR_INVALID_STATE);
    assert(deletes == 0);
    assert(memo_upload_choose(ID, false) == ESP_OK);
    assert(review_rejected() && deletes == 0 && file_state == MEMO_SPOOL_PENDING);
    assert(memo_upload_pending());
    ID = previous_id;
}

static void test_legacy_files_remain_unresolved_until_retry_or_delete(void)
{
    reset(); file_exists = false; extra_exists = true;
    assert(memo_upload_pending() && review_rejected());
    memo_upload_rejection_t r;
    assert(memo_upload_rejection(&r) && r.legacy_kept);
    assert(memo_upload_choose(EXTRA_ID, false) == ESP_OK && review_rejected());
    assert(extra_state == MEMO_SPOOL_PENDING && memo_upload_pending() && deletes == 0);

    file_exists = true; file_state = MEMO_SPOOL_BAD;
    assert(!review_rejected()); /* retry the active item before another legacy dialog */
    assert(!memo_upload_rejection(&r));
    memo_spool_entry_t active;
    assert(pending_entry(&active) && strcmp(active.id, EXTRA_ID) == 0);
    assert(!upload_once(&active)); assert(!upload_once(&active)); assert(upload_once(&active));
    assert(memo_upload_rejection(&r) && strcmp(r.id, EXTRA_ID) == 0 && !r.legacy_kept);
    assert(posts == 3 && deletes == 0 && memo_upload_pending());
    assert(memo_upload_choose(EXTRA_ID, true) == ESP_OK && review_rejected());
    assert(!extra_exists && file_exists && memo_upload_pending());
    assert(review_rejected());
    assert(memo_upload_rejection(&r) && strcmp(r.id, ID) == 0);
    assert(memo_upload_choose(ID, true) == ESP_OK && review_rejected());
    assert(!memo_upload_pending() && deletes == 2);

    reset(); file_exists = false; extra_exists = true;
    assert(review_rejected());
    assert(memo_upload_choose(EXTRA_ID, true) == ESP_OK && review_rejected());
    assert(!extra_exists && deletes == 1 && !memo_upload_pending());
}
static void test_stop_timeout_retains_worker_ownership(void)
{
    reset(); stop_timeout = true;
    assert(memo_upload_stop() == ESP_ERR_TIMEOUT);
    assert(s_task != NULL && !s_running);
    assert(memo_upload_start() == ESP_ERR_INVALID_STATE);
    assert(memo_upload_stop() == ESP_ERR_TIMEOUT);
    stop_timeout = false;
    assert(memo_upload_stop() == ESP_OK && s_task == NULL);
    assert(memo_upload_start() == ESP_OK);
}
static void test_local_playback_holds_accepted_upload_source(void)
{
    reset(); code=201;
    memo_upload_set_playback_busy(true);
    upload_task(NULL); assert(posts==0 && file_exists && deletes==0);
    memo_upload_set_playback_busy(false); s_running=true;
    memo_spool_entry_t e=entry(); playback_during_response=true;
    assert(upload_once(&e) && posts==1 && deletes==0 && file_exists);
    assert(memo_upload_pending());
    playback_during_response=false; memo_upload_set_playback_busy(false);
    assert(upload_once(&e) && posts==2 && deletes==1 && !file_exists);
}

static void test_manual_discovery_is_queued_and_coalesced(void)
{
    reset(); file_exists=false; online=false;
    memo_upload_request_discovery(); memo_upload_request_discovery();
    upload_task(NULL);
    assert(memo_upload_discovery_pending() && probes==0 && invalidations==0);
    online=true; s_running=true; delays=0;
    upload_task(NULL);
    assert(!memo_upload_discovery_pending() && probes==1 && invalidations==1);

    reset(); file_exists=false; memo_upload_set_capture_busy(true);
    memo_upload_request_discovery(); upload_task(NULL);
    assert(memo_upload_discovery_pending() && probes==0 && invalidations==0);
    memo_upload_set_capture_busy(false); s_running=true; delays=0;
    request_during_probe=true;
    upload_task(NULL);
    assert(!memo_upload_discovery_pending() && probes==2 && invalidations==2);

    reset(); file_exists=false; probe_failure=true;
    memo_upload_request_discovery(); upload_task(NULL);
    assert(!memo_upload_discovery_pending() && invalidations==1 && probes==1);
    assert(posts==0 && deletes==0);

    reset(); memo_upload_request_discovery();
    interruptible_delay(120000); assert(delays==0); // manual retry wakes backoff
    interruptible_delay(200); assert(delays==2); // busy wait must not spin
}

static void test_scan_failure_holds_capture_and_upload_immediately(void)
{
    reset();scan_failure=true;
    assert(memo_upload_pending());
    reset();scan_failure=true;
    assert(capture_busy());
    upload_task(NULL);assert(posts==0 && file_exists && deletes==0);
}

int main(void)
{
    test_scan_failure_holds_capture_and_upload_immediately();
    test_manual_discovery_is_queued_and_coalesced();
    test_local_playback_holds_accepted_upload_source();
    test_stop_timeout_retains_worker_ownership();
    test_three_rejections_wait_for_explicit_delete();
    test_retry_round_and_failed_choices();
    test_reboot_recovers_review_without_network();
    test_transport_and_server_errors_do_not_discard();
    test_rename_retry_does_not_post_again();
    test_capture_and_confirmation_block_upload();
    test_local_limit_and_429_use_same_budget();
    test_next_rejected_file_rejects_stale_choice();
    test_legacy_files_remain_unresolved_until_retry_or_delete();
    puts("memo uploader rejection/choice runtime: PASS");
    return 0;
}
