#pragma once
/* Host stand-in for FreeRTOS semaphores: mutexes and counting semaphores over
 * pthread primitives, with the same take/give contract. */
#include "freertos/FreeRTOS.h"

typedef void *SemaphoreHandle_t;

SemaphoreHandle_t xSemaphoreCreateMutex(void);
SemaphoreHandle_t xSemaphoreCreateBinary(void);
void              vSemaphoreDelete(SemaphoreHandle_t s);
SemaphoreHandle_t xSemaphoreCreateCounting(UBaseType_t max_count, UBaseType_t initial_count);
BaseType_t        xSemaphoreTake(SemaphoreHandle_t s, TickType_t ticks_to_wait);
BaseType_t        xSemaphoreGive(SemaphoreHandle_t s);
