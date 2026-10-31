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
#include "library.h"       /* the immutable Playlist the broadcast consumes */

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MINI_ICECAST_CHUNK_FRAMES 1024

/* The highest source sample rate the broadcast puts on air. Above it the card and
 * decoder cost more than this board can carry at the Station rate — a 24-bit 192 kHz
 * FLAC needs ~700 KB/s and ~0.9 of a core before the encoder — so the Track is skipped
 * with a log instead of starving every listener. 96 kHz keeps 88.2/96 kHz hi-res;
 * 176.4/192 kHz files must be transcoded on the card (the index still lists them). Override at build time to widen or narrow the budget. */
#ifndef MINI_ICECAST_MAX_SOURCE_HZ
#define MINI_ICECAST_MAX_SOURCE_HZ 96000
#endif

static inline bool mini_icecast_source_rate_ok(uint32_t hz)
{
    return hz > 0 && hz <= MINI_ICECAST_MAX_SOURCE_HZ;
}
#define MINI_ICECAST_MAX_CLIENTS 4  /* the client table size; max_clients must fit in it */
#define MINI_ICECAST_BITRATE_DEFAULT 128 /* kbps */
/* Encoded bytes between metadata blocks. A byte count rather than a time, so
 * what it means in seconds depends on the station bit rate: 16000 B is 1.00 s at
 * 128 kbps and 0.50 s at 256 kbps. */
#define MINI_ICECAST_METAINT_DEFAULT 16000

typedef struct {
    /* The Playlist to broadcast; NULL means idle/no card. The caller keeps the
     * generation alive for as long as the broadcast may use it (ticket 11 adds
     * the lease). */
    const library_t *library;
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
    char     path[LIB_PATH_MAX];                   /* the Track's relative path, "" when idle */
    mi_fmt_t format;                               /* MI_FMT_NONE when nothing is playing */
    uint32_t source_hz;                            /* the file's own sample rate */
    uint32_t out_hz;                               /* the station rate */
    uint32_t bitrate;                              /* the station bit rate, kbps */
    uint32_t elapsed_ms;                           /* broadcast from this track so far */
    uint32_t duration_ms;                          /* this track's length, 0 = unknown */
    uint32_t library_count;                        /* Tracks in the Library generation */
    uint32_t generation;                           /* the generation the Playlist came from */
    uint32_t track_index;                          /* position in the Playlist, 0-based; 0 when idle */
    /* RMS of the chunk being encoded, 0..1 per channel: the display's level, not
     * the wire's. Written by the broadcast task per chunk and read without the
     * lock like elapsed_ms - a 32-bit float store is atomic on this core. */
    float    level_l;
    float    level_r;
} mini_icecast_status_t;

/* How far behind real time the broadcast is, in milliseconds, or 0 when it is
 * keeping up. Written by the pacing loop and read without the lock like the
 * status fields. A lower-priority consumer of the same microSD card - the
 * display's artwork loader - uses it to back off when the stream is starving,
 * instead of taking the bus for a fixed slice whether it is needed or not. */
uint32_t  mini_icecast_lag_ms(void);

void      mini_icecast_status(mini_icecast_status_t *out);

/* Park the broadcast's card access: the station stops reading the SD and plays a
 * quiet noise bed instead, so a rescan can have the whole bus. Parking is also
 * how the scan avoids the IDF 5.4 VFS readdir/stat cache race.
 *
 * The track is not skipped: when the park ends, playback continues from where it
 * stopped, so a listener hears the music resume mid-phrase after the break. The
 * station is a delayed stream, so nothing has to be seeked.
 *
 * max_ms bounds the park so a caller that never returns cannot leave the station
 * on the bed forever. Safe from any task. */
void      mini_icecast_park_card(uint32_t max_ms);
void      mini_icecast_unpark_card(void);
bool      mini_icecast_card_parked(void);

/* Replace the Playlist generation. The caller keeps `lib` alive until the next
 * call or shutdown; the broadcast resets its history and starts the new
 * generation at its first Track. NULL idles. */
void      mini_icecast_set_library(const library_t *lib);

/* Play a Track by its relative path at the next chunk boundary, like skip.
 * Returns false when the path is unknown or the Library has no generation; the
 * current track then keeps playing. */
bool      mini_icecast_play(const char *relpath);

void      mini_icecast_skip(void);                              /* jump to next track */
void      mini_icecast_prev(void);                              /* jump to the previous track */

/* Shuffle: a mode of the broadcast, not an action
 * (docs/adr/0023-shuffle-is-a-broadcast-mode.md).
 *
 * One permutation of the whole Playlist, walked forward by the natural end of a Track
 * and the next command and back by the previous one; the end of a lap clears the
 * history, as the linear Playlist's lap does. A pick (mini_icecast_play) moves the
 * order's cursor, so the next advance continues from it without rebuilding. A new
 * Library generation builds a new order with the mode still on. The mode lives until
 * reboot; there is no NVS entry.
 *
 * Seed it once at boot from the hardware RNG: the broadcast never calls esp_random()
 * itself, so the app gives the randomness and the host tests can pin the sequence.
 * Call the seed setter after mini_icecast_start() (or before it: the seed is kept for
 * the first order). */
void      mini_icecast_shuffle_seed(uint32_t seed);

/* Turn the mode on or off. `set(true)` only publishes the request - the broadcast
 * builds the order at the top of its next pass, off the UI thread, where it already
 * holds the Library lease - so mini_icecast_shuffle_enabled() reads On immediately
 * and the order catches up at the next Track boundary. `set(false)` frees the order
 * and returns to Playlist order at the same boundary. If the order cannot be
 * allocated the mode stays Off, the failure is logged, and playback continues in
 * Playlist order: the row cannot claim a mode that is not running. Safe from any
 * task; mini_icecast_shuffle_enabled() is what the Menu's row reads. */
void      mini_icecast_shuffle_set(bool on);
bool      mini_icecast_shuffle_enabled(void);

/* The screen's tone controls: three band targets (0 = low, 1 = mid, 2 = high) and the
 * trim, in dB. The ranges are the DJ ones: bands -26..+6, trim -12..+12. These only
 * publish a target; the broadcast task applies it at its next chunk, at its slew rate.
 * The trim's default is 0 dB, the same neutral as the bands. Safe to call from another
 * task. */
void      mini_icecast_eq_set(int band, float db);
void      mini_icecast_gain_set(float db);

size_t    mini_icecast_now_playing(char *out, size_t out_size); /* current title, for the index */

#ifdef __cplusplus
}
#endif
