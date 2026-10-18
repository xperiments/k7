/*
 * LVGL's allocator, in PSRAM.
 *
 * LV_USE_CUSTOM_MALLOC means the backend is supplied here in full: nine symbols.
 * src/stdlib/clib/lv_mem_core_clib.c is the shape being replaced, so anything it
 * provides must exist here - including lv_mem_init().
 *
 * Every allocation prefers PSRAM (ADR 0004). The default heap_caps path prefers
 * internal memory below SPIRAM_MALLOC_ALWAYSINTERNAL and falls through to PSRAM only
 * when internal runs out - silently, spending the margin Wi-Fi needs for its
 * contiguous DMA buffers. This backend makes the placement a decision.
 */
#include <stdint.h>

#include "esp_heap_caps.h"
#include "lvgl.h"

#include "lvgl_mem.h"

static size_t lv_held;

size_t screen_display_lvgl_held(void) { return lv_held; }

void lv_mem_init(void) { }
void lv_mem_deinit(void) { }
lv_mem_pool_t lv_mem_add_pool(void *mem, size_t bytes) { (void)mem; (void)bytes; return NULL; }
void lv_mem_remove_pool(lv_mem_pool_t pool) { (void)pool; }

void *lv_malloc_core(size_t size)
{
    void *p = heap_caps_malloc_prefer(size, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                      MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (p) lv_held += heap_caps_get_allocated_size(p);
    return p;
}

void *lv_realloc_core(void *p, size_t new_size)
{
    size_t was = p ? heap_caps_get_allocated_size(p) : 0;
    void *q = heap_caps_realloc_prefer(p, new_size, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                       MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (q) lv_held = lv_held - was + heap_caps_get_allocated_size(q);
    return q;
}

void lv_free_core(void *p)
{
    if (!p) return;
    lv_held -= heap_caps_get_allocated_size(p);
    heap_caps_free(p);
}

void lv_mem_monitor_core(lv_mem_monitor_t *mon)
{
    multi_heap_info_t info;
    heap_caps_get_info(&info, MALLOC_CAP_SPIRAM);
    mon->total_size = info.total_free_bytes + info.total_allocated_bytes;
    mon->free_size = info.total_free_bytes;
    mon->free_biggest_size = info.largest_free_block;
    mon->free_cnt = info.free_blocks;
    mon->used_cnt = info.allocated_blocks;
    mon->max_used = info.total_allocated_bytes;
    size_t total = mon->total_size ? mon->total_size : 1;
    mon->used_pct = (uint8_t)(100 - (100 * mon->free_size) / total);
    mon->frag_pct = (uint8_t)(100 - (100 * mon->free_biggest_size) / (mon->free_size ? mon->free_size : 1));
}

lv_result_t lv_mem_test_core(void)
{
    void *p = lv_malloc_core(64);
    if (!p) return LV_RESULT_INVALID;
    lv_free_core(p);
    return LV_RESULT_OK;
}
