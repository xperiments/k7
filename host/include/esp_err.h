#pragma once
/*
 * Host stand-in for ESP-IDF's esp_err.h.
 *
 * Only the host build sees this file: it lives in host/include, which is on the
 * include path for the host target and never for the firmware. No production
 * component contains target conditional compilation.
 */
#include <stdio.h>
#include <stdlib.h>

typedef int esp_err_t;

#define ESP_OK 0
#define ESP_FAIL (-1)
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_ERR_NOT_SUPPORTED 0x106

const char *esp_err_to_name(esp_err_t code);

#define ESP_ERROR_CHECK(x)                                                                    \
    do {                                                                                      \
        esp_err_t err_rc_ = (x);                                                              \
        if (err_rc_ != ESP_OK) {                                                              \
            fprintf(stderr, "ESP_ERROR_CHECK failed: %s (0x%x) at %s:%d\n",                   \
                    esp_err_to_name(err_rc_), err_rc_, __FILE__, __LINE__);                   \
            abort();                                                                          \
        }                                                                                     \
    } while (0)
