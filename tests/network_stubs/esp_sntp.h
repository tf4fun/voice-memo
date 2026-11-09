#pragma once
#define ESP_SNTP_OPMODE_POLL 0
#define SNTP_SYNC_STATUS_COMPLETED 1
void esp_sntp_setoperatingmode(int mode);
void esp_sntp_setservername(int index,const char *name);
void esp_sntp_init(void);
int esp_sntp_get_sync_status(void);
