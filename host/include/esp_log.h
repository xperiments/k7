#pragma once
/*
 * Host stand-in for ESP-IDF's esp_log.h. Messages go to stderr, so a host run
 * can be filtered by level with HOST_LOG_LEVEL=debug|info|warn|error.
 */
#include <stdio.h>

typedef enum {
    HOST_LOG_DEBUG = 0,
    HOST_LOG_INFO,
    HOST_LOG_WARN,
    HOST_LOG_ERROR,
} host_log_level_t;

void host_log(host_log_level_t level, const char *tag, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

#define ESP_LOGE(tag, fmt, ...) host_log(HOST_LOG_ERROR, tag, fmt, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) host_log(HOST_LOG_WARN, tag, fmt, ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) host_log(HOST_LOG_INFO, tag, fmt, ##__VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...) host_log(HOST_LOG_DEBUG, tag, fmt, ##__VA_ARGS__)
#define ESP_LOGV(tag, fmt, ...) host_log(HOST_LOG_DEBUG, tag, fmt, ##__VA_ARGS__)
