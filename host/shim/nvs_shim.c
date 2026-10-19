#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "nvs.h"

typedef struct {
    char    key[16];
    uint8_t bytes[16];
    size_t  len;
    bool    used;
} entry_t;

static entry_t s_keys[32];

esp_err_t nvs_open(const char *namespace_name, int open_mode, nvs_handle_t *out_handle)
{
    (void)namespace_name; (void)open_mode;
    if (!out_handle) return ESP_ERR_INVALID_ARG;
    *out_handle = 1;
    return ESP_OK;
}

void nvs_close(nvs_handle_t h) { (void)h; }
esp_err_t nvs_commit(nvs_handle_t h) { (void)h; return ESP_OK; }

static entry_t *find(const char *key, bool create)
{
    for (int i = 0; i < 32; i++)
        if (s_keys[i].used && strcmp(s_keys[i].key, key) == 0) return &s_keys[i];
    if (!create) return NULL;
    for (int i = 0; i < 32; i++)
        if (!s_keys[i].used) {
            s_keys[i].used = true;
            snprintf(s_keys[i].key, sizeof s_keys[i].key, "%s", key);
            s_keys[i].len = 0;
            return &s_keys[i];
        }
    return NULL;
}

esp_err_t nvs_set_u8(nvs_handle_t h, const char *key, uint8_t value)
{
    (void)h;
    entry_t *e = find(key, true);
    if (!e) return ESP_ERR_NO_MEM;
    e->bytes[0] = value; e->len = 1;
    return ESP_OK;
}

esp_err_t nvs_get_u8(nvs_handle_t h, const char *key, uint8_t *out)
{
    (void)h;
    entry_t *e = find(key, false);
    if (!e || !out || e->len != 1) return ESP_ERR_NVS_NOT_FOUND;
    *out = e->bytes[0];
    return ESP_OK;
}

esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *value, size_t length)
{
    (void)h;
    if (!value || length > sizeof s_keys[0].bytes) return ESP_ERR_INVALID_SIZE;
    entry_t *e = find(key, true);
    if (!e) return ESP_ERR_NO_MEM;
    memcpy(e->bytes, value, length); e->len = length;
    return ESP_OK;
}

esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *out, size_t *length)
{
    (void)h;
    entry_t *e = find(key, false);
    if (!e || !out || !length) return ESP_ERR_NVS_NOT_FOUND;
    if (*length < e->len) return ESP_ERR_INVALID_SIZE;
    memcpy(out, e->bytes, e->len); *length = e->len;
    return ESP_OK;
}
