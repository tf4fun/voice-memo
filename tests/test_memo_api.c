#include <assert.h>
#include "../main/memo_api.c"

static const char *ID = "11111111-1111-4111-8111-111111111111";
static cJSON api = {.key="api", .type=2, .valuedouble=1};
static cJSON service = {.key="service", .type=1, .valuestring="memo", .next=&api};
static cJSON ping = {.child=&service};
static cJSON bytes = {.key="max_bytes", .type=2, .valuedouble=1048576};
static cJSON ms = {.key="max_memo_ms", .type=2, .valuedouble=300000, .next=&bytes};
static cJSON limits = {.key="limits", .child=&ms};
static cJSON info_api = {.key="api", .type=2, .valuedouble=1, .next=&limits};
static cJSON info = {.child=&info_api};
static cJSON snippet = {.key="snippet", .type=1, .valuestring="原始摘要"};
static cJSON status = {.key="status", .type=1, .valuestring="ready", .next=&snippet};
static cJSON duration = {.key="duration_ms", .type=2, .valuedouble=1200, .next=&status};
static cJSON time_node = {.key="recorded_at", .type=2, .valuedouble=1700000000, .next=&duration};
static cJSON id_node = {.key="id", .type=1, .next=&time_node};
static cJSON row = {.child=&id_node};
static cJSON items = {.key="items", .type=3, .child=&row};
static cJSON next_cursor = {.key="next_cursor", .type=1, .valuestring="next_token"};
static cJSON previous_cursor = {.key="previous_cursor", .type=1, .valuestring="prev_token", .next=&next_cursor};
static cJSON list = {.child=&items};
static char revision_string[]="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
static cJSON text_content = {.key="text", .type=1, .valuestring="abcdef"};
static cJSON text_total = {.key="total", .type=2, .valuedouble=12, .next=&text_content};
static cJSON text_end = {.key="end", .type=2, .valuedouble=6, .next=&text_total};
static cJSON text_offset = {.key="offset", .type=2, .valuedouble=0, .next=&text_end};
static cJSON text_revision = {.key="revision", .type=1, .valuestring=revision_string, .next=&text_offset};
static cJSON text_status = {.key="status", .type=1, .valuestring="ready", .next=&text_revision};
static cJSON text_id = {.key="id", .type=1, .next=&text_status};
static cJSON window = {.child=&text_id};
static char last_url[384];
static int late_action;
static esp_http_client_config_t request;
static bool endpoint_valid = true, oversized;
static memo_endpoint_t endpoint = {.host="127.0.0.1", .port=8080};
static int locks, http_code = 200, invalidations;

