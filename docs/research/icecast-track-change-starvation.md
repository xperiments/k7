# A track change starves the stream because the decoded ring starts empty and the rewind is clocked by the audio it is losing

Measured on the Sunton ESP32-S3 (240 MHz, 800×480 panel, SD over SPI) while the station was
44,100 Hz / 256 kbps (`components/app/appliance/appliance.c:205-206`) and a client was attached —
the log carries a worst-connection stack figure. One track boundary, captured by the owner. The
board's telemetry says the encoder is not the limit and the resampler is bypassed; the log says the
broadcast produced audio at about half real time for eleven seconds. The mechanism is a boundary
that starts with an empty decoded-audio ring, followed by the one thing that starts at the same
instant on the decoder's core: the cassette's eight-second rewind, whose redraw work is clocked by
the audio it is starving with.

## What was seen

The owner's report is "song starts, starves, then goes": every track change the broadcast stalls for
several seconds. The verbatim excerpt, timestamps in microseconds:

```
I (1135766) screen: rewind: the hub ring dims
W (1137384) icecast: starved for 1066 ms, rebasing the broadcast clock (#1)
W (1139667) icecast: starved for 1159 ms, rebasing the broadcast clock (#2)
W (1141989) icecast: starved for 1041 ms, rebasing the broadcast clock (#3)
W (1144411) icecast: starved for 1246 ms, rebasing the broadcast clock (#4)
I (1144577) icecast: encoder: 43493 frames, 16958 us/frame = 64.9% of one core
I (1144578) icecast: decode 49194 ms (of which SD 6090 ms), resample 0 ms, tone 0 ms = 71.8% / 0.0% / 0.0% of one core
I (1144584) icecast: memory: internal 53531 B free (largest block 31744 B), external 1749888 B free; starvation rebases 4
I (1144595) icecast: stack headroom: broadcast 11172 B, worst connection 960 B
W (1146555) icecast: starved for 1125 ms, rebasing the broadcast clock (#5)
I (1149691) screen: rewind: the hub ring back to the playing red
```

Three clocks are in play and must not be mixed. The encoder line is a lifetime average: `frames`
and `encode_us` are never reset (`components/portable/mini_icecast/mini_icecast.c:1348-1353`,
`mi_encode.c:254-255`). The decode/resample/tone line is the last 60-second window: the cells are
read and cleared every period (`mini_icecast.c:1359-1363`). The two screen lines are the rewind's
fade-out and fade-in, emitted when the tape ink reaches 0 and when it comes back to full
(`components/app/screen_display/cassette_tapes.c:819-826`).

