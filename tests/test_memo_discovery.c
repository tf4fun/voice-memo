#include <assert.h>
#include "../main/memo_discovery.c"
static int allocations, frees, initialisations, result_frees;
static bool held[3], query_failure, fail_address_lock;
SemaphoreHandle_t xSemaphoreCreateMutex(void) { if (fail_address_lock && allocations == 1) return NULL; return (void *)(uintptr_t)++allocations; }
int xSemaphoreTake(SemaphoreHandle_t sem,unsigned ticks)
{ (void)ticks; int i=(int)(uintptr_t)sem; assert(i>0 && i<3 && !held[i]);held[i]=true;return pdTRUE; }
int xSemaphoreGive(SemaphoreHandle_t sem)
{ int i=(int)(uintptr_t)sem;assert(held[i]);held[i]=false;return pdTRUE; }
esp_err_t mdns_init(void) { initialisations++;return ESP_OK; }
void mdns_free(void) { frees++; }
esp_err_t mdns_query_ptr(const char *service,const char *proto,uint32_t timeout,size_t max,mdns_result_t **out)
{
    (void)service;(void)proto;(void)timeout;(void)max;
    memo_endpoint_t during;
    assert(!memo_discovery_endpoint(&during)); /* Snapshot never takes the long query lock. */
    static mdns_ip_addr_t address={.addr={.type=0,.u_addr.ip4={.addr=0x0100007f}}};
    static mdns_result_t result={.port=8080,.addr=&address};
    *out=query_failure?NULL:&result;return query_failure?ESP_FAIL:ESP_OK;
}
esp_err_t mdns_query_srv(const char *i,const char *s,const char *p,uint32_t t,mdns_result_t **out)
{ (void)i;(void)s;(void)p;(void)t;*out=NULL;return ESP_FAIL; }
esp_err_t mdns_query_a(const char *h,uint32_t t,esp_ip4_addr_t *out)
{ (void)h;(void)t;(void)out;return ESP_FAIL; }
void mdns_query_results_free(mdns_result_t *results) { (void)results;result_frees++; }
int main(void)
{
    fail_address_lock=true;
    assert(memo_discovery_init()==ESP_ERR_NO_MEM && initialisations==0);
    assert(memo_discovery_search(100)==ESP_ERR_INVALID_STATE);
    memo_discovery_stop(); assert(frees==0);
    fail_address_lock=false;
    assert(memo_discovery_init()==ESP_OK && initialisations==1);
    assert(memo_discovery_search(100)==ESP_OK && result_frees==1);
    memo_endpoint_t first,second;
    assert(memo_discovery_endpoint(&first) && !first.confirmed && strcmp(first.host,"127.0.0.1")==0);
    memo_discovery_confirm(&first);
    assert(memo_discovery_state()==MEMO_DISCOVERY_FOUND);
    assert(memo_discovery_search(100)==ESP_OK && memo_discovery_endpoint(&second));
    assert(first.generation!=second.generation && !second.confirmed);
    memo_discovery_invalidate(&first); /* Late failure from old request. */
    assert(memo_discovery_endpoint(&second));
    memo_discovery_confirm(&first);
    assert(memo_discovery_state()==MEMO_DISCOVERY_CANDIDATE);
    memo_discovery_confirm(&second);
    assert(memo_discovery_state()==MEMO_DISCOVERY_FOUND);
    memo_discovery_invalidate(&second);
    assert(!memo_discovery_endpoint(&first));
    query_failure=true;
    assert(memo_discovery_search(100)==ESP_ERR_NOT_FOUND && !memo_discovery_endpoint(&first));
    memo_discovery_stop();assert(frees==1);
    query_failure=false;
    assert(memo_discovery_search(100)==ESP_OK && initialisations==2 && allocations==2);
    puts("memo discovery query serialization, snapshots, verification and stale generations: PASS");
}
