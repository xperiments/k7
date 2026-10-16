#pragma once
/* Host stand-in for ESP-IDF's esp_timer.h: microseconds from a monotonic clock. */
#include <stdint.h>

int64_t esp_timer_get_time(void);
