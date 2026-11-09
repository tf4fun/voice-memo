#pragma once
#include "FreeRTOS.h"
typedef void *SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateMutex(void);
SemaphoreHandle_t xSemaphoreCreateBinary(void);
void vSemaphoreDelete(SemaphoreHandle_t sem);
int xSemaphoreTake(SemaphoreHandle_t sem, unsigned ticks);
int xSemaphoreGive(SemaphoreHandle_t sem);
