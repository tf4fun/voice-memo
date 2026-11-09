#pragma once
#include "FreeRTOS.h"
typedef void *TaskHandle_t;
int xTaskCreate(void (*fn)(void *), const char *name, unsigned stack, void *arg,
                unsigned priority, TaskHandle_t *out);
void vTaskDelay(unsigned ticks);
void vTaskDelete(TaskHandle_t task);
void vTaskSuspend(TaskHandle_t task);
typedef unsigned UBaseType_t;
typedef unsigned StackType_t;
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task);
