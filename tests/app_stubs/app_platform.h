#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
typedef void *QueueHandle_t;
typedef void *TaskHandle_t;
typedef unsigned UBaseType_t;
#define pdTRUE 1
#define pdPASS 1
#define pdMS_TO_TICKS(ms) (ms)
#define portMAX_DELAY UINT32_MAX
QueueHandle_t xQueueCreate(unsigned count,unsigned size);
int xQueueSend(QueueHandle_t q,const void *event,unsigned ticks);
int xQueueReceive(QueueHandle_t q,void *event,unsigned ticks);
unsigned uxQueueMessagesWaiting(QueueHandle_t q);
int xTaskCreate(void (*entry)(void *),const char *name,unsigned stack,void *arg,unsigned priority,TaskHandle_t *out);
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task);
typedef void *esp_timer_handle_t;
typedef struct {void (*callback)(void *);const char *name;} esp_timer_create_args_t;
int64_t esp_timer_get_time(void);
esp_err_t esp_timer_create(const esp_timer_create_args_t *args,esp_timer_handle_t *out);
esp_err_t esp_timer_start_once(esp_timer_handle_t timer,uint64_t us);
esp_err_t esp_timer_stop(esp_timer_handle_t timer);
#define MALLOC_CAP_8BIT 1
size_t esp_get_free_heap_size(void);
size_t heap_caps_get_largest_free_block(unsigned caps);
void esp_fill_random(void *out,size_t len);
void esp_restart(void);
#define ESP_SLEEP_WAKEUP_GPIO 7
#define ESP_GPIO_WAKEUP_GPIO_LOW 0
uint32_t esp_sleep_get_wakeup_causes(void);
esp_err_t esp_deep_sleep_enable_gpio_wakeup(uint64_t mask,int mode);
void esp_deep_sleep_start(void);
typedef enum { BSP_BTN_UP,BSP_BTN_DOWN,BSP_BTN_OK } bsp_btn_t;
typedef enum { BSP_BTN_PRESS,BSP_BTN_CLICK,BSP_BTN_DOUBLE,BSP_BTN_LONG,BSP_BTN_RELEASE } bsp_btn_ev_t;
int bsp_button_read_mv(void);
esp_err_t bsp_button_init(void (*callback)(bsp_btn_t,bsp_btn_ev_t,void *),void *arg);
esp_err_t bsp_button_prepare_deep_sleep(int *level);
esp_err_t bsp_i2c_init(void);
esp_err_t bsp_i2c_prepare_deep_sleep(void);
esp_err_t bsp_audio_init(void);
esp_err_t bsp_audio_sleep(void);
esp_err_t bsp_audio_prepare_deep_sleep(void);
esp_err_t bsp_audio_set_format(uint32_t hz,uint8_t bits,uint8_t channels);
void bsp_audio_set_volume(uint8_t volume);
esp_err_t bsp_battery_init(void);
esp_err_t bsp_battery_sleep(void);
int bsp_battery_soc(void);
esp_err_t bsp_display_init(void);
void *bsp_lvgl_init(void);
bool bsp_lvgl_lock(int timeout);
void bsp_lvgl_unlock(void);
void bsp_display_backlight(uint8_t percent);
esp_err_t bsp_display_prepare_deep_sleep(void);
