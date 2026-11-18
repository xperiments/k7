#include "player_status.h"

#include <stddef.h>
#include <string.h>

void player_status_init(player_status_t *s)
{
    if (s) memset(s, 0, sizeof *s);
}

void player_status_set_network(player_status_t *s, bool usable)
{
    if (s) s->network = usable;
}

void player_status_set_stream(player_status_t *s, bool established)
{
    if (s) s->stream = established;
}

void player_status_set_audio(player_status_t *s, bool going_out)
{
    if (s) s->audio = going_out;
}

void player_status_set_fault(player_status_t *s, bool faulted)
{
    if (s) s->fault = faulted;
}

player_light_t player_status_light(const player_status_t *s)
{
    if (!s) return PLAYER_LIGHT_SEARCHING;
    if (s->fault) return PLAYER_LIGHT_FAULT;
    if (s->stream && s->audio) return PLAYER_LIGHT_PLAYING;
    if (s->network) return PLAYER_LIGHT_READY;
    return PLAYER_LIGHT_SEARCHING;
}

const char *player_light_name(player_light_t light)
{
    switch (light) {
    case PLAYER_LIGHT_READY: return "ready";
    case PLAYER_LIGHT_PLAYING: return "playing";
    case PLAYER_LIGHT_FAULT: return "fault";
    case PLAYER_LIGHT_SEARCHING: break;
    }
    return "searching";
}
