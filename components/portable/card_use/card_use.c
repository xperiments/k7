/*
 * card_use - see include/card_use.h.
 */
#include "card_use.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static SemaphoreHandle_t s_gate;  /* the count and the closed flag */
static SemaphoreHandle_t s_idle;  /* given when the last user leaves */
static int  s_users;
static bool s_closed;

card_use_result_t card_use_init(void)
{
    s_gate = xSemaphoreCreateMutex();
    s_idle = xSemaphoreCreateBinary();
    s_users = 0;
    s_closed = false;
    if (!s_gate || !s_idle) return CARD_USE_NO_MEM;
    return CARD_USE_OK;
}

bool card_use_begin(void)
{
    if (!s_gate) return true; /* before init: nothing to close yet */
    xSemaphoreTake(s_gate, portMAX_DELAY);
    bool ok = !s_closed;
    if (ok) s_users++;
    xSemaphoreGive(s_gate);
    return ok;
}

void card_use_end(void)
{
    if (!s_gate) return;
    xSemaphoreTake(s_gate, portMAX_DELAY);
    bool zero = false;
    if (s_users > 0 && --s_users == 0) zero = true;
    xSemaphoreGive(s_gate);
    if (zero) xSemaphoreGive(s_idle);
}

bool card_use_quiesce(uint32_t wait_ms)
{
    if (!s_gate) return true;
    /* A give from a previous close is not this close's. */
    while (xSemaphoreTake(s_idle, 0) == pdTRUE) { }
    xSemaphoreTake(s_gate, portMAX_DELAY);
    s_closed = true;
    bool zero = s_users == 0;
    xSemaphoreGive(s_gate);
    if (zero) return true;
    return xSemaphoreTake(s_idle, pdMS_TO_TICKS(wait_ms)) == pdTRUE;
}

void card_use_resume(void)
{
    if (!s_gate) return;
    xSemaphoreTake(s_gate, portMAX_DELAY);
    s_closed = false;
    xSemaphoreGive(s_gate);
}

int card_use_users(void)
{
    if (!s_gate) return 0;
    xSemaphoreTake(s_gate, portMAX_DELAY);
    int n = s_users;
    xSemaphoreGive(s_gate);
    return n;
}
