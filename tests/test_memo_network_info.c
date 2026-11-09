#include <assert.h>
#include <stdio.h>
#include "memo_network_info_store.h"
static memo_network_info_store_t store = MEMO_NETWORK_INFO_STORE_INIT;
static void *writer(void *arg)
{
    (void)arg;
    for (unsigned i = 0; i < 50000; ++i) {
        const char *ssid = i % 2 ? "abcdefghijklmnopqrstuvwxyz123456" : "短网";
        memo_network_info_ssid(&store, (const unsigned char *)ssid, strlen(ssid));
        memo_network_info_ip(&store, i % 2 ? "192.168.100.200" : "1.2.3.4");
    }
    return NULL;
}
int main(void)
{
    memo_network_info_t info;
    memo_network_info_ssid(&store, (const unsigned char *)"long-network", 12);
    memo_network_info_ip(&store, "192.168.100.200");
    memo_network_info_ssid(&store, (const unsigned char *)"new", 3);
    memo_network_info_copy(&store, &info);
    assert(!strcmp(info.ssid, "new") && !info.ip[0]);
    const unsigned char full[] = "abcdefghijklmnopqrstuvwxyz123456";
    memo_network_info_ssid(&store, full, 32);
    memo_network_info_copy(&store, &info);
    assert(strlen(info.ssid) == 32 && !memcmp(info.ssid, full, 32));
    memo_network_info_ssid(&store, NULL, 0);
    memo_network_info_copy(&store, &info);
    assert(!info.ssid[0]);
    pthread_t thread;
    assert(pthread_create(&thread, NULL, writer, NULL) == 0);
    for (unsigned i = 0; i < 50000; ++i) {
        memo_network_info_copy(&store, &info);
        assert(!info.ssid[0] || !strcmp(info.ssid, (const char *)full) || !strcmp(info.ssid, "短网"));
        assert(!info.ip[0] || !strcmp(info.ip, "192.168.100.200") || !strcmp(info.ip, "1.2.3.4"));
        /* Returned data is independent, including while another task writes. */
        memset(&info, 0x7f, sizeof(info));
    }
    assert(pthread_join(thread, NULL) == 0);
    memo_network_info_ip(&store, NULL);
    memo_network_info_copy(&store, &info);
    assert(!info.ip[0] && strlen(info.ssid) <= 32);
    puts("memo network snapshot tests passed");
}
