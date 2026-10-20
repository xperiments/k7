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
 * This component produces audio and owns the client list, the burst ring, the
 * per-client send rings and their sender tasks, and the pacing clock. It owns no
 * listening socket, no request parsing and no routing: it registers a route with
 * http_server, which is what calls back in. The audio task never writes a socket:
 * it queues framed bytes for a sender, so a slow listener cannot slow the stream.
 *
 * Features: pacing, burst-on-connect, ICY metadata (StreamTitle), track change, next
 * and previous.
 * Bandwidth: bitrate/8 bytes/s per client (32 KB/s at 256 kbps): fine for a LAN.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "audio_format.h" /* mi_fmt_t: the status struct reports a container */

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

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
    /* RMS of the chunk being encoded, 0..1 per channel: the display's level, not
     * the wire's. Written by the broadcast task per chunk and read without the
     * lock like elapsed_ms - a 32-bit float store is atomic on this core. */
    float    level_l;
    float    level_r;
} mini_icecast_status_t;

/* How far behind real time the broadcast is, in milliseconds, or 0 when it is
 * keeping up. Written by the pacing loop and read without the lock like the
 * status fields. A lower-priority consumer of the same microSD card - the
 * download path - uses it to back off when the stream is starving, instead of
 * taking the bus for a fixed slice whether it is needed or not. */
uint32_t  mini_icecast_lag_ms(void);

void      mini_icecast_status(mini_icecast_status_t *out);

/* Park the broadcast's card access: the station stops reading the SD and plays a
 * quiet noise bed instead, so a download can have the whole bus. Measured on this
 * board, a download sharing the card with the broadcast manages 55 KB/s and still
 * makes it miss deadlines; alone it gets 250 KB/s.
 *
 * The track is not skipped: when the park ends, playback continues from where it
 * stopped, so a listener hears the music resume mid-phrase after the break. The
 * station is a delayed stream, so nothing has to be seeked.
 *
 * max_ms bounds the park so a caller that never returns cannot leave the station
 * on the bed forever. Safe from any task. */
/* The library directory the station was started with. */
const char *mini_icecast_music_dir(void);

void      mini_icecast_park_card(uint32_t max_ms);
void      mini_icecast_unpark_card(void);
bool      mini_icecast_card_parked(void);

void      mini_icecast_skip(void);                              /* jump to next track */
void      mini_icecast_prev(void);                              /* jump to the previous track */
size_t    mini_icecast_now_playing(char *out, size_t out_size); /* current title, for the index */

#ifdef __cplusplus
}
#endif
