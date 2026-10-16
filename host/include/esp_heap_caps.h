#pragma once
/*
 * Host stand-in for ESP-IDF's esp_heap_caps.h.
 *
 * The capability flags are accepted and ignored: every allocation is a plain
 * malloc, so the PSRAM-preferred path always succeeds on the host and the
 * "which path did this take" logging is exercised rather than skipped.
 *
 * The free-memory figures are synthetic (see shim.c) - they exist so the
 * telemetry line has the same shape as on the device, not to be believed.
 */
#include <stddef.h>
#include <stdint.h>

#define MALLOC_CAP_8BIT (1 << 2)
#define MALLOC_CAP_SPIRAM (1 << 10)
#define MALLOC_CAP_INTERNAL (1 << 11)
#define MALLOC_CAP_DEFAULT (1 << 12)

void  *heap_caps_malloc(size_t size, uint32_t caps);
void  *heap_caps_calloc(size_t n, size_t size, uint32_t caps);
void   heap_caps_free(void *p);
size_t heap_caps_get_free_size(uint32_t caps);
/* The largest single free block, which on the target is the number that decides
 * whether a contiguous request such as Wi-Fi's receive buffers can be met at all.
 * The host has no such constraint, so its shim reports the free total: the point of
 * the host tier is behaviour, not the target's memory map. */
size_t heap_caps_get_largest_free_block(uint32_t caps);
