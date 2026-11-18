#pragma once
#include <stddef.h>
#include <stdint.h>

/* One decoder API for .mp3 / .flac / .wav. Always yields stereo int16
 * (mono is duplicated, >2 channels keep the first two) at the file's own rate. */
#include <stdbool.h>
#include "audio_format.h" /* the source-format authority, shared with the file surface */

#define MI_READ_FRAMES 1024

typedef struct mi_dec mi_dec_t;

/* The card file's lifetime, set once by mini_icecast_start(): begin() is called before the
 * file is opened and end() after it is closed. NULL disables the gate. */
void      mi_dec_set_card_hooks(bool (*begin)(void), void (*end)(void));

/* Why the last mi_dec_open() returned NULL. One message for every failure is how a card that
 * could not be read came to be reported as a corrupt file (2026-11-18). */
typedef enum {
    MI_OPEN_OK = 0,
    MI_OPEN_FORMAT,  /* the extension names no format this decoder has */
    MI_OPEN_MEMORY,  /* the decoder's own state could not be allocated */
    MI_OPEN_CARD,    /* the card gate refused: no card, or it is being unmounted */
    MI_OPEN_FILE,    /* the file could not be opened on a card that is there */
    MI_OPEN_CODEC,   /* the container or the codec refused the bytes */
    MI_OPEN_SCRATCH, /* the native-channel scratch buffer could not be allocated */
} mi_dec_error_t;

mi_dec_error_t mi_dec_last_error(void);
const char *mi_dec_error_text(mi_dec_error_t error);

mi_dec_t *mi_dec_open(const char *path); /* NULL if unsupported extension / unreadable / corrupt */
uint32_t  mi_dec_hz(const mi_dec_t *d);

/* The track's length, or 0 when the decoder cannot know it (a truncated file, a
 * stream that never announced its length). For MP3 the figure includes the encoder
 * delay and padding the format carries, so it runs a few tens of milliseconds long -
 * the honest answer, since that is how much audio the file holds.
 *
 * An MP3 with no Xing/Info header is estimated from its frame length and bitrate: the
 * exact count costs a linear read of the whole file, and this is asked at the moment
 * the broadcast has nothing else to send. A VBR file that announces no length is 0,
 * not a guess. */
uint32_t  mi_dec_duration_ms(const mi_dec_t *d);
size_t    mi_dec_read(mi_dec_t *d, int16_t *stereo, size_t max_frames); /* <= MI_READ_FRAMES; 0 = end */
/* Wall time this decoder has spent blocked in fread() since it was opened. The
 * difference from the time spent in mi_dec_read() is the decoder's own CPU, which
 * is what a stage-cost log wants; the I/O share is the card's, not the codec's. */
uint64_t  mi_dec_io_us(const mi_dec_t *d);
void      mi_dec_close(mi_dec_t *d);
