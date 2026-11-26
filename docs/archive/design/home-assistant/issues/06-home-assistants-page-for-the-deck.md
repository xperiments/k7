# 06 — Home Assistant's page for the deck

**What to build:** a person can put their deck on a Home Assistant dashboard and press play, next and
previous — without a broker, a token, or a firmware change beyond the two routes above. The page is
the recipe: a `rest` sensor over the status document, a `rest_command` for play, and a `template`
`media_player` that declares only the controls the deck can honour. The page also says, in one
sentence, why there is no pause button.

**Blocked by:** 01 — The broadcast can be skipped by URL; 02 — The deck says which deck it is.

**Status:** ready-for-agent — HA-06 on the board (proposed).

- [ ] A page carries a complete, copy-pasteable configuration: the status sensor, the play command,
      and a `media_player` whose title, position and duration come from the status document and whose
      unique id comes from the board's id.
- [ ] The player offers play, next and previous, and offers **no** pause, seek, stop, volume or power
      — the broadcast has no transport and the deck has no volume, and an offered control that
      cannot work is a lie the dashboard repeats.
- [ ] It addresses the deck by its own name (`k7-<name>.local`), with the plain name and a literal
      address given as the fallbacks a person will need when mDNS does not answer.
- [ ] No artwork, and the page says why: covers are an archive on the device, not a per-cover URL.
- [ ] The README gains two sentences: what Home Assistant can do today with no firmware change, and
      that MQTT cannot give the deck a media player at all.
- [ ] The stale claim that Home Assistant cannot reach the device is corrected where it is written,
      pointing at the research note that settles it.
- [ ] Verified on a real Home Assistant instance: the entity appears, plays a chosen track, and the
      next and previous buttons move the stream.

**Not in this:** a custom integration, discovery over mDNS, or an MQTT bridge. Those are separate
decisions; this ticket is the recipe that needs none of them.

Spec: `docs/research/home-assistant-feasibility.md` §B2b is the draft this ticket makes true.
