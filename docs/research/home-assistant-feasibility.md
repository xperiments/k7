# Can the K7 and the K7Player live in Home Assistant?

**Status:** research · **Updated:** 2026-11-25
**Conclusion:** The K7 works in HA today with no firmware change; MQTT cannot give it a media_player, and the K7Player cannot be addressed at all.

**Scope:** research only; no source changed. Written 2026-11-24 against `7aaa72a` (the working tree
also carries other sessions' edits, which this note does not read as evidence).

**The question:** how can this pair of boards be used from Home Assistant, or an architecture like
it, and what would it take to integrate them?

**Labels.** **VERIFIED** = read in the source that owns it, or in this repository at the cited line.
**EXTERNAL** = fetched from the cited URL, all on 2026-11-24 unless stated. **INFERENCE** = derived
from verified facts, with the derivation stated. **UNVERIFIED** = could not confirm, and why.

**Supersedes, in this tree only.** `service-surface-and-opportunities.md` (2026-10-30) and
`marketing/opportunities.md` (2026-10-30) both claim that Home Assistant's MCP client is "SSE-only"
and cannot reach this device, and that an HA integration is "absent". The first claim was already
false when written: HA has preferred **Streamable HTTP** since 2026.2 (PR
[#161547](https://github.com/home-assistant/core/pull/161547), merged 2026-01-25). The second is
true only in the narrow sense that the *device* speaks no HA protocol — it does not mean HA cannot
use it, which is what this note is about. Those two notes stay as they are; this one is the
correction.

---

## The answer in eleven lines

1. **HA can play what the K7 broadcasts today**, with no change to either board: `GET /stream` is a
   plain HTTP/1.0 live MP3 body, and HA hands the URL to a Cast or Sonos device. One room, no
   transport, no metadata.
2. **Better: Music Assistant** takes the URL once as a **Radio** item and fans it out — MA pulls
   once, decodes once, re-encodes per player. It is the only topology that respects the K7's
   **one listener** while reaching more than one room.
3. **HA can control and observe the K7 with no firmware change either**: `rest` sensors for
   `/api/status`, `rest_command` for `POST /api/play`, and — new enough to change the answer — a
   **`template` `media_player`** (HA **2026.11**, PR
   [#186934](https://github.com/home-assistant/core/pull/186934), merged 2026-10-27). Before
   2026-11-04 this was not possible without a custom integration or MQTT.
4. **And it must be an honest player**: `PLAY`, `NEXT_TRACK`, `PREVIOUS_TRACK` — no `PAUSE`, `SEEK`,
   `STOP`, `VOLUME_*`, `TURN_ON/OFF`. The broadcast has no transport and the device has no volume;
   the template platform renders only the controls you define, so honesty is the default.
5. **MQTT is the wrong vehicle, and this is the note's sharpest correction: Home Assistant has no
   MQTT `media_player` platform at all** (the integration's platform table has no media-player row —
   [mqtt docs](https://www.home-assistant.io/integrations/mqtt/)). Adding an MQTT
   client to the K7 would be the largest firmware change on the list and would buy every entity
   *except* the one people want.
6. **HA cannot address the K7Player at all** — not badly, not with a limitation. It receives audio
   only over ESP-NOW ([ADR 0105](../adr/0105-the-player-is-esp-now-only.md)), serves nothing, has no
   volume, no controls and no Bluetooth ([ADR 0108](../adr/0108-the-speaker-needs-a-second-radio-not-a-second-build.md)),
   and the K7 keeps no record of it ([ADR 0098](../adr/0098-the-player-is-a-client-the-k7-keeps-no-record-of.md)).
7. **Making it addressable is new player firmware, not an HA setting.** Snapcast is the cheapest
   route (two maintained classic-ESP32 ports, one of which names the player's own PCM5102A) at
   ~250 kbit/s–1.6 Mbit/s and 500–700 ms of buffer; AirPlay works but is heavy; ESPHome is the wrong
   shape and memory-starved; Sendspin needs PSRAM this board lacks.
8. **MCP connects, and buys almost nothing here.** HA tries Streamable HTTP first and needs no
   token, so the K7's stateless `POST /mcp` is exactly its shape — but the MCP client creates **no
   entities**: it hands tools to an LLM conversation agent. It is an assistant surface, not an
   integration.
9. **Matter and HomeKit are dead ends** for this pair: no Matter cluster carries audio (Speaker
   `0x0022` is On/Off + Level Control), HA's Matter integration ships no `media_player` platform,
   HomeKit's HAP is MFi-gated, and AirPlay is licensed separately.
10. **The real blockers are ours, and they are small**: one fixed hostname (`k7.local`, so two units
    collide), no stable device identity in `/api/status`, **no `next`/`prev` route**, no per-cover
    URL, and an address that three documents disagree about (host `k7.local` vs `xtape.local`, port
    **80** vs **8000**).
11. **So the work is one afternoon of YAML, or one small integration plus four small firmware
    additions.** No broker, no cloud, no new hardware, no account. The K7Player is a separate,
    bigger decision that no Home Assistant feature can make for it.

---

# Part A — what the two devices offer an automation system

## A1. The surfaces, against what a host needs

| Verb | K7 | K7Player | Consequence |
|---|---|---|---|
| Browse a library | `/api/library`, `/api/library.bin`, `/mcp` tools | — | only the K7 is addressable |
| Choose what plays | `POST /api/play`, `/api/playlists/select`, MCP control tools (opt-in build) | — | the K7 chooses; nobody else does |
| **Receive** audio | **`GET /stream` — the K7 is the origin** | ESP-NOW from the K7 only | **no host can push audio to either device** |
| Next / previous | `mini_icecast_skip/prev`, called only by the MCP control tools and the transport test — **no HTTP route** | — | a `media_player`'s next button has nothing to call |
| Volume, tone, shuffle, light | panel + knobs + Menu | none | a host gets *less* than the panel |
| On the network | `k7.local` + `_mcp._tcp` | joins Wi-Fi, serves nothing | one discoverable endpoint, one invisible |

Sources: `docs/specs/sd-stream-server.md` §6; `docs/specs/front-door.md` §2; the route registrations
in `components/portable/*/`; `docs/research/service-surface-and-opportunities.md` §A5. **[VERIFIED]**

## A2. The address, which is currently three addresses

| Source | Host | Port | Scheme |
|---|---|---|---|
| `components/app/appliance/appliance.c:42` (`#define HTTP_PORT 80`) and `:357` (`app_board_mdns_start("k7", …)`) | **`k7.local`** | **80** | plain |
| `docs/specs/sd-stream-server.md` M3 | `xtape.local` | 8000 | plain |
| `README.md` curl examples | literal IP | 8000 | plain |
| `components/portable/mcp/mcp.c:983` (origin check) | accepts `xtape.local`, `k7.local`, `localhost`, local IP | — | — |

**[VERIFIED]** The code wins: one plain listener, everything on it. The TLS/plain split in
`http_server.h:68-69` is a vestige — the plain dispatcher calls
`http_find_route(false /* filter_by_listener */, HTTP_LISTENER_PLAIN, …)`
(`components/portable/http_server/http_server.c:316`), so routes registered for either listener
answer on the plain one. So the honest URL is `http://k7.local/…` and **the spec's `:8000` is
stale**.

**[INFERENCE]** Anything written into an HA config, a README, or a bundle of YAML examples has to
pick one and say so. A discovery matcher written for `xtape.local` will never match a device
advertising `k7.local`.

## A3. What is missing, stated as absences

| Assumed by an integrator | Reality |
|---|---|
| Authentication, token, API key | None, by decision (`sd-stream-server.md` §7, A1–A2) |
| TLS | None in use; `tls/` is kept for a device that wants it again (ADR 0049) |
| Push / event stream / webhook | None. All state is polled; `/api/status` is `no-store` per request |
| Stable device identity | **None.** `/api/status` carries the *card's* `mark`, not the board's |
| Next / previous over HTTP | None (MCP control tools only, `XTAPE_MCP_CONTROL=1`) |
| Per-cover artwork URL | None: `/api/covers` is a cover *archive*, not `/cover?path=` |
| EQ / VU / strip / backlight / shuffle over HTTP | None: panel, Menu, and one MCP field (`player_status.level_l/right`) |
| A unique hostname per device | None: the mDNS name is the literal `k7` (`board_esp32.c`), so two units both answer `k7.local` |

The last one is a real bug for anyone with two units, not a theoretical one: `k7.local` resolves to
whichever responder wins, and a `rest` sensor can silently poll the *other* deck. **[INFERENCE from
the fixed name + RFC 6762 conflict resolution.]**

---

# Part B — the three relationships

## B1. K7 → HA: the broadcast as a source (works today)

`GET /stream` is `HTTP/1.0 200 OK`, `Content-Type: audio/mpeg`, `Connection: close`, no
`Content-Length`, no ranges, no chunking, endless body; `icy-metaint: 16000` only when the client
asks with `Icy-MetaData: 1` (`mini_icecast.c:439-444`). 128 kbps when the emitter is ESP-NOW and
**256 kbps when the emitter is set to HTTP** ([ADR 0106](../adr/0106-the-station-stops-and-starts-so-the-format-can-follow-the-transport.md)),
so the payload rate is 16 or 32 kB/s. **One listener**; the second gets `503` and its sentence
([ADR 0107](../adr/0107-the-k7-carries-one-listener.md)).

| Consumer | How | What it costs | Verdict |
|---|---|---|---|
| **Google Cast** | `media_player.play_media` with `media_content_type: music`, the URL as `media_content_id`. Cast explicitly supports MP3 net radio. | The Chromecast holds the K7's only slot | **works today**, but the URL must carry the **IP** — Chromecast ignores DHCP DNS and mDNS, so `k7.local` is a dead end ([cast docs](https://www.home-assistant.io/integrations/cast/)) |
| **Sonos** | Same action, direct HTTP/HTTPS URI | Sonos holds the slot | **works today**; live MP3 is implied by the docs, not named ([sonos docs](https://www.home-assistant.io/integrations/sonos/)) |
| **Music Assistant** | Add `http://<k7-ip>/stream` as a **Radio** item in the Builtin provider ([builtin](https://www.music-assistant.io/music-providers/builtin/)) | MA holds the slot, **permanently and by design** | **best topology**: MA pulls once, keeps a ~15 s rolling buffer, decodes once, re-encodes per player and serves that on its own port-8097 stream server ([streams README](https://github.com/music-assistant/server/blob/dev/music_assistant/controllers/streams/README.md)) |
| **Squeezebox / DLNA** | through LMS / DIDL | one slot, device-dependent | works, no reason to prefer it |

HA has **no generic URL player**: `play_media` dispatches to whichever integration owns the entity,
and the action docs say a URL goes "to Sonos and Cast, but only a playlist ID to iTunes"
([play_media docs](https://www.home-assistant.io/actions/media_player.play_media/)). And HA core has
**no `airplay` and no `raop` integration** — both manifests are 404 — so AirPlay targets appear only
through Music Assistant. **[EXTERNAL]**

**The one-listener problem is a product constraint, not a bandwidth one.** The broadcast is 128–256
kbps; the fix for "two rooms" is a pull-once proxy (MA, or Snapcast), never a bigger `max_clients` —
ADR 0107 spent ~480 KiB of PSRAM taking the slots away precisely because a second listener divides
one person's airtime.

## B2. HA → K7: control and observation (works today, almost)

### B2a. The honest entity model

| Entity | Source today | Notes |
|---|---|---|
| `media_player` (K7) | `/api/status` `playing.{title,path,elapsed_ms,duration_ms}`, `playlist.name`, `order` | `state`: playing/idle. Features: `PLAY`, `NEXT_TRACK`, `PREVIOUS_TRACK`. **Not** pause/seek/stop/volume/power |
| `sensor` × n | `tracks`, `generation`, `library`, `listeners.{used,slots}`, `card`, `mark` | `mark` is the *card's* name; useful, not an identity |
| `binary_sensor` | `card`, `rescanning`, reachability | `expire_after` is how you fake a dead device honestly |
| `button` | `POST /api/rescan` | exact fit |
| `select` | `playlists` (id/name) → `POST /api/playlists/select` | the list must be known when the payload is published |
| `number` / `light` / `switch` | — | EQ, strip, backlight and shuffle have **no HTTP surface**; each is a new route, not new HA plumbing |
| artwork (`entity_picture`) | — | needs `/api/cover?path=…`; the archive exists, a per-cover URL does not |

**[INFERENCE]** `media_position` is tolerable because `/api/status` reports per-track
`elapsed_ms`/`duration_ms`; it must never be presented as a position in the broadcast, because a
broadcast has none.

### B2b. Build 1 — zero firmware change (the recommendation)

`rest` creates **only** sensors and binary sensors ([rest docs](https://www.home-assistant.io/integrations/rest/));
`rest_command` creates actions ([rest_command docs](https://www.home-assistant.io/integrations/rest_command/));
the **template** integration's `media_player` platform (2026.11) supplies the entity
([template docs](https://www.home-assistant.io/integrations/template/#media-player)). Together they
are the whole integration, with no broker and no token:

```yaml
# configuration.yaml  — substitute :80 for :8000 if appliance.c's HTTP_PORT is what is flashed
rest:
  - resource: http://k7.local/api/status
    scan_interval: 5
    timeout: 5
    sensor:
      - name: K7 status
        unique_id: k7_status_raw
        value_template: "{{ value_json.playing.title }}"
        json_attributes: [playing, playlist, listeners, library, tracks, generation, card]

rest_command:
  k7_play:
    url: http://k7.local/api/play
    method: post
    content_type: application/json
    payload: '{"path": "{{ path }}"}'
  k7_playlist:
    url: http://k7.local/api/playlists/select
    method: post
    content_type: application/json
    payload: '{"id": "{{ id }}"}'
  k7_rescan:
    url: http://k7.local/api/rescan
    method: post
    content_type: application/json
    payload: "{}"

template:
  - media_player:
      - name: K7 broadcast
        unique_id: k7_broadcast
        device_class: speaker
        state: >-
          {% if states('sensor.k7_status') in ['unknown','unavailable'] %}idle{% else %}playing{% endif %}
        media_title: >-
          {{ state_attr('sensor.k7_status','playing').title
             if state_attr('sensor.k7_status','playing') else none }}
        media_content_id: >-
          {{ state_attr('sensor.k7_status','playing').path
             if state_attr('sensor.k7_status','playing') else none }}
        media_content_type: music
        media_duration: >-
          {{ (state_attr('sensor.k7_status','playing').duration_ms / 1000) | int
             if state_attr('sensor.k7_status','playing') else 0 }}
        media_position: >-
          {{ (state_attr('sensor.k7_status','playing').elapsed_ms / 1000) | int
             if state_attr('sensor.k7_status','playing') else 0 }}
        play_media:
          - action: rest_command.k7_play
            data: { path: "{{ media_content_id }}" }
```

The `/api/status` body it reads (`components/portable/front_door/front_door.c`): **[VERIFIED]**

```json
{"card":true,"mark":"MIXTAPE 7","tracks":64,"generation":7,"library":"ok","rescanning":false,
 "order":"list","playlist":{"id":"0123456789abcdef","name":"Late night"},
 "playing":{"title":"Blue Monday","path":"New Order/Substance/01.mp3",
            "elapsed_ms":61000,"duration_ms":450000},
 "listeners":{"used":1,"slots":1}}
```

**What it cannot do.** There is no `media_next_track` to bind: the next path would have to be
computed by walking `/api/library` in broadcast order, or read from `player_status.track_index` over
MCP. That is the one gap that makes the zero-code player feel broken, and it is **one route away**
(§D1). **[VERIFIED absence + INFERENCE]**

### B2c. Build 2 — a custom integration (when discovery and a device page matter)

A small HACS/Marketplace integration is ~300–600 lines and needs **no credentials** — HA documents
"discoverable integrations that require no authentication" as a first-class config-flow shape
([config flow docs](https://developers.home-assistant.io/docs/core/integration/config_flow)). The
manifest that fits:

```json
{
  "domain": "k7", "name": "K7", "version": "0.1.0",
  "documentation": "https://github.com/…", "issue_tracker": "https://github.com/…/issues",
  "codeowners": ["@…"], "config_flow": true, "integration_type": "device",
  "iot_class": "local_polling", "dependencies": ["http"], "requirements": [],
  "zeroconf": [{"type": "_mcp._tcp.local.", "properties": {"device_type": "mcp:device"}}]
}
```

- `local_polling` is the honest `iot_class`: HA polls `/api/status`; the device never pushes.
- `zeroconf` is the point: core claims no `_mcp._tcp.local.` service
  (`homeassistant/generated/zeroconf.py`), and a custom component's matchers are appended to the
  generated set (`loader.py`), so the K7 appears under **Discovered** with no IP typed. The
  `properties` filter keeps it from matching a future MCP server that is not a K7.
- Unique ID: the MAC (via the mDNS A record or a DHCP matcher), **not** the hostname — HA's docs
  rule hostnames and IPs out. This is why the firmware needs a stable id (§D1).
- A `DataUpdateCoordinator` over `GET /api/status` + `async_config_entry_first_refresh()` is the
  documented shape for one poll feeding every entity.

Packaging is the Marketplace (HACS is built in from 2026.11): one integration per repo, everything
under `custom_components/k7/`, `hacs.json`, brand assets. A custom integration is graded **Custom**,
not against the quality scale; the scale's real teeth (95 % coverage, codeowner, discovery → Gold)
only bite on a core submission.

### B2d. Build 3 — MQTT (why it is the worst of the three here)

**There is no MQTT `media_player` platform in Home Assistant.** The integration's own platform table
has no media-player row; `.../mqtt/media_player.py` does not exist on
`dev` nor at any sampled tag back to 0.40.0; and `…/integrations/media_player.mqtt/` is a 404.
**[EXTERNAL, five independent checks]**

So an MQTT client in the K7's firmware would buy `number`/`select`/`switch`/`button`/`sensor`/
`binary_sensor` entities, retained state, LWT availability and *push* — and still leave the
`media_player` to YAML or Python. It is also the largest firmware change on the list: a broker
dependency for the user, a discovery payload, retained state, LWT, and an event hook into a
firmware whose state is currently polled.

The device budgets say the same: with a listener on `/stream` the K7 has **47.9–51 KB internal free
and a 31.7 KB largest block** (`README.md`), while the encoder is already at 41–64 % of a core. A
plain MQTT client fits; MQTT over TLS would not. **[INFERENCE]**

**If MQTT is ever wanted, want it for the numbers** (EQ, VU, the light), which MQTT is genuinely
good at — not for the player entity it cannot make.

## B3. HA → K7Player: not today, and not with an HA setting

| Transport | Player needs | K7's role | Verdict |
|---|---|---|---|
| HTTP/ICY client | TCP client, ICY demux, URL provisioning, reconnect; and `max_clients` back to 2 | keeps serving | **needs firmware**; re-opens ADRs 0105/0107 |
| **Snapcast client** | protocol + FLAC/Opus decode + 500–700 ms buffer + I2S; **two maintained classic-ESP32 ports exist**, one naming the PCM5102A | nothing — the snapserver is the source | **cheapest**; ~250 kbit/s (Opus) to 1.6 Mbit/s (PCM) |
| AirPlay receiver | FairPlay v3 + SRP/Curve25519 + RSA/AES + PTP-grade timing + ALAC/AAC | nothing | possible, **heavy** on 85 KiB free |
| ESPHome `speaker` | ESPHome's own firmware, ~1 MB/pipeline default, PSRAM "strongly recommended" | nothing (HA is the origin) | **wrong shape**, memory-starved |
| MA Sendspin | 2 MB PSRAM + 4 MB flash | nothing | **not this hardware** |
| Bluetooth A2DP | — | — | **dead by silicon** (ADR 0108) |

Two numbers decide it. The player has **85 KB internal free and a 43 KB largest block** with the
stream up ([ADR 0102](../adr/0102-the-players-ring-is-96-kb-and-clears-itself-rather-than-dropping-frames.md)),
while a FLAC snapclient at 500 ms wants ~117 KiB free (and ~660–758 ms is its ceiling on this chip
class); and ESP-NOW and Wi-Fi **can** share the radio — ESP-NOW is a service of the Wi-Fi driver, and
Espressif's coexistence tables list ESP-NOW TX beside Wi-Fi station as supported — but the ESP-NOW
feed is only ~154 kbit/s, so Snapcast costs **6.5× to 10× the airtime** on a link already measured
stalling for 1–9 s at a time. **[EXTERNAL + ADR 0103]**

The honest designs are therefore *(a)* Snapcast **instead of** ESP-NOW, with an always-on snapserver
as the player's source and the K7 demoted to one input, or *(b)* keep ESP-NOW and give up on
receiving from HA. Both at once is unverified and, on these numbers, unlikely.

**[INFERENCE]** This is a decision for an ADR, not a feature: ADR 0105 chose one transport; making
the player addressable means reversing it, or shipping a second player variant.

---

# Part C — the "alike" architectures

## C1. MCP, both directions

**HA as MCP client → the K7's `/mcp`.** HA prefers Streamable HTTP and falls back to SSE only on a
404/405 of the initial POST; it needs no authentication unless the server answers 401, and its
client handles a missing session id, a JSON (not SSE) reply, and `405` on GET. The K7 does exactly
those things. **[EXTERNAL:** [mcp docs](https://www.home-assistant.io/integrations/mcp/),
`components/mcp/coordinator.py`, `config_flow.py`; **VERIFIED** in `mcp.c` for the device side.**]**
What it buys: an LLM conversation agent that can call `library_search` / `player_status` /
`player_play`. What it does not buy: **a single entity**. The component has no platform module at
all. Tools refresh every 30 minutes; each call opens a new client.

Two risks worth recording: the K7's **HTTP/1.0 + `Connection: close`** against HA's `httpx`-based
client is unproven on the board; and the K7 is a **legacy-era** server (`initialize`,
`2025-11-25`), which the 2026-07-28 revision retires — modern clients need `server/discover`, per-request
`_meta`, `Mcp-Method`/`Mcp-Name`, and `resultType`. That is an envelope branch, not a second server.
**[EXTERNAL: modelcontextprotocol.io 2025-11-25 and 2026-07-28]**

**HA as MCP server** (`/api/mcp`, OAuth, admin) inverts the direction and cannot help until the K7
is an entity — which per B2 means a `media_player`.

**`_mcp._tcp` is a local invention.** No mDNS/DNS-SD appears anywhere in the MCP docs index, IANA
has no `mcp` service name, and the standard's out-of-band answer is HTTP-hosted Server Cards
(SEP-2127). That is not fatal — DNS-SD types are cheap to invent, and mDNS is on HA's allowed
discovery list — but no generic MCP client will ever find the K7 by it. It becomes useful the
moment an HA integration declares it, which is exactly B2c.

## C2. Matter / Thread — no

Matter's `Speaker` device type (`0x0022`) is **On/Off + Level Control**: a volume knob with a name.
`Streaming Audio Player` (`0x0020`) and `Casting Audio Player` (`0x0021`) are Media Playback +
Content Launcher — a remote for the device's *own* playback, where the device still fetches the
content. **No cluster carries audio**, so Matter never means "HA streams to this box". HA's Matter
integration ships 18 platforms and no `media_player`. Commissioning needs the setup code plus **a
phone with Bluetooth**, production needs a DAC certificate, and esp-matter recommends ESP-IDF 6.x
against this project's 5.4.4. **[EXTERNAL: `connectedhomeip` device-type XML, HA Matter docs]**

## C3. HomeKit / AirPlay — no

HAP's specification is MFi-gated (no public download), HA's HomeKit Controller has **no
`media_player`**, and the audio path for a third-party speaker is AirPlay, licensed separately. The
open ESP32 HomeKit SDK has no speaker profile and commercial use needs the MFi variant; the older
ADK port is dormant since 2021. Three independent walls. **[EXTERNAL: developer.apple.com/homekit,
HA homekit_controller docs, Espressif SDK README]**

## C4. An HA app as a bridge — the zero-firmware alternative

An app (formerly add-on) can run on the host network so `k7.local` resolves, present a UI through
ingress with HA's auth in front, re-serve `/stream` (ingress proxies support streaming), and
announce the K7 to HA's MCP client with `POST /discovery {"service": …, "config": {"url": …}}`. It
is the cheapest way to get "discovered + authenticated + modern MCP" — and it can never create an
entity. Right bridge, wrong product. **[EXTERNAL: developers.home-assistant.io apps docs]**

## C5. The rest, in one line each

- **openHAB** — its HTTP binding is a direct `url` Thing over `/api/status` + `/api/play`: same work
  as HA's, a fraction of the audience.
- **Node-RED** — a scripting layer that needs the entity to exist first; it can call `/api/play` on a
  timer and nothing more.
- **ioBroker / Domoticz** — states over HTTP/JSON; no media abstraction survives a one-listener
  broadcast.
- **Music Assistant as a K7 *player*** — inverts who owns playback (MA drives players; the K7 is not
  one), so a K7 player provider is the wrong shape; MA as the K7's *consumer* (B1) is the right one.

## C6. What "integrated" means to an HA user, and the formal bar

The prior art is unanimous: an HA user means *an entity appears, is discovered without typing an
IP, and can be told to play something*. WiiM (LinkPlay HTTP + UPnP, `_linkplay._tcp`) is Platinum;
Sonos and Bluesound are Bronze; Volumio, Arylic and Squeezebox have their own integrations. **Every
one is a sink HA tells what to play.** A *source* device has no precedent — which is the structural
reason the K7 lands in "assistant" and "sensor" territory more naturally than "media_player".

The quality scale is 54 rules in four tiers: Bronze 20 (config flow, brands, tests, docs), Silver 10
(owner, ≥95 % coverage, reauth), Gold 21 (discovery — mDNS included — diagnostics, translations,
reconfigure), Platinum 3 (fully async/typed). Config flow and branding are Bronze with no exceptions;
a custom integration is graded **Custom** instead. **[EXTERNAL:
[quality scale](https://developers.home-assistant.io/docs/core/integration-quality-scale/)]**

---

# Part D — what we need to build

## D1. Device side (firmware), ordered by what it unblocks

| # | Change | Why | Size | Blocks |
|---|---|---|---|---|
| **1** | `POST /api/next`, `POST /api/prev` | `media_next_track`/`media_previous_track` have nothing to call; `mini_icecast_skip/prev` already exist and are called by the MCP control tools | one route registration each | **the honest player** |
| **2** | a stable id in `/api/status` (MAC-derived), plus `fw`, `board`, `rate`, `bitrate` | config-flow `unique_id`; HA refuses hostnames/IPs as identity; `player_status` already has most of these over MCP | small JSON + one read | **discovery** |
| **3** | `GET /api/cover?path=…` | `entity_picture`/`media_image_url`; the cover archive exists, a per-cover URL does not | one route over the archive | artwork |
| **4** | EQ, VU, strip, backlight, shuffle, emitter over HTTP (or MQTT) | the entities a host cannot see at all today: they live on the panel, the Menu, and one MCP field | each is a read path + a write path | extra entities |
| **5** | optional shared-secret header on `/api/play`, `/api/rescan`, `/mcp` | the device is the weak link, not HA (§D4); HA's convention is a config-entry field sent as a header | one check, one field | trust |
| **6** | modern MCP envelope (`server/discover`, `_meta`, `Mcp-Method`/`Mcp-Name`, `resultType`) | HA's client is legacy/dual-era today; the 2026-07-28 revision retires the legacy path | envelope branch | MCP's shelf life |
| **7** | a MAC-derived mDNS hostname | two units both answer `k7.local` today | one call | multi-unit homes |

Only **#1 and #2** stand between today's firmware and a genuinely good HA integration. #3 is what
makes it look like a music player rather than a text feed. #4 is optional. #5 is the only one that
is about trust rather than capability.

## D2. HA side — pick one

| Path | Lines | Gets | Cost |
|---|---|---|---|
| **YAML** (B2b) | ~40 | `media_player` + sensors + buttons, no discovery | hand-written, no device page, next/prev hollow until D1 |
| **Custom integration** (B2c) | ~300–600 | the same, plus discovery, device registry, diagnostics, options | a repo, packaging, upkeep |
| **App bridge** (C4) | a container | discovery + auth for MCP only; no entities | a second codebase, HAOS/Supervised only |
| **MQTT** (B2d) | firmware + broker | numbers/selects/switches, push, LWT — **no `media_player`** | the largest change, for the least |

## D3. Docs and address — one truth

Pick the host and the port, and make the spec, the README and the firmware agree: today they say
`k7.local:80`, `xtape.local:8000` and `192.168.x.x:8000` respectively. Then write the two sentences
into the README:

1. *"Home Assistant can control the K7 today with a `template` media player over its open HTTP API —
   no broker, no token, no firmware change — but it shows a play button and a next button, not a
   pause button, because the broadcast has no transport."*
2. *"MQTT cannot give the K7 a media player: Home Assistant has no MQTT `media_player` platform."*

## D4. Security, stated plainly

HA's own threat model is the household LAN and it does not require a local device to authenticate;
"nothing" is the norm (WLED has no credential field at all, ESPHome's Noise key is optional). The K7
is unusual only in that its unauthenticated endpoint controls *content*. The blast radius is
"someone else changes what plays in your room", denial of the one pair of ears, and a card rescan —
no credential to steal, no audio to exfiltrate beyond the library you already own. The proportionate
fix is network segmentation or D1 #5; Matter/HomeKit would buy commissioning keys at the price of a
certificate supply chain and a phone ceremony, for a deck.

---

# Part E — what I would do first

1. **Write the YAML recipe into the docs and let it be true.** It is the whole answer to "can I use
   my K7 in HA", it needs nothing, and it is honest about the missing transport.
2. **Add `next`/`prev` and a stable id to the firmware** (D1 #1–2). Two small changes turn a
   read-only card into a player an automation can drive.
3. **Then decide whether to ship a custom integration**, with `_mcp._tcp` discovery — the first
   consumer the service advertisement has ever had — or to keep it a documented YAML recipe and
   spend the effort on the player instead.
4. **Leave MQTT out.** It is not a media player in HA, it is the biggest firmware change, and it
   makes the user run a broker to get less.
5. **Keep the MCP entry, and correct the claim about it.** It is free, it is an assistant surface,
   and it becomes a real integration the day the K7 is an entity — not before.
6. **Treat "HA drives the K7Player" as a separate product decision.** Snapcast is the cheapest road
   if it is ever taken; it means a player that is no longer a client of the K7, which is a different
   device with a different story.
7. **Do not do Matter, HomeKit, ESPHome or Sendspin** for this pair: none of them carries audio to
   this hardware, and two of them cannot show a `media_player` in HA even if it did.

---

# Part F — what only the board can settle

1. **Does HA's MCP client complete a session against `POST /mcp`?** Every requirement is met on
   paper; the unknowns are HTTP/1.0 + `Connection: close` against `httpx`, and the `Origin`/`Host`
   guard when HA sends its own `Host`. One `curl`-and-logs session settles it.
2. **Does Cast accept `http://<ip>/stream` from this server** — HTTP/1.0, close-delimited, no
   `Content-Length`? The shape is exactly what radio clients are built against, but "MP3 net radio"
   in Google's docs is not a promise about this server's framing.
3. **Does a `template` `media_player` accept every key in B2b** as written? `availability` is
   documented as a UI option but not listed in the YAML key table.
4. **Two units on one LAN**: does anything in the K7's mDNS respond to a name conflict, and what does
   HA's discovery do with two identical `_mcp._tcp` instances?
5. **What a snapclient costs on this board alongside the existing ring** is unmeasured; the numbers
   in B3 are the port authors' own, on their own buffers, not on ours.

---

## Sources

**Home Assistant** — fetched 2026-11-24: [mqtt](https://www.home-assistant.io/integrations/mqtt/),
[rest](https://www.home-assistant.io/integrations/rest/),
[rest_command](https://www.home-assistant.io/integrations/rest_command/),
[template `media_player`](https://www.home-assistant.io/integrations/template/#media-player),
[play_media](https://www.home-assistant.io/actions/media_player.play_media/),
[cast](https://www.home-assistant.io/integrations/cast/), [sonos](https://www.home-assistant.io/integrations/sonos/),
[squeezebox](https://www.home-assistant.io/integrations/squeezebox/), [mcp](https://www.home-assistant.io/integrations/mcp/),
[mcp_server](https://www.home-assistant.io/integrations/mcp_server/), [matter](https://www.home-assistant.io/integrations/matter/),
[homekit_controller](https://www.home-assistant.io/integrations/homekit_controller/),
[music_assistant](https://www.home-assistant.io/integrations/music_assistant/),
[marketplace](https://www.home-assistant.io/integrations/marketplace/),
[2026.11 release](https://www.home-assistant.io/blog/2026/11/04/release-202611/).
Developer docs: [media player entity](https://developers.home-assistant.io/docs/core/entity/media-player/),
[manifest](https://developers.home-assistant.io/docs/creating_integration_manifest),
[config flow](https://developers.home-assistant.io/docs/core/integration/config_flow),
[coordinator](https://developers.home-assistant.io/docs/integration_fetching_data),
[quality scale](https://developers.home-assistant.io/docs/core/integration-quality-scale/),
[apps](https://developers.home-assistant.io/docs/apps).
Core source on `dev`: `components/{mqtt,mcp,matter,hassio,template,media_player}/`,
`loader.py`, `generated/zeroconf.py`; PRs [#186934](https://github.com/home-assistant/core/pull/186934)
(template media player, 2026-10-27) and [#161547](https://github.com/home-assistant/core/pull/161547)
(Streamable HTTP, 2026-01-25).

**Music Assistant** — [docs](https://www.music-assistant.io/), [builtin provider](https://www.music-assistant.io/music-providers/builtin/),
[player support](https://www.music-assistant.io/player-support/), [ESP32 FAQ](https://www.music-assistant.io/faq/stream-to/),
[streams README](https://github.com/music-assistant/server/blob/dev/music_assistant/controllers/streams/README.md).

**Snapcast / AirPlay / ESPHome** — [snapcast](https://github.com/badaix/snapcast) and its
[configuration](https://github.com/badaix/snapcast/blob/master/doc/configuration.md),
[ESP32 snapclient](https://github.com/CarlosDerSeher/snapclient),
[esp-snapcast](https://github.com/DavidVentura/esp-snapcast),
[shairport-sync](https://github.com/mikebrady/shairport-sync),
[airplay-esp32](https://github.com/rbouteiller/airplay-esp32),
[ESPHome speaker media player](https://esphome.io/components/media_player/speaker.html),
[audio_http](https://esphome.io/components/media_source/audio_http/).

**MCP / Matter / Apple** — [2025-11-25 transports](https://modelcontextprotocol.io/specification/2025-11-25/basic/transports),
[2026-07-28 changelog](https://modelcontextprotocol.io/specification/2026-07-28/changelog),
[server/discover](https://modelcontextprotocol.io/specification/2026-07-28/server/discover),
[Server Cards SEP-2127](https://modelcontextprotocol.io/seps/2127-mcp-server-cards),
[IANA service names](https://www.iana.org/assignments/service-names-port-numbers/service-names-port-numbers.csv),
[`connectedhomeip` device types](https://raw.githubusercontent.com/project-chip/connectedhomeip/master/src/app/zap-templates/zcl/data-model/chip/matter-devices.xml),
[esp-matter](https://docs.espressif.com/projects/esp-matter/en/latest/esp32/introduction.html),
[Apple HomeKit](https://developer.apple.com/homekit/).

**This repository, read 2026-11-24** — `docs/specs/sd-stream-server.md` §6–§7,
`docs/specs/front-door.md` §2, `docs/specs/k7player.md`, `docs/specs/espnow-stream.md`,
`docs/specs/menu.md`, `components/portable/front_door/front_door.c` (`/api/status`),
`components/portable/mcp/mcp.c`, `components/portable/http_server/http_server.{c,h}`,
`components/portable/mini_icecast/mini_icecast.c`, `components/portable/playlist_http/`,
`components/portable/covers_http/`, `components/app/appliance/appliance.c:42,357`,
`components/app/board_esp32/board_esp32.c`; ADR 0049, 0094–0099, 0102–0109, 0116;
`README.md` (memory and encoder census); `docs/research/service-surface-and-opportunities.md`.