The excerpt is at minimum a crop that begins at the dim, the ink's 1 000 ms mark and therefore 1.0 s
of new-track audio after the boundary; the lines that would date the boundary (`track open`,
`now playing`, `track request`) are all earlier and are not in it. The dim itself proves the open
path ran — a file that failed to open or was skipped would leave `elapsed` at 0 and never reach the
dim — so only its duration is missing. Absent a wider capture, the absence says nothing about which
of the later paths ran, and `track open` is the number this note needs most (see "The open
question").

## What the numbers say

An MP3 frame is 1152 samples at 44.1 kHz = 26.122 ms (`mi_encode.c:231,248`). The encoder takes
16.958 ms of CPU per frame, so it occupies 64.9 % of one core while producing audio and has a
ceiling of 26.122 / 16.958 = **1.54× real time** when it has input. It did not have input.

The 65 % is a property of the station format and this build, not of the starvation. The appliance
asks for 256 kbps where the specification recommends 128 (`components/app/appliance/appliance.c:206`,
`docs/specs/sd-stream-server.md:322-328`), and F4's "under 35 % of one core" is an acceptance
threshold that no run in `audio-decode-cost.md` meets. It matters here only as headroom: the
encoder's core has a third spare, so it is never the stage that runs out.

The five rebases are not five holes. Each `pace()` call compares the audio time encoded since the
last rebase with wall time (`mini_icecast.c:587-609`); a rebase moves the origin so the debt reads
zero, but it produces no audio. The interval between two rebases therefore measures the production
rate directly: wall between them minus the debt at the second is the audio produced.

| Rebase | at (µs) | logged behind | interval since previous | audio produced in interval | rate |
|---|---|---|---|---|---|
| #1 | 1 137 384 | 1 066 ms | — | — | — |
| #2 | 1 139 667 | 1 159 ms | 2 283 ms | 1 124 ms | 49.2 % |
| #3 | 1 141 989 | 1 041 ms | 2 322 ms | 1 281 ms | 55.2 % |
| #4 | 1 144 411 | 1 246 ms | 2 422 ms | 1 176 ms | 48.6 % |
| #5 | 1 146 555 | 1 125 ms | 2 144 ms | 1 019 ms | 47.5 % |
| #1 → #5 | | | 9 171 ms | 4 600 ms | **50.2 %** |

So between the first and the last rebase the whole pipeline — decoder, ring, resampler, encoder —
delivered 4.60 s of audio in 9.17 s of wall time. The encoder's own ceiling is 1.54×, and the
resampler is bypassed (`dec` 0 ms, source rate equal to the station rate), so the missing half was
upstream of the broadcast task, not inside it.

The lifetime totals cross-check the window. 43 493 frames × 26.122 ms = **1 136.1 s** of audio
(18.94 min) against 1 144.577 s of uptime = **99.3 %**. Not all of the 8.4 s between them is loss:
the station spends a second or two after boot scanning and opening its first Track, and
`frames_encoded` starts at the first block. Allow 1.2 s for that and about **7.3 s** of wall time
since boot was not spent sending audio — close to what this one boundary accounts for (4.57 s
between #1 and #5, plus the deficit before #1 and the tail after #5), and consistent with a session
that has crossed one or two long Tracks rather than a dozen short ones.

The screen's span is a second measurement of the same rate, from a different code path. The rewind
closes when the *new track's elapsed* reaches 8 000 ms (`components/portable/cassette/include/cassette.h:68`,
`components/portable/cassette/cassette.c:425-427`), and `elapsed_ms` is derived from encoded frames,
not from a wall clock (`mini_icecast.c:592`, `:1726-1727`). The tape's ink is 0 from elapsed 1 000 ms
and full again only on the closing step at elapsed 8 000 ms (`CASSETTE_REWIND_MS` and
`CASSETTE_REWIND_FADE_MS`, `cassette.h:68-70`; `cassette.c:111-120`,
`components/app/screen_display/cassette_tapes.c:819-826`), so the log's dim and red lines are the
window's 1 s and 8 s marks. The 13.925 s of wall between them therefore spans **7.000 s of tape
time** = **50.3 %**, against the rebase table's 50.2 %. Two clocks that share nothing but the
encoded-frame count agree to a tenth of a percent. The animation did not overrun its constant: 8 s
of audio at 50.3 % takes ~15.9 s of wall, and the dim→red slice is the 7 s between the ink's
endpoints.

The decoder window: **49.194 s inside `mi_dec_read`**, of which 6.090 s was `fread`, leaving 43.104 s
= 71.8 % of the window's wall time = **0.72 core-seconds per wall-second**. Read as a per-audio-second
cost at the station's ~1× average, the decoder needs 0.72 of a core, which leaves about **28 % of
core 0** for the screen, Wi-Fi, lwIP and the port. Note what this number is and is not: it is wall
time around the decode call (`mini_icecast.c:812-817`), so it includes any preemption the task
suffered, not only its own CPU. It is an upper bound on decode CPU, not a measurement of it.

## The chain of causation

**1. The next track starts with an empty ring, by construction.** The ring is 384 KB of decoded
stereo frames (`mini_icecast.c:774`), 98 304 frames = **2.23 s** at 44.1 kHz. At a natural end the
broadcast drains it to empty before it breaks: `mi_ring_get` returns 0 and `j.eof` is set
(`mini_icecast.c:924-929`). At a skip/previous/pick it breaks immediately (`:922`) and frees the ring
with the broadcast no longer reading it (`:937-942`, `:845`), throwing away up to the full 2.23 s of
already-decoded audio. Either way, the first frame of the next track is fed from a ring whose level
is zero. This is the source-is-per-Track decision of
[ADR 0041](../adr/0041-the-decoded-source-is-its-own-task.md): the ring is created in
`stream_file()` and freed when the track stops, so it cannot carry a cushion across the boundary.
The 10-second client ring (`mini_icecast.c:63`) is downstream of the encoder and cannot help.

**2. The only producer is the source task, and it is not running during the open.** `stream_file()`
creates the ring and the `ice_decode` task (`mini_icecast.c:860-880`), then blocks on `j.ready`
(`:881`) while `src_task` runs `mi_dec_open(j->path)` (`:792`). That call is `fopen` + `setvbuf` +
`drflac_open`/`drmp3_init` (`components/portable/mini_icecast/mi_decode.c:187-240`), the one stage
in the boundary with no bound. The code says so and logs its duration: "The track change is the one
moment the stream has nothing to send: how long the open and the length took is what tells a slow
card from a slow scan"
(`mini_icecast.c:797-801`). That log line is not in the excerpt. After the open, the broadcast still
needs a 1024-frame block before the chunker will emit (`:702-716`), 23.2 ms of audio; the tail flush
adds at most one 1152-frame encoder frame of silence (`:743-753`, `mi_encode.c:344-350`, spec
requirements E5 at `docs/specs/sd-stream-server.md:296-298`). `mi_rs_free` releases a `calloc`'d
bypass struct (`mi_resample.c:67-80`, `:197-201`) and `src_job_free` releases the 384 KB ring plus four semaphores
(`mini_icecast.c:843-852`); both are small next to the open.

A gap is not, by itself, a rebase. The pacer aims to send 1 500 ms ahead of real time
(`components/portable/mini_icecast/mi_station.c:15`, `mini_icecast.c:607-608`), so the first
starvation of a healthy run needs roughly 2.5 s of net loss from the last on-time frame; after a
rebase the lead is zeroed, so the next needs only 1.0 s. The first rebase's 1 066 ms is therefore
*not* a 1.066 s hole: it is the residual debt at the moment the threshold was crossed, and the dim
1.6 s earlier proves new audio was already flowing. The open gap is bounded by that debt plus
whatever lead the pacer still held — at most 1.07 s if the lead had been fully spent, at most 2.57 s
if it still held its full 1.5 s — and the excerpt does not record the lead. The boundary's own gap
is the small part; the sustained half-rate is the rest.

**3. `pace()` rebases the measurement, not the pipeline.** The code is explicit:

```c
int64_t behind = now - played_us;
S.lag_ms = behind > 0 ? (uint32_t)(behind / 1000) : 0;
if (behind > 1000000) { /* we starved ... */
    S.rebases++;
    ...
    S.t0 += now - played_us;
    now = played_us;
}
int64_t ahead = played_us - now - (int64_t)S.station.lead_ms * 1000;
if (ahead > 0) vTaskDelay(...);
```
(`mini_icecast.c:593-608`.)

The rebase adds no frames to the ring and creates no lead. After it, `played_us == now`, so the
pacer will not sleep until it is 1.5 s ahead again, and it cannot get ahead while the ring is fed at
less than 1×. The broadcast task is not descheduled — it is priority 6 on its own core, and the pacer
does not delay while it is behind (`mini_icecast.c:607-608` only delays when `ahead > 0`) — it is
blocked on input in `stream_file()`'s ring-empty path: `mi_ring_get` returns 0, then
`xSemaphoreTake(j.data, 20 ms)` and `vTaskDelay(1)` (`:924-929`). It encodes each block the instant
it arrives.

**4. Why five.** The encoder's 1.54× ceiling is irrelevant when the ring is empty; the broadcast can
only run at the rate the decoder feeds it. At 50.2 % of real time the debt grows by ~1 s every ~2 s,
so a new rebase fires every **2.14–2.42 s** of wall time after the first — the observed intervals,
not the ~1.9 s a perfectly smooth 50 % would give. The excess is the crossing being noticed late:
`pace()` runs per encoded frame, so the debt meets the 1-second threshold only when a frame actually
arrives, and it had already reached 1.04–1.25 s at the four later crossings. Delivery is uneven as
well as slow — blocks arrive in bursts with waits between them. Five rebases are how many 1-second
crossings fit in the ~11 s that the under-production lasts; one hole produces one rebase, and a
second needs the rate itself to stay below 1× long enough to lose another second.

**5. What else starts at the boundary: the rewind, on the decoder's core.** The screen sees the new
track before the decoder is open, because `set_track()` publishes the name and resets
`track_frames` before `stream_file()` is called (`mini_icecast.c:1318-1328`, `:539-565`). The next
status poll sees `elapsed` go backwards and opens the eight-second window
(`cassette.c:378-381`, `:414-416`). This is the core split ADR 0041 decided; the tasks that then
share the two cores are:

| task | core | prio | stack | at the boundary |
|---|---|---|---|---|
| `ice_broadcast` | 1 | 6 | 16 KB internal (`mini_icecast.c:46`, `:1509-1510`) | flushes the tail, joins the old source, creates the new one, blocks on the source's open (`j.ready`), then resamples and encodes |
| `ice_decode` | 0 | 3 | 32 KB PSRAM (`mini_icecast.c:775`, `:875-876`) | opens the file and fills the 384 KB ring |
| `screen` | 0 | 3 | 16 KB (`components/app/screen_display/screen_display.c:696`) | polls the status, steps the model, invalidates the packs and hubs every 40 ms |
| LVGL port | 0 | 4 | 16 KB (`components/app/screen_display/panel_rgb.c:61-66`) | renders the invalidated area; 33 ms refresh, direct mode, two full-screen PSRAM framebuffers (`:144-160`) |
| `ice_send` | 0 | 4 | 4 KB (`mini_icecast.c:58`, `:1483-1484`) | copies a client ring to its socket; no extra work at a boundary |
| `artwork` | 0 | 2 | 16 KB (`components/app/screen_display/artwork_loader.c:77-79`) | only if the Peek or Real is up: open, tags, cover |
| `ice_telem` | 1 | 3 | 4 KB (`mini_icecast.c:47`, `:1512`) | the 60 s telemetry line |

Per frame the rewind does three things to core 0. `cassette_step()` interpolates both pack fills and
both hub phases from the old state to the new one (`cassette.c:437-444`); `cassette_tapes_set()`
resizes each pack and redraws its rings and strand (`cassette_tapes.c:812-845`, `:616-626`). The pack
diameter runs 168–356 px with the fill (`cassette_tapes.c:60-61`, `:632-638`), and the two fills are
complementary (`cassette.c:455-457`), so the two pack boxes together cover roughly 160 000 px,
**about 42 % of the 800×480 panel, on every 40 ms tick** (the two squares sum to ~162 800 px at every
fill position); the hub spokes add only a few dozen pixels each (`cassette_tapes.c:75`, `:289-314`).
The invalidated area is then rendered by the LVGL port, not by the screen task, at 33 ms in direct
mode with two full-screen PSRAM framebuffers (`panel_rgb.c:144-160`, `sdkconfig.sunton:4084`), so
the rewind renders about 25 times a second.

The decoder's own line is 0.72 core-seconds per wall-second, so it leaves about 28 % of core 0 for
everything else. The code puts a full-screen invalidation on this part at "over 100 ms"
(`screen_display.c:425-427`), so a ~42 %-of-screen redraw is the order of tens of milliseconds; at
25 Hz that is a large fraction of the 28 %, and any excess comes out of the decoder's rate. The
boundary is also a one-off heavy frame: `set_track()` changes the title, the cassette's ink and the
label (`components/app/screen_display/cassette_view.c:304-320`), and `cassette_levels` clears the
tags before the new ones arrive (`components/app/screen_display/cassette_levels.c:65-67`). The
screen task's adaptive backoff never engages on any of this: the per-pass cost at
`screen_display.c:404-405` is sampled with `t0` taken immediately before the measurement, so
`frame_ms` reads 0 and the rest is always the 40 ms floor (`:425-433`). The backoff the comment
describes does not exist as written — and it times the screen task's own object updates, never the
port task's render.

**6. The rewind is clocked by the audio, so the stall and the animation are the same length.** This
is the part ADR 0011 did not cover. The ADR says the window is "driven by the new track's elapsed,
not the wall clock … a slow frame drops drawn frames but cannot stretch the window"
(`docs/adr/0011-cassette-rewinds-on-a-track-change.md:20-24`) and lists as a consequence that "the
screen resizes both packs every update for 8 s" (`:69-71`). Both statements are true of a slow
*render* and false of a slow *stream*: the window's progress is `(elapsed - elapsed_at_detection) /
8000` (`cassette.c:425-427`), and `elapsed` only advances when the encoder emits frames
(`mini_icecast.c:592`, `:1726-1727`). At 50.3 % of real time the nominal 8 s window is ~15.9 s of
wall, and the screen draws ~2× the frames the ADR budgeted. The extra work would keep the decoder
down for longer, which stretches the window further: a feedback loop with the amplifier set by the
decoder's own current. Whether the redraw is what takes the core is the open question below; what
is proven is that the window's length is set by the audio.

The dim and red lines are consistent with that and not with an animation that is "late": the dim is
at 1 s of elapsed and the red on the closing step at 8 s, so the 13.9 s between them is 7 s of tape
time at 50.3 %, exactly the rate the rebases measure. Nothing in the LVGL timer is off; the tape is
slow because the tape is the audio.

## What it is not

| Candidate | Verdict | Evidence |
|---|---|---|
| The socket, a slow client, or the send path | ruled out | `pace()` counts encoded frames, not bytes sent (`mini_icecast.c:611-617`); `broadcast()` only enqueues into a 10 s client ring (`:63`, `:175-195`, `:261-274`), and no "fell behind" or "client gone" line appears |
| Encoder throughput | ruled out | 16.958 ms of CPU per 26.122 ms frame = 64.9 %, a 1.54× ceiling; it cannot be the producer of a 50 % rate |
| Resampler or tone | ruled out | both log 0 ms (`:1364-1368`); the resampler is a bypass at equal rates (`components/portable/mini_icecast/mi_resample.c:80`) |
| The end-of-track flush | ruled out as the cause | at most one encoder frame of silence, 26.12 ms (`mi_encode.c:344-350`; spec E5, `docs/specs/sd-stream-server.md:296-298`); a rebase needs 1 000 ms, the first needs ~2.5 s |
| The decoder open alone | not enough for five | a single open can produce at most one rebase; the first debt is only 1 066 ms, and the next four need the sustained rate |
| The 384 KB ring being too small | not the cause | it holds 2.23 s and is empty at the boundary in both paths (`mini_icecast.c:924-929`, `:937-942`); a larger ring is also empty at the next track's first frame |
| The SD card | not the differentiator in this window | 6.09 s of 60 s; the known-good 16-bit/44.1 kHz run spent **12.6 s** of 60 s and still made 101.3 % with zero rebases (`docs/research/audio-decode-cost.md:65-67`) |
| Memory | ruled out | 53 531 B internal free with a 31 744 B largest block, 1 749 888 B external, 11 172 B of broadcast stack headroom (`log`; `mini_icecast.c:1373-1384`) |
| The 96 kHz source-rate gate | it passed | `resample 0` means source rate = 44.1 kHz, which is below the 96 kHz cap (`components/portable/mini_icecast/include/mini_icecast.h:41-47`); the gate checks rate only |
| The artwork loader | only if the Peek or Real was up, which the excerpt cannot rule out | a track request is made on every change but the loader is asked only when the Peek or Real is visible (`components/app/screen_display/cassette_levels.c:54-75`); it is core 0, priority 2, does not park the card, waits up to 20×50 ms while `lag_ms > 0` and yields every 16 KB (`artwork_loader.c:77-79`, `:167-172`, `:208-215`); `lag_ms` reads near 1 s only at the instant before each rebase — `pace()` writes it from the debt and then re-anchors the clock, so the next frame writes it near 0 (`mini_icecast.c:596`, `:604`) |

Three secondary facts are worth recording because they bound the UI hypothesis. The panel scans about
32 MB/s out of PSRAM (`panel_rgb.c:31-37`), and the decoder's ring and 32 KB stack are in the same
PSRAM (`mini_icecast.c:774`, `:875-876`); the redraw adds traffic to a bus the decode already shares.
If the Peek or Real was up, the artwork loader also shares the one SD bus: it is core 0, priority 2
(`artwork_loader.c:78-79`), so it cannot preempt the decoder, but it can hold the VFS volume lock
through an `fread` (`:30-31`, `:208-215`) and delay the decoder's next card read. And the screen's
"rest" controller, as noted, is inert (`screen_display.c:404-405`), so the UI does not reduce its
rate when a frame is expensive.

## The open question

The log proves *where* the audio is missing — the broadcast is blocked on a ring that the decoder
has not filled — and that the rewind is coextensive with the stall. It does not prove *why the
decoder is below 1×*, and there are two candidates that the log cannot separate:

1. **The source is intrinsically expensive.** The 0.72 above is per wall-second of the window; per
   audio-second the window actually produced (≈55 s), the same 43.104 s is ~0.78 even if every
   millisecond of it were CPU. The earlier 16-bit/44.1 kHz FLAC measured 0.146 in the same terms
   (`audio-decode-cost.md:65-67`). A 24-bit or unusually high-order 44.1 kHz file could be several
   times heavier, and the rate gate would let it through because it only checks Hz
   (`mini_icecast.h:41-47`).
2. **The decoder is preempted by the rewind.** `dec_ms` is wall around `mi_dec_read`
   (`mini_icecast.c:812-817`), so every millisecond the port task (priority 4) takes from the
   decoder (priority 3) is inside the 49.194 s. If the source is closer to the 0.15–0.3 range,
   then most of that 43 s was not CPU at all, and the rewind is the reason the decoder ran at 0.5×.

The recovery bounds the first candidate without settling it. The stream resumes while the same file
is still decoding: the last rebase is at 1 146 555 µs and the ink returns at 1 149 691 µs with audio
flowing throughout. So the source cannot cost more than core 0 leaves it once the rewind is over —
a Track that needed 0.78 of a core would go on starving for the rest of its length unless the quiet
system left the decoder almost the whole core. That is possible, not typical, and it is the
difference between the two hypotheses: 0.15–0.3 says the screen took the time, 0.7–0.8 says the file
did.

The circularity is what makes the log insufficient: the rewind lasts as long as the audio takes, so
"the stall lasts as long as the rewind" is true under either hypothesis. The coincidence in time is
not evidence of cause. The excerpt also lacks the boundary's own line, `track open` — the missing
datum is the open time, since the first rebase's 1 066 ms bounds the debt, not the gap (§2), and
`track open` is the line that would close it.

## The measurement that would settle it

1. **The full boundary, unfiltered.** `track open: %s (N ms)` and `now playing` already exist
   (`mini_icecast.c:799-801`, `:917`); the excerpt's first line is after both. One capture that
   starts at `set_track` would give the open time for free and show whether the card is slow.
2. **Count the ring wait.** Time the zero-return path at `mini_icecast.c:924-929` and print
   "ring starved N ms" beside the decode line. If it is ~5 s in the same window, the broadcast is
   definitively input-starved; the log currently only implies it.
3. **Count the audio decoded.** Add `S.dec_frames += n` in `src_task` (`mini_icecast.c:814`) and
   print decoded seconds per window. Then (dec_ms − io) / decoded_s is the decoder's wall cost per
   audio-second. Compare it with the 0.146 of the healthy 16/44.1 run (`audio-decode-cost.md:67`):
   ~0.15–0.3 means preemption, ~0.7–0.8 means the source. This is the single cheapest decisive
   number.
4. **Per-task runtime on core 0.** `CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS` is off in both
   configurations (`sdkconfig:2640`, `sdkconfig.sunton:2622`). One build with it on and
   `vTaskGetRunTimeStats()` in the 60 s line would show whether the port and screen gain the
   decoder's share during the stall.
5. **A/B the rewind.** Repeat the same boundary with the pack resize suppressed (the ADR's own
   fallback, "a coarser diameter step",
   `docs/adr/0011-cassette-rewinds-on-a-track-change.md:69-71`) or with the screen task parked. If
   the rebases fall to zero and the decode share drops, the contention link is proven; if they
   persist, the source is.
6. **Fix the soak's accounting.** `host/tests/board_soak.sh:93` counts `starved for` lines, but the
   firmware only logs rebases #1–#5 and every 25th (`mini_icecast.c:601-603`), so its count
   saturates at five; the true count is the telemetry's `starvation rebases N` (`:1373`). The soak
   also needs a run that is known to cross at least one track boundary: a five-minute run of tracks
   longer than the run proves nothing about this path (`README.md:526-529`).

A host test can pin the pacing arithmetic — first rebase after 2.5 s of net loss, subsequent ones
after 1.0 s — by driving the same `pace()` logic with a synthetic clock, but the host cannot model
two competing tasks on one core. The contention question is board-only.

## Next step

No fix is decided here. Ranked by the evidence:

1. **Measure (1–3 above) before changing the pipeline.** The two causes want different fixes, and
   item 3 is cheap.
2. **If the decoder is preempted:** the rewind is the amplifier and it is already ~2× longer than
   designed. Wall-clock its 8 s (or bound it by `lag_ms`), and coarsen the pack resize while the
   stream is behind. Both are screen-side and reversible; ADR 0011 already names the coarser step
   as the escape. Raising `SRC_PRIO` above the port's 4 is the direct fix but trades screen
   smoothness for audio and should follow a measurement, not precede one.
3. **If the source is expensive:** gate on measured decode cost at open, the way the rate gate
   works, rather than on Hz alone. Pre-opening the next decoder will not help by itself: the ring is
   already empty at the boundary, and the contended resource is the same core 0 the decoder is on.
4. **Not the levers the log rules out:** a larger decoded ring (empty at the boundary), a lower
   station rate (the encoder core is not the limit), and anything on the socket path.

## What followed

Written after this note, and not evidence for it:

- [ADR 0070](../adr/0070-the-reel-change-yields-to-the-broadcast.md) takes the screen-side
  changes: the window closes on the later of the two clocks, its packs move in 24 steps, the
  screen rests while `lag_ms` is up, and its frame cost is finally measured across the whole
  pass. The decode priority is untouched.
- The two counters this note asked for — decoded audio seconds and the ring wait — are in the
  60-second telemetry, and the soak now reads its rebase count from there and reports how many
  Track boundaries the run crossed (`docs/specs/sd-stream-server.md` §4.5, E6/E7).
- The open question stands until the board run: `decoded N s = X core-s per audio-second` is
  the number that says whether the source or the contention is the larger half.

