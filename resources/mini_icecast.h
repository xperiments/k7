#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Called for every output chunk (MINI_ICECAST_CHUNK_FRAMES stereo frames, int16, out_hz),
 * right after it is broadcast. Same task as the pacing loop: keep it fast or copy the
 * data out (e.g. into a ring buffer for an FFT task). Note it runs ~lead_ms ahead of
 * what a remote listener hears. */
typedef void (*mini_icecast_pcm_cb_t)(const int16_t *stereo, size_t frames, void *ctx);

#define MINI_ICECAST_CHUNK_FRAMES 1024

typedef struct {
    const char *music_dir;     /* e.g. "/sdcard/music": any mix of .mp3 .flac .wav, any sample rate */
    const char *station_name;  /* icy-name */
    uint16_t    port;          /* default 8000 */
    uint8_t     max_clients;   /* 1..4, default 2 */
    uint32_t    out_hz;        /* station sample rate, default 44100. Everything is resampled to it */
    uint32_t    metaint;       /* bytes of audio between ICY metadata blocks, default 16000 */
    uint32_t    lead_ms;       /* how far ahead of real time we send, default 1500 */
    uint32_t    burst_ms;      /* history sent on connect, default 500 (88 KB of RAM at 44.1k; PSRAM if present) */
    mini_icecast_pcm_cb_t pcm_cb;
    void       *pcm_cb_ctx;
} mini_icecast_cfg_t;

esp_err_t mini_icecast_start(const mini_icecast_cfg_t *cfg);
void      mini_icecast_skip(void);                              /* jump to next track */
size_t    mini_icecast_now_playing(char *out, size_t out_size); /* current title, for your web UI */

#ifdef __cplusplus
}
#endif
