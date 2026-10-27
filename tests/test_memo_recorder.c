/* Exercise the real capture worker's failure/stop ownership and encoder lifetime. */
#include <assert.h>
#include <stdio.h>
#include "../main/memo_recorder.c"

static int signal_done, read_count, append_calls, recover_calls, close_calls, finalize_calls;
static bool write_failure, encode_failure, read_failure, create_failure, task_failure;
static bool encoder_live;
static int fail_read_at;
static int64_t clock_us;
static const char *ID = "00000000-0000-4000-8000-000000000001";
SemaphoreHandle_t xSemaphoreCreateBinary(void) { return (void *)1; }
int xSemaphoreTake(SemaphoreHandle_t sem, unsigned ticks)
{ (void)sem; (void)ticks; if (!signal_done) return 0; signal_done = 0; return pdTRUE; }
int xSemaphoreGive(SemaphoreHandle_t sem) { (void)sem; signal_done = 1; return pdTRUE; }
int xTaskCreate(void (*fn)(void *), const char *name, unsigned stack, void *arg,
                unsigned priority, TaskHandle_t *task)
{ (void)fn; (void)name; (void)stack; (void)arg; (void)priority;
  if (task_failure) return 0;
  *task = (void *)2; return pdPASS; }
void vTaskDelete(TaskHandle_t task) { (void)task; }
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task) { (void)task; return 100; }
uint32_t esp_get_free_heap_size(void) { return 100000; }
int64_t esp_timer_get_time(void) { return clock_us; }
uint64_t memo_net_now(void) { return 1700000000; }
esp_err_t bsp_audio_set_format(int rate, int bits, int channels)
{ assert(rate == 16000 && bits == 16 && channels == 1); return ESP_OK; }
esp_err_t bsp_audio_read(void *data, size_t bytes)
{
    memset(data, 0, bytes); read_count++; clock_us += 60000;
    if (read_failure || read_count == fail_read_at) return ESP_FAIL;
    if (read_count >= 20) s_rec.active = false; /* normal user release */
    return ESP_OK;
}
esp_audio_err_t esp_opus_enc_open(void *cfg, int len, void **enc)
{ (void)cfg; (void)len; assert(!encoder_live); encoder_live = true; *enc = (void *)3; return 0; }
esp_audio_err_t esp_opus_enc_get_frame_size(void *enc, int *in, int *out)
{ (void)enc; *in = 1920; *out = 2048; return 0; }
esp_audio_err_t esp_opus_enc_process(void *enc, esp_audio_enc_in_frame_t *in,
                                     esp_audio_enc_out_frame_t *out)
{ (void)enc; (void)in; memset(out->buffer, 42, 150); out->encoded_bytes = 150;
  return encode_failure ? -1 : 0; }
void esp_opus_enc_close(void *enc)
{ assert(enc != NULL && encoder_live); encoder_live = false; close_calls++; }
esp_err_t memo_spool_mount(void) { return ESP_OK; }
esp_err_t memo_spool_create(const char *id, const uint8_t raw[80], void **file)
{ assert(strcmp(id, ID) == 0 && memcmp(raw, "MEMO", 4) == 0);
  if (create_failure) return ESP_FAIL;
  *file = tmpfile(); assert(*file); return ESP_OK; }
esp_err_t memo_spool_append(void *file, const void *data, size_t len)
{ assert(file && data && len > 0); append_calls++; return write_failure ? ESP_FAIL : ESP_OK; }
esp_err_t memo_spool_finalize(void *file, const char *id, uint32_t frames, uint32_t duration,
                             uint32_t *size)
{ assert(strcmp(id, ID) == 0 && duration == frames * 60); assert(fclose(file) == 0);
  finalize_calls++; *size = 80 + frames * 152; return ESP_OK; }
esp_err_t memo_spool_discard(void *file, const char *id)
{ (void)id; return fclose(file) == 0 ? ESP_OK : ESP_FAIL; }
int memo_spool_recover(void) { recover_calls++; return 1; }
static void reset(void)
{
    assert(s_task == NULL && !encoder_live);
    signal_done = read_count = append_calls = recover_calls = finalize_calls = 0;
    fail_read_at = 0;
    write_failure = encode_failure = read_failure = create_failure = task_failure = false;
}
int main(void)
{
    assert(memo_recorder_init() == ESP_OK && !encoder_live);
    reset(); assert(memo_recorder_start(ID) == ESP_OK && encoder_live);
    clock_us += 300000000; /* Cap tracks wall time, including audio stalls. */
    assert(memo_recorder_elapsed_ms() == 300000);
    assert(memo_recorder_start(ID) == ESP_ERR_INVALID_STATE);
    assert(memo_recorder_stop(true, NULL) == ESP_ERR_TIMEOUT);
    assert(encoder_live && s_task != NULL && finalize_calls == 0);
    assert(memo_recorder_start(ID) == ESP_ERR_INVALID_STATE);
    recorder_task(NULL);
    assert(memo_recorder_stop(true, NULL) == ESP_FAIL && recover_calls == 1 && !encoder_live);

    reset(); assert(memo_recorder_start(ID) == ESP_OK);
    recorder_task(NULL);
    assert(!memo_recorder_is_active());
    memo_recorder_result_t result;
    assert(memo_recorder_stop(true, &result) == ESP_OK);
    assert(result.frame_count == 20 && result.duration_ms == 1200 && append_calls == 2);
    assert(!encoder_live && s_task == NULL);

    reset(); write_failure = true; assert(memo_recorder_start(ID) == ESP_OK);
    recorder_task(NULL);
    assert(!memo_recorder_is_active() && append_calls == 1 && read_count < 20);
    assert(memo_recorder_stop(true, NULL) == ESP_FAIL && recover_calls == 1 && !encoder_live);
    reset(); encode_failure = true; assert(memo_recorder_start(ID) == ESP_OK);
    recorder_task(NULL);
    assert(read_count == 1 && append_calls == 0);
    assert(memo_recorder_stop(true, NULL) == ESP_FAIL && recover_calls == 1);
    reset(); read_failure = true; assert(memo_recorder_start(ID) == ESP_OK);
    recorder_task(NULL);
    assert(read_count == 1 && append_calls == 0);
    assert(memo_recorder_stop(true, NULL) == ESP_FAIL && recover_calls == 1);
    reset(); create_failure = true;
    assert(memo_recorder_start(ID) == ESP_FAIL && !encoder_live);
    reset(); task_failure = true;
    assert(memo_recorder_start(ID) == ESP_ERR_NO_MEM && !encoder_live);
    reset(); fail_read_at = 5;
    assert(memo_recorder_start(ID) == ESP_OK);
    recorder_task(NULL);
    assert(read_count == 5 && append_calls == 1 && s_rec.frames == 4);
    assert(memo_recorder_stop(true, NULL) == ESP_FAIL && recover_calls == 1);
    assert(close_calls == 8);
    puts("memo recorder durable write/error/stop/wall clock/encoder ownership: PASS");
}
