#pragma once
/*
 * The slice of ESP-IDF's NVS this project uses, for the host build: same names,
 * same return codes, in-memory storage. The firmware keeps its settings in flash
 * and the host keeps them until it exits, so the code that saves a preference is
 * compiled and exercised by the host build instead of being firmware-only.
 */
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t nvs_handle_t;

#define NVS_READONLY  0
#define NVS_READWRITE 1

#ifndef ESP_ERR_NVS_NOT_FOUND
#define ESP_ERR_NVS_NOT_FOUND 0x1102
#endif

esp_err_t nvs_open(const char *namespace_name, int open_mode, nvs_handle_t *out_handle);
void      nvs_close(nvs_handle_t handle);
esp_err_t nvs_commit(nvs_handle_t handle);
esp_err_t nvs_set_u8(nvs_handle_t handle, const char *key, uint8_t value);
esp_err_t nvs_get_u8(nvs_handle_t handle, const char *key, uint8_t *out_value);
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *value, size_t length);
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out_value, size_t *length);

#ifdef __cplusplus
}
#endif
