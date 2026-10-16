#pragma once
/*
 * mini_icecast - the broadcast: PCM in, one continuous MP3 stream out, fanned
 * out to every connected client.
 *
 * Every track (.mp3 / .flac / .wav, any sample rate, mono or stereo) is decoded
 * to PCM, resampled to ONE station format (stereo, 16-bit, out_hz), encoded to
 * MP3 at one bit rate and broadcast in real time. Because the station format
 * never changes, track changes are seamless for every player.
 *
 * This component produces audio and owns the client list, the burst ring and the
 * pacing clock. It owns no listening socket, no request parsing and no routing:
 * it registers a route with http_server, which is what calls back in.
 *
 * Features: pacing, burst-on-connect, ICY metadata (StreamTitle), track change, skip.
 * Bandwidth: bitrate/8 bytes/s per client (32 KB/s at 256 kbps): fine for a LAN.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mi_decode.h"   /* mi_fmt_t: the status struct reports a container */

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Called for every output chunk (MINI_ICECAST_CHUNK_FRAMES stereo frames, int16,
 * out_hz), as that chunk is handed to the encoder. It carries the same PCM
 * *content* that goes on the wire, but as unencoded 1024-frame chunks, up to one
 * encoder frame (26 ms at 44.1 kHz, 72 ms at 8 kHz) ahead of it. Same task as the
 * pacing loop: keep it fast or copy the data out (e.g. into a ring buffer for an
 * FFT task). It also runs ~lead_ms ahead of what a remote client hears. */
typedef void (*mini_icecast_pcm_cb_t)(const int16_t *stereo, size_t frames, void *ctx);

#define MINI_ICECAST_CHUNK_FRAMES 1024
#define MINI_ICECAST_MAX_CLIENTS 4  /* the client table size; max_clients must fit in it */
#define MINI_ICECAST_BITRATE_DEFAULT 128 /* kbps */
/* Encoded bytes between metadata blocks. A byte count rather than a time, so
 * what it means in seconds depends on the station bit rate: 16000 B is 1.00 s at
 * 128 kbps and 0.50 s at 256 kbps. */
#define MINI_ICECAST_METAINT_DEFAULT 16000

typedef struct {
    const char *music_dir;     /* e.g. "/sdcard/music": any mix of .mp3 .flac .wav, any sample rate */
    const char *station_name;  /* icy-name, default "ESP32 Radio" */
    uint16_t    port;          /* default 8000; used in the startup log */
    /* 1..MINI_ICECAST_MAX_CLIENTS. Every field here treats 0 as "leave it at the
     * documented default", so 0 is 2 clients; any other value outside the range
     * is refused with a diagnostic rather than coerced (defect D3). */
    uint8_t     max_clients;
    uint32_t    out_hz;        /* station sample rate, default 44100. Must be a rate the encoder accepts */
    uint32_t    bitrate;       /* station bit rate in kbps, default 128. Must be valid with out_hz */
    uint32_t    metaint;       /* bytes of audio between ICY metadata blocks, default 16000 */
    uint32_t    lead_ms;       /* how far ahead of real time we send, default 1500 */
    uint32_t    burst_ms;      /* history sent on connect, default 500, in external memory if present */
    mini_icecast_pcm_cb_t pcm_cb;
    void       *pcm_cb_ctx;
} mini_icecast_cfg_t;

/* Check a configuration without starting anything.
 *
 * Validation is arithmetic: it allocates nothing and brings nothing up. That
 * matters because the encoder's state is about 100 KB of internal RAM, and internal
 * RAM is also where Wi-Fi gets its DMA-capable buffers - measured, allocating the
 * first before the second makes esp_wifi_init() fail outright ("wifi:malloc buffer
 * fail", "Expected to init 16 rx buffer, actual is 14"), after which the driver's
 * own cleanup aborts and the device reboots in a loop. So an application should
 * validate with this, bring the network up, and only then start the broadcast.
 * Returns ESP_OK, or ESP_ERR_INVALID_ARG with `err` filled in. */
esp_err_t mini_icecast_check_station(const mini_icecast_cfg_t *cfg, char *err, size_t err_size);

/* Validate the station format, allocate the encoder and burst ring, start the
 * broadcast and telemetry tasks, and register the "/stream" route with
 * http_server. Call http_server_start() afterwards. Returns ESP_ERR_INVALID_ARG,
 * naming the offending value in the log, for a format or client limit that cannot
 * be served. Call it after the network is up: see mini_icecast_check_station(). */
esp_err_t mini_icecast_start(const mini_icecast_cfg_t *cfg);

/* What the broadcast is playing, for a display.
 *
 * A snapshot: every string is copied out, so the caller holds no lock and the values
 * cannot change underneath it. Fields are zero (and `format` is MI_FMT_NONE) when
 * nothing is known yet - no card, nothing playable, or a track change in progress.
 *
 * `duration_ms` is 0 when the length is unknown; `elapsed_ms` counts the audio
 * broadcast from the current track, so it is what a progress bar should show. For MP3
 * the file's own length includes the encoder delay and padding the format carries, so
 * a track can finish a few tens of milliseconds before the bar reaches the end. */
#define MINI_ICECAST_STATUS_TITLE_MAX 128
#define MINI_ICECAST_STATUS_FILE_MAX  160

typedef struct {
    char     title[MINI_ICECAST_STATUS_TITLE_MAX]; /* as sent in ICY metadata */
    char     file[MINI_ICECAST_STATUS_FILE_MAX];   /* the file's name, extension included */
    mi_fmt_t format;                               /* MI_FMT_NONE when nothing is playing */
    uint32_t source_hz;                            /* the file's own sample rate */
    uint32_t out_hz;                               /* the station rate */
    uint32_t bitrate;                              /* the station bit rate, kbps */
    uint32_t elapsed_ms;                           /* broadcast from this track so far */
    uint32_t duration_ms;                          /* this track's length, 0 = unknown */
    uint32_t library_count;                        /* playable files found on the card */
} mini_icecast_status_t;

void      mini_icecast_status(mini_icecast_status_t *out);

void      mini_icecast_skip(void);                              /* jump to next track */
size_t    mini_icecast_now_playing(char *out, size_t out_size); /* current title, for the index */

/* What mini_icecast_register_client() did with a connection. */
typedef enum {
    MINI_ICECAST_REGISTERED = 0,  /* the fd is on the air and outlives the request */
    MINI_ICECAST_REGISTER_FULL,   /* every client slot is taken: answer 503 */
    MINI_ICECAST_REGISTER_FAILED, /* headers or burst could not be sent: just close */
} mini_icecast_register_result_t;

/* Put an already-accepted connection on the air: write the stream headers, then
 * register it and send the burst under the broadcast lock. The caller must not
 * close the fd afterwards when this returns MINI_ICECAST_REGISTERED. Called by
 * the "/stream" route; applications do not normally call this. */
mini_icecast_register_result_t mini_icecast_register_client(int fd, bool want_icy_metadata);

#ifdef __cplusplus
}
#endif
