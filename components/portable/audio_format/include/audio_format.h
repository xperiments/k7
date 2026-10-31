#pragma once
/*
 * audio_format - the one authority on the source formats the server accepts.
 *
 * The decoder, the broadcast's library scan and the index all ask this function. It is only a name test: a leading dot (macOS AppleDouble
 * files) and everything else about a name is the caller's business. Keeping the
 * extension -> enum rule here means it cannot drift between surfaces; adding a
 * format still touches the decoder dispatch and the MIME map, but not the
 * acceptance rule. It also lets the library depend on the rule without depending
 * on the decoder.
 *
 * The `mi_`/`dec` prefixes predate this component. They are kept because the
 * status struct and the decoder API expose them; renaming them is a separate,
 * wider change.
 */
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MI_FMT_NONE = -1,
    MI_FMT_MP3 = 0,
    MI_FMT_FLAC,
    MI_FMT_WAV,
} mi_fmt_t;

/* Decided by extension, case-insensitively. NULL or a name without a dot is
 * MI_FMT_NONE. */
mi_fmt_t mi_dec_format_of(const char *name);

#ifdef __cplusplus
}
#endif
