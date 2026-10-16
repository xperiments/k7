#pragma once
/*
 * Host stand-in for FreeRTOS tasks: one pthread per task, the requested stack
 * size honoured, core pinning ignored (the host has cores to spare and no
 * ISR-affinity problem to solve).
 */
#include "freertos/FreeRTOS.h"

typedef void *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);

BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char *name, uint32_t stack_bytes,
                                   void *arg, UBaseType_t priority, TaskHandle_t *out,
                                   int core_id);
void       vTaskDelay(TickType_t ticks);
void       vTaskDelete(TaskHandle_t task);
TickType_t xTaskGetTickCount(void);