cJSON *cJSON_Parse(const char *text)
{
    if (!text) return NULL;
    if (strcmp(text,"ping") == 0) return &ping;
    if (strcmp(text,"info") == 0) return &info;
    if (strcmp(text,"list") == 0) return &list;
    if (strcmp(text,"window") == 0) return &window;
    return NULL;
}
void cJSON_Delete(cJSON *root) { (void)root; }
const cJSON *cJSON_GetObjectItem(const cJSON *root, const char *key)
{ for (const cJSON *n = root ? root->child : NULL; n; n=n->next) if (n->key && strcmp(key,n->key)==0) return n; return NULL; }
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void *)1; }
int xSemaphoreTake(SemaphoreHandle_t s, unsigned t) { (void)s; (void)t; assert(locks++ == 0); return pdTRUE; }
int xSemaphoreGive(SemaphoreHandle_t s) { (void)s; assert(--locks == 0); return pdTRUE; }
bool memo_discovery_endpoint(memo_endpoint_t *out) { if (endpoint_valid) *out = endpoint; return endpoint_valid; }
esp_err_t memo_discovery_search(uint32_t timeout) { (void)timeout; endpoint_valid = true; return ESP_OK; }
void memo_discovery_confirm(const memo_endpoint_t *e) { (void)e; endpoint.confirmed = true; }
void memo_discovery_invalidate(const memo_endpoint_t *e) { (void)e; endpoint_valid = false; invalidations++; }
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *cfg)
{ request = *cfg; strcpy(last_url,cfg->url); return (void *)2; }
esp_err_t esp_http_client_perform(esp_http_client_handle_t client)
{
    (void)client; assert(request.timeout_ms == 3000);
    if (late_action == 1) { late_action=0; assert(memo_api_request_page(0)==ESP_OK); }
    if (late_action == 2) { late_action=0; memo_api_cancel_text(); }
    if (late_action == 3) { late_action=0; assert(memo_api_request_text("22222222-2222-4222-8222-222222222222",0,false,"")==ESP_OK); }
    const char *body = strstr(request.url,"/ping") ? "ping" : strstr(request.url,"/server") ? "info" : strstr(request.url,"/text?") ? "window" : "list";
    esp_http_client_event_t ev = {.event_id=HTTP_EVENT_ON_DATA, .user_data=request.user_data, .data=(void *)body, .data_len=(int)strlen(body)};
    request.event_handler(&ev);
    if (oversized) { static char large[32768]; ev.data=large; ev.data_len=sizeof(large); request.event_handler(&ev); }
    return ESP_OK; /* Even if the transport ignores a callback error. */
}
int esp_http_client_get_status_code(esp_http_client_handle_t c) { (void)c; return http_code; }
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t c) { (void)c; return ESP_OK; }
esp_err_t esp_http_client_open(esp_http_client_handle_t c, int n) { (void)c; (void)n; return ESP_FAIL; }
int esp_http_client_fetch_headers(esp_http_client_handle_t c) { (void)c; return 0; }
int esp_http_client_read(esp_http_client_handle_t c, char *out, int n) { (void)c; (void)out; (void)n; return 0; }
esp_err_t esp_http_client_close(esp_http_client_handle_t c) { (void)c; return ESP_OK; }
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t c) { (void)c; return false; }
int main(void)
{
    id_node.valuestring = (char *)ID;
    assert(memo_api_init() == ESP_OK);
    assert(memo_api_probe() == ESP_OK && endpoint.confirmed);
    assert(memo_api_max_memo_ms() == 300000 && memo_api_max_bytes() == 1048576);
    api.valuedouble=2;
    assert(memo_api_probe() == ESP_ERR_INVALID_RESPONSE && invalidations == 1);
    assert(memo_api_max_bytes() == 0);
    api.valuedouble=1; bytes.valuedouble=-1;
    assert(memo_api_probe() == ESP_ERR_INVALID_RESPONSE);
    bytes.valuedouble=1048576;
    assert(memo_api_probe() == ESP_OK);
    assert(memo_api_refresh() == ESP_OK && memo_api_count() == 1 && memo_api_all_terminal());
    memo_api_item_t copy;
    assert(memo_api_item_copy(0,&copy) && strcmp(copy.id,ID)==0);
    assert(!memo_api_item_copy(1,&copy));
    assert(memo_api_take_updated() && !memo_api_take_updated());
    assert(MEMO_API_MAX_ITEMS == 4 && strstr(last_url,"limit=4&"));
    cJSON bounded_rows[5] = {0}, bounded_ids[5] = {0};
    char bounded_uuid[5][37];
    for (int i=0; i<5; i++) {
        snprintf(bounded_uuid[i],sizeof(bounded_uuid[i]),"00000000-0000-4000-8000-%012u",(unsigned)i+1);
        bounded_ids[i]=(cJSON){.key="id",.type=1,.valuestring=bounded_uuid[i],.next=&time_node};
        bounded_rows[i].child=&bounded_ids[i];
        if(i>0 && i<4)bounded_rows[i-1].next=&bounded_rows[i];
    }
    items.child=&bounded_rows[0];
    assert(memo_api_refresh()==ESP_OK && memo_api_count()==4);
    assert(memo_api_take_updated());
    bounded_rows[3].next=&bounded_rows[4];
    assert(memo_api_refresh()==ESP_ERR_INVALID_RESPONSE && memo_api_count()==4);
    assert(!memo_api_take_updated());
    items.child=&row;
    assert(memo_api_refresh()==ESP_OK && memo_api_count()==1 && memo_api_take_updated());
    items.type=0;
    assert(memo_api_refresh() == ESP_ERR_INVALID_RESPONSE && memo_api_count() == 1);
    items.type=3; oversized=true;
    assert(memo_api_refresh() != ESP_OK && memo_api_item_copy(0,&copy));
    assert(strcmp(copy.snippet,"原始摘要")==0 && !memo_api_take_updated());
    oversized=false; snippet.valuestring="";
    assert(memo_api_refresh() == ESP_OK && memo_api_item_copy(0,&copy) && copy.snippet[0]=='\0');
    memo_api_request_refresh(); memo_api_request_refresh();
    assert(memo_api_take_refresh_request() && !memo_api_take_refresh_request());
    body_buf_t body={0}; static char maximum[32768];
    assert(body_append(&body,maximum,sizeof(maximum)) == ESP_OK && body.cap == 32769);
    assert(body_append(&body,"x",1) == ESP_ERR_INVALID_SIZE);
    assert(body_append(&body,"",0) == ESP_ERR_INVALID_SIZE); free(body.data);
    // Page moves preserve metadata as a single snapshot and never accumulate history.
    items.next=&previous_cursor;
    assert(memo_api_refresh()==ESP_OK);
    memo_api_page_t page; memo_api_item_t page_items[MEMO_API_MAX_ITEMS];
    assert(memo_api_page_copy(page_items,MEMO_API_MAX_ITEMS,&page) && page.count==1);
    assert(!strcmp(page.next_cursor,"next_token") && !strcmp(page.previous_cursor,"prev_token"));
    assert(memo_api_request_page(1)==ESP_OK);
    assert(memo_api_request_page(1)==ESP_ERR_INVALID_STATE);
    assert(memo_api_refresh()==ESP_OK && strstr(last_url,"direction=older&cursor=next_token"));
    assert(memo_api_page_copy(NULL,0,&page) && page.generation==1 && page.navigation==1 && !page.loading);
    assert(memo_api_request_page(-1)==ESP_OK && memo_api_refresh()==ESP_OK);
    assert(strstr(last_url,"direction=newer&cursor=prev_token"));
    const uint32_t good_generation=s_page.generation;
    assert(memo_api_request_page(1)==ESP_OK);http_code=503;
    assert(memo_api_refresh()!=ESP_OK);http_code=200;
    assert(memo_api_page_copy(page_items,MEMO_API_MAX_ITEMS,&page) && page.count==1 && page.error!=ESP_OK && !page.loading && page.generation==good_generation);
    assert(memo_api_refresh()==ESP_OK && strstr(last_url,"cursor=prev_token")); // Failed move restored the displayed query.
    late_action=1;
    assert(memo_api_refresh()==ESP_ERR_INVALID_STATE);
    assert(memo_api_page_copy(NULL,0,&page) && page.loading);
    assert(memo_api_refresh()==ESP_OK && strstr(last_url,"direction=older&cursor="));
    assert(memo_api_page_copy(NULL,0,&page) && page.navigation==2 && !page.loading);
    next_cursor.valuestring="bad&query";
    assert(memo_api_refresh()==ESP_ERR_INVALID_RESPONSE);next_cursor.valuestring="";
    assert(memo_api_refresh()==ESP_OK && memo_api_request_page(1)==ESP_ERR_NOT_FOUND);
    duration.valuedouble=-1;assert(memo_api_refresh()==ESP_ERR_INVALID_RESPONSE);duration.valuedouble=1200;
    time_node.valuedouble=9223372036854775808.0;assert(memo_api_refresh()==ESP_ERR_INVALID_RESPONSE);time_node.valuedouble=0;
    status.valuestring="transcribing";
    assert(memo_api_refresh()==ESP_OK && memo_api_item_copy(0,&copy) && !strcmp(copy.status,"transcribing"));

    // Text windows are asynchronous, bounded, versioned and invalidated on navigation.
    text_id.valuestring=(char *)ID;
    assert(memo_api_request_text("bad",0,false,"")==ESP_ERR_INVALID_ARG);
    assert(memo_api_request_text(ID,0,false,"not-a-revision")==ESP_ERR_INVALID_ARG);
    assert(memo_api_request_text(ID,0,false,"")==ESP_OK);
    memo_api_text_t text_copy;
    assert(memo_api_text_copy(&text_copy) && text_copy.pending);
    memo_api_poll_text();
    assert(memo_api_text_copy(&text_copy) && !text_copy.pending && text_copy.error==ESP_OK && !strcmp(text_copy.text,"abcdef") && text_copy.total==12);
    assert(strstr(last_url,"limit=2048&offset=0&direction=next&revision="));
    text_offset.valuedouble=6;text_end.valuedouble=12;text_content.valuestring="ghijkl";
    assert(memo_api_request_text(ID,6,false,revision_string)==ESP_OK);memo_api_poll_text();
    assert(memo_api_text_copy(&text_copy) && text_copy.offset==6 && !strcmp(text_copy.text,"ghijkl"));
    text_end.valuedouble=13;
    assert(memo_api_request_text(ID,6,false,revision_string)==ESP_OK);memo_api_poll_text();
    assert(memo_api_text_copy(&text_copy) && text_copy.error==ESP_ERR_INVALID_RESPONSE && !text_copy.pending && !strcmp(text_copy.text,"ghijkl"));
    text_end.valuedouble=12;
    assert(memo_api_request_text(ID,6,false,revision_string)==ESP_OK);http_code=503;memo_api_poll_text();http_code=200;
    assert(memo_api_text_copy(&text_copy) && text_copy.pending && text_copy.error==ESP_FAIL);
    memo_api_poll_text();assert(memo_api_text_copy(&text_copy) && !text_copy.pending && text_copy.error==ESP_OK);
    assert(memo_api_request_text(ID,6,false,revision_string)==ESP_OK);http_code=409;memo_api_poll_text();http_code=200;
    assert(memo_api_text_copy(&text_copy) && !text_copy.pending && text_copy.error==ESP_ERR_INVALID_STATE);
    assert(memo_api_request_text(ID,6,false,revision_string)==ESP_OK);http_code=400;memo_api_poll_text();http_code=200;
    assert(memo_api_text_copy(&text_copy) && !text_copy.pending && text_copy.error==ESP_ERR_INVALID_RESPONSE);
    assert(memo_api_request_text(ID,12,true,revision_string)==ESP_OK);memo_api_poll_text();
    assert(memo_api_text_copy(&text_copy) && text_copy.offset==6 && text_copy.end==12 && text_copy.error==ESP_OK);
    text_content.valuestring="\xED\xA0\x80xyz";
    assert(memo_api_request_text(ID,6,false,revision_string)==ESP_OK);memo_api_poll_text();
    assert(memo_api_text_copy(&text_copy) && text_copy.error==ESP_ERR_INVALID_RESPONSE);
    text_content.valuestring="ghijkl";
    assert(memo_api_request_text(ID,6,false,revision_string)==ESP_OK);late_action=2;memo_api_poll_text();
    assert(!memo_api_text_copy(&text_copy));
    text_offset.valuedouble=0;text_end.valuedouble=6;text_content.valuestring="abcdef";
    assert(memo_api_request_text(ID,0,false,"")==ESP_OK);late_action=3;memo_api_poll_text();
    assert(memo_api_text_copy(&text_copy) && !strcmp(text_copy.id,"22222222-2222-4222-8222-222222222222") && text_copy.pending && !text_copy.text[0]);
    memo_api_cancel_text();
    puts("memo API body/cache limits, bidirectional page snapshots, stale moves and bounded/versioned text worker: PASS");
}
