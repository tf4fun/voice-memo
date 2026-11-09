/* Exercise the real capture worker's failure/stop ownership and encoder lifetime. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
static void *recorder_malloc(size_t size);
static void recorder_free(void *ptr);
#define malloc recorder_malloc
#define free recorder_free
#include "../main/memo_recorder.c"
#undef malloc
#undef free

static int signal_done, signal_start, read_count, append_calls, recover_calls, close_calls, finalize_calls;
static int task_creates, task_deletes, live_buffers, allocation_attempt, fail_allocation;
static bool encoder_failure, worker_live;
static bool write_failure, encode_failure, read_failure, create_failure, task_failure;
static bool discard_failure;
static bool encoder_live;
static int fail_read_at, release_after;
static int64_t clock_us;
static const char *ID = "00000000-0000-4000-8000-000000000001";
static int semaphores;
SemaphoreHandle_t xSemaphoreCreateBinary(void) { return (void *)(size_t)++semaphores; }
void vSemaphoreDelete(SemaphoreHandle_t sem) { (void)sem; }
int xSemaphoreTake(SemaphoreHandle_t sem, unsigned ticks)
{
    (void)ticks;
    int *signal = sem == s_start ? &signal_start : &signal_done;
    if (!*signal) {
        return 0;
    }
    *signal = 0;
    return pdTRUE;
}
int xSemaphoreGive(SemaphoreHandle_t sem)
{
    if (sem == s_start) {
        assert(worker_live && encoder_live && live_buffers == 2 && s_rec.file && s_rec.active);
        signal_start = 1;
    } else {
        signal_done = 1;
    }
    return pdTRUE;
}
int xTaskCreate(void (*fn)(void *), const char *name, unsigned stack, void *arg,
                unsigned priority, TaskHandle_t *task)
{ (void)fn; (void)name; (void)arg;
  assert(stack==28672 && priority==6 && !encoder_live && !worker_live && !signal_start);
  task_creates++;
  if (task_failure) return 0;
  worker_live=true;*task = (void *)3; return pdPASS; }
void vTaskDelete(TaskHandle_t task)
{ assert(worker_live);worker_live=false;task_deletes++;
  if(task)assert(!signal_start && !s_rec.active); }
void vTaskSuspend(TaskHandle_t task) { assert(task==NULL && signal_done && worker_live); }
static void *recorder_malloc(size_t size)
{ allocation_attempt++;if(allocation_attempt==fail_allocation)return NULL;
  void *p=malloc(size);if(p)live_buffers++;return p; }
static void recorder_free(void *ptr)
{ if(ptr){assert(live_buffers>0);live_buffers--;}free(ptr); }
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
    if (read_count >= release_after) s_rec.active = false; /* normal user release */
    return ESP_OK;
}
esp_audio_err_t esp_opus_enc_open(void *cfg, int len, void **enc)
{ (void)cfg; (void)len; assert(!encoder_live && worker_live && !signal_start);
  if(encoder_failure)return ESP_AUDIO_ERR_MEM_LACK;
  encoder_live = true; *enc = (void *)3; return 0; }
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
{ (void)id; const int closed=fclose(file); return closed==0 && !discard_failure ? ESP_OK : ESP_FAIL; }
int memo_spool_recover(void) { recover_calls++; return 1; }
static void reset(void)
{
    assert(s_task == NULL && !encoder_live && !worker_live && live_buffers==0);
    signal_done = signal_start = read_count = append_calls = recover_calls = finalize_calls = 0;
    allocation_attempt=fail_allocation=0;encoder_failure=false;
    fail_read_at = 0; release_after = 20;
    write_failure = encode_failure = read_failure = create_failure = task_failure = false;
    discard_failure = false;
}
int main(void)
{
    memo_signal_t signal={0};int16_t samples[100]={0};
    for(int n=0;n<49;n++)assert(memo_signal_read(&signal,samples,100,60).hint==MEMO_SIGNAL_NORMAL);
    assert(memo_signal_read(&signal,samples,100,60).hint==MEMO_SIGNAL_QUIET);
    for(int i=0;i<100;i++)samples[i]=1000;
    memo_signal_reading_t reading=memo_signal_read(&signal,samples,100,60);
    assert(reading.hint==MEMO_SIGNAL_NORMAL && reading.level==33);
    for(int i=0;i<100;i++)samples[i]=(i%2)?INT16_MIN:INT16_MAX;
    for(int n=0;n<2;n++)assert(memo_signal_read(&signal,samples,100,60).hint==MEMO_SIGNAL_NORMAL);
    reading=memo_signal_read(&signal,samples,100,60);
    assert(reading.hint==MEMO_SIGNAL_CLIPPED && reading.level==100);
    assert(memo_signal_read(&signal,samples,0,60).level==0);
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
    reset(); release_after = 6000; /* Delayed input heartbeat must not overshoot. */
    assert(memo_recorder_start(ID) == ESP_OK);
    recorder_task(NULL);
    assert(read_count == 5000 && !memo_recorder_is_active());
    assert(memo_recorder_stop(true, &result) == ESP_OK);
    assert(result.duration_ms == MEMO_RECORD_LIMIT_MS && result.frame_count == 5000);
    assert(close_calls == 8);
    reset();assert(memo_recorder_prepare(ID)==ESP_OK);
    assert(s_prepared && !memo_recorder_is_active() && memo_recorder_is_busy() && !signal_start);
    clock_us+=10000000;assert(memo_recorder_elapsed_ms()==0);
    assert(memo_recorder_prepare(ID)==ESP_ERR_INVALID_STATE);
    assert(memo_recorder_stop(true,NULL)==ESP_ERR_INVALID_STATE);
    assert(memo_recorder_cancel_prepared()==ESP_OK && !s_prepared && !signal_start);
    assert(memo_recorder_begin()==ESP_ERR_INVALID_STATE);
    reset();assert(memo_recorder_prepare(ID)==ESP_OK);
    int allocations=allocation_attempt,created=task_creates;
    assert(memo_recorder_begin()==ESP_OK && allocation_attempt==allocations && task_creates==created);
    assert(memo_recorder_cancel_prepared()==ESP_ERR_INVALID_STATE);
    recorder_task(NULL);assert(memo_recorder_stop(false,NULL)==ESP_OK);
    for(int i=0;i<100;i++){
        reset();assert(memo_recorder_prepare(ID)==ESP_OK);
        assert(memo_recorder_cancel_prepared()==ESP_OK);
    }
    for(int fail=1;fail<=2;fail++){
        reset();fail_allocation=fail;
        assert(memo_recorder_start(ID)==ESP_ERR_NO_MEM);
        assert(!memo_recorder_is_busy() && !signal_start && !worker_live && !live_buffers && !encoder_live);
    }
    reset();encoder_failure=true;
    assert(memo_recorder_start(ID)==ESP_ERR_NO_MEM && !worker_live && !signal_start);
    // Repeated starts re-use the gate; every failure above can retry immediately.
    for(int i=0;i<100;i++){
        reset();assert(memo_recorder_start(ID)==ESP_OK);
        recorder_task(NULL);
        assert(memo_recorder_stop(false,NULL)==ESP_OK);
    }
    reset();discard_failure=true;assert(memo_recorder_start(ID)==ESP_OK);
    recorder_task(NULL);assert(memo_recorder_stop(false,NULL)==ESP_FAIL);
    reset();assert(task_creates==task_deletes+1); // One injected create failure.
    puts("memo recorder durable write/error/stop/wall clock/encoder ownership: PASS");
}
