/*
 * audio_format - the one authority on the source formats the server accepts.
 * See audio_format.h for the contract.
 */
#include "audio_format.h"

#include <string.h>
#include <strings.h>

mi_fmt_t mi_dec_format_of(const char *name)
{
    if (!name) return MI_FMT_NONE;
    const char *ext = strrchr(name, '.');
    if (!ext) return MI_FMT_NONE;
    if (!strcasecmp(ext, ".mp3")) return MI_FMT_MP3;
    if (!strcasecmp(ext, ".flac")) return MI_FMT_FLAC;
    if (!strcasecmp(ext, ".wav")) return MI_FMT_WAV;
    return MI_FMT_NONE;
}
