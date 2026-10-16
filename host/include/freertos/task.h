#pragma once
/*
 * Host stand-in for FreeRTOS tasks: one pthread per task, the requested stack
 * size honoured, core pinning ignored (the host has cores to spare and no
 * ISR-affinity problem to solve).
 */
#include "freertos/FreeRTOS.h"

typedef void *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);

/* FreeRTOS's stack unit. On Xtensa it is one byte, which is why
 * uxTaskGetStackHighWaterMark()'s return value reads as bytes there. */
typedef uint8_t StackType_t;

BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char *name, uint32_t stack_bytes,
                                   void *arg, UBaseType_t priority, TaskHandle_t *out,
                                   int core_id);
void       vTaskDelay(TickType_t ticks);
void       vTaskDelete(TaskHandle_t task);
TickType_t xTaskGetTickCount(void);

/* Stack left unused, in StackType_t units. The host shim cannot measure a pthread
 * stack's high-water mark, so it reports a fixed, ample figure (see shim.c); the
 * device's own log is where this number means something. */
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task);
