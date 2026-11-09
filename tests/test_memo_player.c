// Real worker, file/fragmented HTTP paths and lifecycle, using deterministic RTOS hooks.
#include <assert.h>
#include <unistd.h>
#include "../main/memo_player.c"

static int done, writes, decodes, opens, closes, paused_ticks, http_offset;
static bool fail_task, pause_test, stop_in_read, decoder_live;
static unsigned char container[160];
static size_t container_size;
static const char *ID="00000000-0000-4000-8000-000000000001";
SemaphoreHandle_t xSemaphoreCreateBinary(void) { return (void *)1; }
int xSemaphoreTake(SemaphoreHandle_t sem, unsigned ticks)
{ (void)sem; (void)ticks; if (!done) return 0; done=0; return pdTRUE; }
int xSemaphoreGive(SemaphoreHandle_t sem) { (void)sem; done=1; return pdTRUE; }
int xTaskCreate(void (*fn)(void *), const char *n,unsigned s,void *a,unsigned p,TaskHandle_t *out)
{ (void)fn;(void)n;(void)s;(void)a;(void)p; if(fail_task)return 0;*out=(void *)2;return pdPASS; }
void vTaskDelete(TaskHandle_t task) { assert(task!=NULL); }
void vTaskSuspend(TaskHandle_t task) { assert(task==NULL); }
void vTaskDelay(unsigned ticks)
{
    assert(ticks==20 && memo_player_is_paused() && memo_player_position_ms()==120);
    assert(writes==2 && decoder_live); paused_ticks++;
    assert(memo_player_resume()==ESP_OK);
}
uint32_t esp_get_free_heap_size(void) { return 100000; }
esp_err_t bsp_audio_set_format(int rate,int bits,int channels)
{ assert(rate==16000 && bits==16 && channels==1);return ESP_OK; }
esp_err_t bsp_audio_write(const void *data,size_t bytes)
{
    assert(data && bytes==1920);writes++;
    if (pause_test && writes==2) assert(memo_player_pause()==ESP_OK);
    return ESP_OK;
}
esp_audio_err_t esp_opus_dec_open(void *cfg,int len,void **out)
{ (void)cfg;(void)len;assert(!decoder_live);decoder_live=true;opens++;*out=(void *)3;return 0; }
void esp_opus_dec_close(void *dec)
{ assert(dec && decoder_live);decoder_live=false;closes++; }
esp_audio_err_t esp_opus_dec_decode(void *d,esp_audio_dec_in_raw_t *in,
                                   esp_audio_dec_out_frame_t *out,esp_audio_dec_info_t *info)
{
    (void)info;assert(d && decoder_live && in->len==1);
    decodes++;assert(in->buffer[0]==decodes); // Resume must never replay frame 1.
    assert(out->len>=1920);memset(out->buffer,0,1920);out->decoded_size=1920;return 0;
}
bool memo_discovery_endpoint(memo_endpoint_t *out)
{ *out=(memo_endpoint_t){.host="127.0.0.1",.port=8080,.confirmed=true};return true; }
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *cfg)
{ assert(cfg->timeout_ms<=2000);http_offset=0;return (void *)4; }
esp_err_t esp_http_client_open(esp_http_client_handle_t c,int len) { (void)c;(void)len;return ESP_OK; }
int esp_http_client_fetch_headers(esp_http_client_handle_t c) { (void)c;return container_size; }
int esp_http_client_get_status_code(esp_http_client_handle_t c) { (void)c;return 200; }
esp_err_t esp_http_client_close(esp_http_client_handle_t c) { (void)c;return ESP_OK; }
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t c) { (void)c;return ESP_OK; }
int esp_http_client_read(esp_http_client_handle_t c,char *out,int len)
{
    (void)c;assert(len>0);
    if (stop_in_read) {
        assert(memo_player_stop()==ESP_ERR_TIMEOUT); // Current worker cannot acknowledge itself.
        assert(memo_player_is_busy());return 0;
    }
    if ((size_t)http_offset>=container_size)return 0;
    *out=container[http_offset++];return 1; // Every header and packet fragmented.
}
static void reset(void)
{
    memo_player_poll();assert(!memo_player_is_busy() && !decoder_live && !done);
    writes=decodes=paused_ticks=0; pause_test=stop_in_read=fail_task=false;
}
int main(void)
{
    memo_container_header_t header={.sample_rate=16000,.channels=1,.frame_ms=60,
                                    .duration_ms=240,.frame_count=4};
    strcpy(header.memo_id,ID);memo_container_encode(&header,container);
    container_size=MEMO_CONTAINER_HEADER_SIZE;
    for (int i=1;i<=4;i++) {container[container_size++]=1;container[container_size++]=0;container[container_size++]=i;}
    char path[]="/tmp/memo-player-XXXXXX";int fd=mkstemp(path);assert(fd>=0);
    assert(write(fd,container,container_size)==(ssize_t)container_size);close(fd);
    assert(memo_player_init()==ESP_OK);
    reset();pause_test=true;
    assert(memo_player_play_file(path)==ESP_OK);
    assert(memo_player_stop()==ESP_ERR_TIMEOUT && memo_player_is_busy());
    assert(memo_player_play_file(path)==ESP_ERR_INVALID_STATE);
    player_task(NULL);assert(memo_player_stop()==ESP_OK && !memo_player_is_busy());
    reset();pause_test=true;
    assert(memo_player_play_file(path)==ESP_OK);player_task(NULL);
    assert(writes==4 && decodes==4 && paused_ticks==1 && done);
    assert(memo_player_position_ms()==240 && !memo_player_is_active());
    // Natural completion must be joined before the next worker, without stale acknowledgement.
    pause_test=false; writes=decodes=0;
    assert(memo_player_play_file(path)==ESP_OK && !done);
    assert(memo_player_position_ms()==0);
    assert(memo_player_stop()==ESP_ERR_TIMEOUT && memo_player_is_busy());
    player_task(NULL);memo_player_poll();assert(!memo_player_is_busy());
    reset();pause_test=true;
    assert(memo_player_play_remote(ID)==ESP_OK);stream_task(NULL);
    assert(writes==4 && decodes==4 && paused_ticks==1);
    assert(memo_player_position_ms()==240 && !memo_player_is_active());memo_player_poll();
    reset();stop_in_read=true;
    assert(memo_player_play_remote(ID)==ESP_OK);stream_task(NULL);
    assert(writes==0 && done && memo_player_is_busy());
    assert(memo_player_stop()==ESP_OK && !memo_player_is_busy());
    reset();fail_task=true;
    assert(memo_player_play_file(path)==ESP_ERR_NO_MEM && !memo_player_is_busy());
    assert(memo_player_pause()==ESP_ERR_INVALID_STATE && memo_player_resume()==ESP_ERR_INVALID_STATE);
    assert(opens==closes);unlink(path);
    puts("memo player file/HTTP pause-resume, fragmented reads, stop ownership and stale acknowledgements: PASS");
}
