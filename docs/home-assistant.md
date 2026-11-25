# Home Assistant

A K7 can sit on a Home Assistant dashboard as a **media player** — play, next, previous — with
no broker, no token, no cloud account and no custom integration. The entity is one `rest` sensor,
three `rest_command`s and one `template` media player, in the file you already edit.

It needs **Home Assistant 2026.11 or later** (the `template` integration's `media_player` platform
is that new) and a K7 whose firmware has `POST /api/next` and `POST /api/prev` (HA-01). A firmware
without them answers `405`, and a pressed Next does nothing: check before you blame the YAML.

## The deck's address

The deck answers at **its own name**: `k7-<word>.local` — `k7.local` until someone names it on the
setup page (`Settings → Wi-Fi → Setup` on the panel, then *This deck's name*). So:

1. `k7-kitchen.local` — the name, which survives a new DHCP lease.
2. `k7.local` — still answers while a name is set; use it if the name does not resolve.
3. The literal address — what your router says, or the setup page's state. The last resort for a
   network whose mDNS does not work.

Every URL below is the first; substitute another if yours needs it.

## The page

Paste this into `configuration.yaml` (or a package), replace `k7-kitchen.local`, and restart Home
Assistant. **Paste the deck's `id`** into `unique_id`: read it once with

```sh
curl -s http://k7-kitchen.local/api/status | python3 -c 'import json,sys; print(json.load(sys.stdin)["id"])'
```

That is the deck's station MAC — the same on every boot, never repeated across units, and it
survives a re-flash of the settings — so the entity keeps its identity when the name or the lease
changes.

```yaml
# The deck as a media player. One status sensor, three commands, one template entity.
rest:
  - resource: http://k7-kitchen.local/api/status
    scan_interval: 5
    timeout: 5
    sensor:
      - name: K7 status
        unique_id: k7_status
        value_template: "{{ value_json.playing.title }}"
        json_attributes:
          - id
          - card
          - mark
          - tracks
          - generation
          - library
          - rescanning
          - order
          - playlist
          - playing
          - listeners

rest_command:
  k7_play:
    url: http://k7-kitchen.local/api/play
    method: post
    content_type: application/json
    payload: '{"path": "{{ path }}"}'
  k7_next:
    url: http://k7-kitchen.local/api/next
    method: post
    content_type: application/json
    payload: "{}"
  k7_prev:
    url: http://k7-kitchen.local/api/prev
    method: post
    content_type: application/json
    payload: "{}"

template:
  - media_player:
      - name: K7
        # The deck's own id, pasted from `/api/status`: unique to the unit, stable across
        # renames and leases. Left as `k7_<id>` it cannot collide with a second deck.
        unique_id: k7_28372f87e4d8
        device_class: speaker
        state: >-
          {% if states('sensor.k7_status') in ['unknown', 'unavailable'] %}idle
          {% else %}playing{% endif %}
        media_title: >-
          {{ state_attr('sensor.k7_status', 'playing').title
             if state_attr('sensor.k7_status', 'playing') else none }}
        media_content_id: >-
          {{ state_attr('sensor.k7_status', 'playing').path
             if state_attr('sensor.k7_status', 'playing') else none }}
        media_content_type: music
        media_duration: >-
          {{ (state_attr('sensor.k7_status', 'playing').duration_ms / 1000) | int(0)
             if state_attr('sensor.k7_status', 'playing') else 0 }}
        media_position: >-
          {{ (state_attr('sensor.k7_status', 'playing').elapsed_ms / 1000) | int(0)
             if state_attr('sensor.k7_status', 'playing') else 0 }}
        media_play:
          - action: rest_command.k7_play
            data:
              path: "{{ media_content_id }}"
        media_next_track:
          - action: rest_command.k7_next
        media_previous_track:
          - action: rest_command.k7_prev
```

## What the player offers, and what it refuses

Play, next and previous. **No pause, seek, stop, volume or power** — the deck has no transport to
pause (it is a broadcast, always walking), no seek (the encoder is live), no stop and no volume
(the amplifier is the speaker's). The template platform renders only the actions you define, so
the card shows three buttons and no lies. Adding a `media_pause` that calls nothing would be worse
than the missing button.

The position is the **track's** — `elapsed_ms` of the song — not a position in the broadcast, which
has none. The title is the track the broadcast is carrying; the artist and album are not in the
status document, so the card shows the title alone.

`media_play` sends the path the person chose. Pressing Play with nothing chosen sends an empty
path and the deck answers `400`; it does not invent a track.

## No artwork, on purpose

Covers on a K7 are an **archive on the card**, keyed by path, not a per-cover URL: there is no
endpoint that answers `GET /api/cover?path=…`. The template player has `media_image_url`, but
pointing it at nothing is a blank tile; the page leaves it out and says why. HA's own media
browser is not wired either: the deck publishes no media-source protocol.

## Why not MQTT

**Home Assistant has no MQTT `media_player` platform at all** — the integration's platform table has
no media-player row. Adding an MQTT client to the K7 would be the largest firmware change on the
list and would buy every entity *except* the one people want. The recipe above needs no broker.

## The stream, if you want sound in a room

`GET /stream` is a plain HTTP live MP3 body. Home Assistant can hand it to a Cast or Sonos device
with `media_player.play_media` (`media_content_type: music`, the stream URL as
`media_content_id`) — Chromecast needs the literal IP, it ignores mDNS. The deck has **one**
listener slot, so one room directly, or Music Assistant in front of it to fan one pull out to
several. That is a different job from this page, and
[`research/home-assistant-feasibility.md`](research/home-assistant-feasibility.md) works it out.

## What to check

1. **Restart, then look for `media_player.k7`.** Its state is `playing`, its title is the track the
   deck is carrying, and its `device_class` is a speaker.
2. **Press Next and Previous.** The broadcast steps at the next chunk boundary (about a second) and
   the card's title changes on the next poll.
3. **Play a track** (from Developer Tools → Actions → `media_player.play_media` with the deck's
   path, or the media browser if you wired one): the deck switches to it.
4. **Rename the deck** on the setup page and reboot it: the entity keeps its id (the `unique_id`),
   and only the URL in the YAML needs the new name.
5. **Unplug the deck:** the sensor goes `unavailable` within the timeout and the card reads `idle`
   rather than a stale title.

Two decks on one LAN are two blocks, with their own name, `id` and `unique_id`.
