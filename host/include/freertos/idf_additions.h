#pragma once
/*
 * Host stand-in for ESP-IDF's freertos/idf_additions.h.
 *
 * The firmware prefers external memory for large task stacks (the encoder's state
 * deliberately occupies internal RAM, so a 10 KB connection-task stack cannot
 * always be found there). The host has one memory class, so the capability flags
 * are accepted and ignored - every task gets a normal pthread stack.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

BaseType_t xTaskCreatePinnedToCoreWithCaps(TaskFunction_t fn, const char *name, uint32_t stack_bytes,
                                           void *arg, UBaseType_t priority, TaskHandle_t *out,
                                           int core_id, uint32_t caps);

/* The matching deletion call: on the target it is what frees a capability-
 * allocated stack, and the host shim needs the same shape. */
void vTaskDeleteWithCaps(TaskHandle_t task);
