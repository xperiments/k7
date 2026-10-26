# Wind and rewind on the live stream

Axis: **the transport has `prev`/`next` and the owner wants to add wind and rewind that feel like a
cassette spooling the tape, with the mechanism audible — and asks whether that is affordable on this
chip, or whether a pre-recorded effect is the answer. This note establishes what the stream is today,
what a cassette actually does at that speed, what the program sounds like while it is spooled, what
each way of reproducing it costs here, and recommends one.**

Everything local is read out of this repository at commit `890dd9a` plus the working tree, or out of
the vendored sources it builds. No board run was made while writing this; where a number can only
come from the board it is listed in §9 as something still to measure.

Five kinds of statement appear below and they are never mixed:

- **MEASURED (board)** — printed by this firmware's telemetry on hardware. Quoted from the note that
  recorded it, not re-run here.
- **VERIFIED** — read in the artefact that owns the behaviour: a line of source, a Kconfig `help`, a
  header comment. Cited by `path:line`.
- **ARITHMETIC** — computed here from cited inputs. The multiplication is mine and the assumption is
  stated.
- **ESTIMATE** — a figure derived from a measurement with a stated model, and able to be wrong by
  tens of per cent.
- **UNVERIFIED** — widely repeated or plausible, but not traced to a primary source in this pass. It
  is named as such and never carries a decision.

Two files are rewritten by every build (`sdkconfig.sunton`, `build_sunton/config/sdkconfig.h`) and are
cited by symbol, not line. Hand-written sources are cited by `file:line`.

**What this note does not have.** No manufacturer document for a C60 wind time was reached in this
pass, so §2.1 states its assumption instead of citing a deck. Nothing here was measured on the board.
§9 lists the five measurements that would change the answer.

## Bottom line

1. **The streaming does not seek, and does not need to in order to wind.** Every track is decoded,
   resampled, chunked, encoded and fanned out on one task at one fixed format; `prev` and `next` are a
   file reopen at a chunk boundary, and there is no seek call anywhere (§1). The encoder measures
   41.4 % of one core on the fixture corpus, 52.9 % on synthetic noise and 60.6 % on a dense live
   recording (MEASURED), and decode plus resampling are not in those numbers, so the real headroom is
   **between a quarter and a half of one core**, not the 40-59 % the encoder alone implies.
2. **A cassette winds at roughly 20-45x play speed, not 2-3x** (§2.1). At that ratio the program's
   spectrum is multiplied by the ratio, so **only the bottom ~670 Hz of the music comes back inside a
   20 kHz band** and everything above it leaves (§2.2). What a listener hears in wind is a rhythmic
   chirp with a bright roar over it — the machine is most of the sound.
3. **An audible varispeed spool — the program flying past, pitch-shifted — is not affordable.** Decode
   cost is linear in the spool speed: at 20x it is an estimated 152 % of one core for the decoder alone, before
   the resampler's kernel grows to 338 taps (which the component refuses at its 256-tap ceiling) and
   before the 41-53 % encoder (§7.1). 2x is comfortable, 3x marginal, 4x does not fit.
4. **A *gated* spool is affordable (~8 % of a core) but is a different fiction**: it keeps the
   program at its own pitch and chops it, so it sounds like a scan, not like tape (§7.2).
5. **Seeking is cheap, but the vendored MP3 decoder makes *rewind* expensive by default.** Without a
   seek table, `drmp3_seek_to_pcm_frame()` restarts the stream and decodes forward to the target, so
   seeking from 4:00 to 3:50 decodes 230 s of audio — while seeking to 0:10 decodes 10 s (§5). The fix
   is a 6 KiB, 256-point seek table, most of whose cost the firmware *already pays* at track open for
   headerless MP3s.
6. **Recommendation: the "rerecorded mp3" is the right shape, and cheaper than it sounds.** A
   wind needs a sound, a virtual position and one seek — not a decode at spool speed. Play a recorded
   mechanical bed through the seam that already exists for the idle bed (~0 extra CPU, 353 KB of
   PSRAM or a ~32 KB embedded MP3), move a number while the button is held, seek once at release, and
   give the screen's existing eight-second rewind an explicit signed input so the release does not
   open a second one (§8).

## 1. The streaming as it stands

### 1.1 One pipeline, one format, one clock

Every track goes through the same five stages on the broadcast task, pinned to core 1 at priority 6
(`mini_icecast.c:1033`):

```
mi_dec_read (dr_mp3 / dr_flac / dr_wav)
   -> mi_rs_process (windowed-sinc to the station rate; bypass when the rates match)
      -> chunk_append (1024-frame chunks, carried across track boundaries)
         -> mi_enc_feed (shine, 1152-frame encoder frames, whole frames only)
            -> broadcast(): burst ring + one send ring per client
               -> pace(): the 1.5 s lead and the starvation rebase
```

- **One station format, fixed for the life of the process**: 44.1 kHz / 256 kbps CBR joint stereo
  (`README.md:260-266`). Nothing on the wire is per-listener, so the effect of any transport is heard
  by every client at the same moment. There is no second encoder, and no room for one.
- **There is no seek.** `mi_decode.c` hands dr_libs the seek callbacks a decoder needs to open a file
  (`mi_decode.c:36-44`) and then never calls a seek function. `mi_decode.h:14-23` exposes open / hz /
  duration / read / close — no seek, no tell, no position.
- **`prev` and `next` are a reopen, not a seek.** `mini_icecast_skip()`/`_prev()` set a bare
  `volatile bool` without the lock (`mini_icecast.c:1089-1091`); `stream_file()` breaks its read loop
  on the flag (`mini_icecast.c:725`), `flush_pipeline()` completes the partial encoder frame with
  silence (`mini_icecast.c:688-697`), and the broadcast task picks the next Playlist entry and opens
  it from sample 0 (`mini_icecast.c:867-880`). Cost: one `fopen`, one decoder init, up to 26 ms of
  silence in the encoder's tail. That is why they are cheap, and why they cannot be half-done.
- **The position is derived, not stored.** `pace()` adds every emitted frame to `S.track_frames`
  (`mini_icecast.c:551-571`), and `mini_icecast_status()` turns it into `elapsed_ms`
  (`mini_icecast.c:1117-1118`). `S.idle_frames` — the noise bed played while a download owns the card —
  is subtracted first, so a park does not advance the bar. Two counters matter here: `S.played_frames`
  is the *virtual clock* (pacing) and must keep counting through a seek; `S.track_frames` is the
  *position* and is the one a seek must rebase.
- **The pacing clock is frame-counted, not wall-clocked.** A seek that emits the same number of frames
  per second cannot disturb the lead. A seek that *blocks* for more than a second is indistinguishable
  from a card stall: `pace()` sees `behind > 1 s`, logs "starved", and increments `S.rebases`
  (`mini_icecast.c:559-568`).
- **The glossary says the stream cannot be seeked** (`docs/specs/CONTEXT.md:57-59`: "it cannot be
  paused, seeked or replayed"), and `README.md:962-990` repeats it. A rewind changes the product's
  definition, not only the code.

### 1.2 What the screen already knows how to do

The cassette model is further ahead than the audio is. It opens an **eight-second rewind window**
whenever `elapsed` goes *backwards* while playing (`cassette.c:249-250`, `cassette.c:281-289`), and
inside the window both pack fills migrate, both hubs turn in reverse, the tape's red edge fades to the
tape colour and the VU goes quiet (ADR 0011, ADR 0015). The window is driven by the new `elapsed`, not
the wall clock, so a slow frame cannot stretch it.

Two consequences:

- A backward seek would *already* trigger that window, because the trigger is exactly
  `elapsed < last_elapsed`. That is a head start on the picture — and a trap: a hold-to-rewind that
  lands before it started would open a **second** eight-second rewind at release, and the reels would
  spool twice. The model needs an explicit "winding" input, not an inferred one.
- What the window cannot express is *direction* and *distance*. A forward wind raises `elapsed`, so
  nothing opens and the reels jump (`cassette.c:245-257` is the code that exists to prevent that); and
  the window is a fixed 8 s / 11 revolutions regardless of how far the tape moved. The entry point the
  model needs is something like `cassette_wind(c, delta_ms, now_ms)`, with the window's progress driven
  by `now_ms` rather than `elapsed` — otherwise a wind that starts while the decoder is still seeking
  (no new audio yet) can never close.

The four drawer buttons P / S / R / W exist and are deliberately unwired (`ADR 0016`,
`cassette_buttons.c:270-272`). A hold gesture is not free: `button_create()` attaches pressed,
released, press-lost and clicked (`cassette_buttons.c:201-204`), but there is no
`LV_EVENT_LONG_PRESSED`/`LONG_PRESSED_REPEAT` anywhere, and `screen_input` has no long-press policy. A
hold also has to coexist with the drawer's own gestures, since buttons bubble gestures to the screen
(`cassette_buttons.c:192-193`).

### 1.3 The budget the effect has to fit in

The only cost this firmware measures about itself is the encoder (`telemetry_task`, once a minute).
Measured figures, all on core 1 at the station format:

| what | cost of one core | source |
|---|---|---|
| MP3 encoder, tonal fixture corpus, 44.1k/256 | **41.4 %** (10.8 ms per 1152-frame frame) | `README.md:495`, `README.md:831` |
| MP3 encoder, synthetic full-scale noise | **52.9 %** | `README.md:285` |
| MP3 encoder, dense live recording | **60.6 %** | `docs/research/tape-fx-output-stream.md` §6.7 |
| MP3 decode, 48 kHz stereo, *not dr_mp3* | **7.6 %** at 1x (13.1x real time) | `docs/research/server/s8-input-decoders-and-multiformat.md:522` |
| FLAC decode, 44.1 kHz stereo, *not dr_flac* | **8.0 %** at 1x | `docs/research/radio-player-codecs.md:343` |

Everything above the encoder is an estimate, and the real budget is worse than the encoder figure
suggests: decode, resample and chunking run on the same task but are not in the telemetry. Normal play
already costs the encoder plus roughly 8 % of decode, plus ~4 % of resampling when the source rate is
not the station rate (§7.1). That leaves **between about a quarter and a half of one core** for a
transport effect — not the 40-59 % the encoder alone implies.

Two things tighten it further:

- **A wind bed is noise, and noise is the encoder's expensive case.** The 52.9 % row is synthetic
  full-scale noise, "close to the hardest input a codec can be given" (`README.md:292-293`); music
  measures 41.4 %. A bed is quieter than full scale but still noise-like, so its *own* encoding costs
  nearer 53 % than 41 %. Budget the wind somewhere in between, nearer 53 % if the bed is dense.
- **The task is one thread.** A spool that blocks is not "free CPU that pauses the music": it is a
  stall the pacing loop treats as starvation, and a stall longer than the 1.5 s lead plus the client
  rings is a dropout.

## 2. What a cassette actually does

### 2.1 The speed

The compact cassette's play speed is **4.76 cm/s (1 7/8 in/s)**, fixed by the format's own standard
(the Philips system, standardised as IEC 60094). This repo already carries that constant for the
screen's geometry — `CASSETTE_TAPE_SPEED_MM_S 47.625f`, `cassette.h:39-41` — so the picture and the
physics agree by construction.

A C60 holds 30 minutes per side, so one side is `1800 s x 4.76 cm/s = 85.7 m` of tape (ARITHMETIC).
Wind and rewind do not read at 4.76 cm/s; the deck spins the take-up reel and the tape flies. The
spool speed follows from how long a side takes to wind:

| a C60 side winds in | tape speed | ratio to play speed | hub at a full pack (r = 24 mm) |
|---|---|---|---|
| 30 s | 2.86 m/s | 60x | 1 140 rpm |
| 60 s | 1.43 m/s | 30x | 570 rpm |
| 90 s | 0.95 m/s | 20x | 380 rpm |

ARITHMETIC: 85.7 m / t, divided by 0.0476 m/s; hub speed = v / (2 pi r). The hub figures are the sanity
check — a cassette reel motor really does run at several hundred to about a thousand rpm in wind — and
they explain why the screen's reels cannot be honest: at 30x the hub turns about ten times a second,
and ADR 0011's 36 art frames at 25 fps cannot show that without strobing into noise. The model's 11
revolutions over 8 s is a perceptual stand-in, and §7 inherits the same ceiling.

Two properties of a real spool matter for the sound: the ratio is **not constant** (the pack radius
grows as the tape moves onto the take-up hub, so linear speed falls unless the motor is regulated), and
it is **fast** — one or two orders of magnitude above play speed, not two or three times.

**UNVERIFIED in this pass:** a manufacturer figure for a C60 wind time. The table states its
assumption; a manual's number would replace the range without changing anything else in this note.

### 2.2 What the program does at that speed

Wind is a change of playback speed, so it multiplies every frequency by the ratio. At 30x, in a band
that ends at 20 kHz:

| source | comes back at 30x | audible? |
|---|---|---|
| 60 Hz (bass, kick) | 1.8 kHz | yes, and it carries the rhythm |
| 200 Hz (low vocal, guitar body) | 6 kHz | yes, thinned and sibilant |
| 500 Hz (mid) | 15 kHz | yes, near the top of the band |
| 1 kHz (presence, cymbals) | 30 kHz | no |
| 2 kHz and above | 60 kHz | no |

At a cassette's speed **only the bottom ~670 Hz of the music survives**, swept up into the middle of
the band; everything above it leaves. What a listener hears is not "the song, fast": it is a rhythmic
chirp — the bass line and the transients, pitched up and stripped of what makes the song recognisable.
The transport's own noise is broadband, so it sweeps up into the band and becomes a bright roar at
exactly the moment the music gets thin.

That asymmetry is the most important product fact in this note: at the speed a cassette actually winds,
**the machine is most of the sound and the program is a detail**. It is why §7.3's recorded bed does
not have to contain the music to be convincing.

### 2.3 Whether the program is audible at all

It varies by deck and by whether the deck lifts the tape off the head during wind. Domestic decks
generally let you hear the spooled program; decks with **cue** and **review** make a feature of it by
transporting at a lower speed (a few times play speed rather than tens), which is why cue/review sounds
like a comprehensible, fast song rather than a chirp. That distinction is the same one as §7.1's
varispeed (expensive, sounds like cue) against gated (cheap, sounds like a scan).

**UNVERIFIED in this pass:** the behaviour of any named deck in wind. Treated here as the harder case —
assume the program is audible — because the recommendation does not depend on the answer.

## 3. What the sound is made of

A wind bed is a machine, not weather. The components, in the order they dominate:

| component | what it is | what it needs in a recording |
|---|---|---|
| motor and gearbox | a tone at the hub's rotation rate (≈10 Hz at 570 rpm) with harmonics climbing into the hundreds of Hz, plus motor whine in the low kHz | low-frequency body; this is the "bass" of the effect |
| tape over guides and head | broadband hiss and scrape, modulated as the pack changes | a dense, changing hiss — the "air" |
| pack slip and clutch | irregular impulsive clatter | real transients, not a looped click |
| the deck's own case | resonance that colours everything | recorded, not synthesised |
| the program (if audible) | §2.2's swept chirp | cannot be recorded in advance |

Two consequences for the asset:

- **A generic "wind whoosh" sound effect is wrong.** It will read as weather. What is wanted is a
  mechanical recording: if it does not have a motor's periodicity in it, it will not sound like a
  cassette no matter how loud it is.
- **The program cannot be in the asset.** That is the price of pre-recording, and §2.2 is why the price
  is small: at 20-45x the program is a chirp over the machine, and a listener who has heard a cassette
  wind does not remember the song — they remember the noise.

## 4. The ways to make it, and what each one needs

| way | what the pipeline must do | per-sample cost | what it sounds like |
|---|---|---|---|
| **varispeed** — play the program faster, pitch follows | decode N x faster; kernel N x wider (or accept aliasing); card read N x | N x a whole decode pass (§7.1) | like cue/review: a fast, comprehensible song |
| **reverse** — the same, backwards | a decoded buffer played in reversed blocks, plus the N x decode to fill it | same as varispeed, plus a buffer | reversed chipmunk |
| **gated** — decode at 1x, play one chunk in N | nothing new | ~1x decode (7.6 %) | a scan: snippets at their own pitch |
| **synthesised bed** — noise + a tone that follows the rate | generate PCM on the seam | a few per cent of a core | convincing if the noise is shaped well; responsive to speed |
| **recorded bed** | loop a buffer on the seam | a memcpy, well under 1 % | as good as the recording; fixed |

The names matter less than the split: the first two are *the program at speed* and cost decode
proportional to that speed; the last two are *the machine* and cost almost nothing. §7.1 puts numbers
on the first pair and §7.3 on the last.

This is the family a DAW calls "tape stop" or "varispeed", and every version of it that sounds like tape
does the same thing underneath: it changes the playback rate. There is no cheap trick that makes a
rate change free when the decoder's output is what is being rate-changed.

## 5. What the vendored decoders actually do

Positioning is the cheap part *if the format cooperates*, and this firmware currently ships nothing
that makes it cooperate. Per format, read out of the vendored sources:

| format | what a seek costs today | note |
|---|---|---|
| **WAV** | `fseek` to a byte offset; exact, O(1) | `drwav_seek_to_pcm_frame`, `dr_wav.h:6108` |
| **FLAC** | the file's own SEEKTABLE if it has one, otherwise a brute-force decode from the start | the table is loaded automatically at open (`dr_flac.h:8217-8250`); binary search is compiled out because `mi_decode.c:12` defines `DR_FLAC_NO_CRC`, and `dr_flac.h:95-99` says that "will disable binary search seeking" — dispatch at `dr_flac.h:11852-11866` |
| **MP3, no table** | seek to the *start of the stream*, then **decode and discard forward to the target** | `dr_mp3.h:4403-4423`; the comment at `dr_mp3.h:4382-4389` admits it: "Just using a dumb read-and-discard for now" |
| **MP3, with a table** | seek to the nearest preceding seek point, then decode the remainder | `dr_mp3.h:4450-4505`; a `drmp3_seek_point` is 24 B aligned (`dr_mp3.h:322-329`), so 256 points is 6 KiB |
| **MP3, to 0** | a restart; cheap in every case | `dr_mp3.h:4515-4517` |

Three consequences that are not the ones you would guess:

- **Without a table, a backward seek costs a decode of the target's *position*, not of the distance
  travelled.** Seeking from 4:00 to 3:50 decodes 230 s of audio; seeking from 4:00 to 0:10 decodes 10 s.
  At the published 13x real-time decode rate that is ~17 s of work and ~0.8 s of work respectively.
  **Rewinding to the start is the cheapest rewind there is.**
- **The seek table is usually already paid for.** `drmp3_calculate_seek_points()` is documented as
  "slow" (`dr_mp3.h:527-535`) — a full scan — but for an MP3 without a Xing/Info header this firmware
  *already* does a full scan at track open, inside `drmp3_get_pcm_frame_count()`
  (`mi_decode.c:95`, `dr_mp3.h:4586-4618`), and throws the result away. Computing 256 seek points as a
  by-product of that existing pass costs no second read. For VBR files with a Xing TOC the positions
  come from the header instead.
- **FLAC's table competes for internal RAM.** `dr_flac` reads the file's SEEKTABLE into the same
  allocation as its decoded-samples buffer (`dr_flac.h:8218-8220`), and `mi_decode.c:66` passes no
  allocation callbacks, so that allocation is plain `malloc`. With `CONFIG_SPIRAM_USE_MALLOC=y` (as
  here, `sdkconfig.sunton:1875`) a plain `malloc` is served from internal RAM and falls back to PSRAM
  only when internal is exhausted — the opposite of the resampler's explicit SPIRAM-first request
  (`mi_resample.c:82-94`). Worth fixing if FLAC rewind matters.

What is missing is not a flag: `mi_dec` has no seek-table field and `mi_dec_close()` frees only its
scratch, so a table would leak (`mi_decode.c:20-31`, `:125-139`); and no position accessor is exposed
even though all three decoders track one (`dr_mp3.h:414`, `dr_flac.h:666-668`).

## 6. What a seek would break

The seek itself is the small part. This list is why the feature is a product change and not a
one-function change; each item was found by tracing the pipeline, not by testing.

- **The position.** `S.track_frames` is a pure accumulator; rebasing it is the fix, but it is also what
  feeds `elapsed_ms` (`mini_icecast.c:1117-1118`), the bar's permille, the pack fills and the reel
  geometry (`cassette.c:220-223`, `:264-268`), and the MCP `player_status` output
  (`mcp.c:752-754`). A seek that forgets it puts a 90-second resume back at zero on the panel.
- **The 1024-frame chunker.** `S.chunk_n` deliberately survives track boundaries
  (`mini_icecast.c:645-661`). A seek that does not zero it emits up to 23 ms of pre-seek audio after the
  jump, and the 1024-chunk <-> 1152-frame mismatch (`mini_icecast.h:34-36`) means a seek can only be
  frame-accurate to within one chunk unless it cuts mid-chunk.
- **The burst ring.** It holds the last 500 ms and nothing can invalidate it — `mi_burst.h:20-50` has
  new/free/capacity/used/put/begin/next, no clear. A client connecting during a wind is served
  old-position audio first, then the jump (`mini_icecast.c:166-178`, `:466-481`). A `mi_burst_clear()`
  is part of the feature.
- **The client rings.** 10 s of audio each (`mini_icecast.c:60`), and a fixed latency, not elastic. The
  *wrong* way to fast-forward is to encode the skipped audio: `played_frames` jumps, `ahead` becomes the
  whole jump, one `vTaskDelay` silences the station, and a client pushed past 10 s is declared dead and
  closed (`mini_icecast.c:146-149`, `:213-216`). The right way emits a bed for the hold and rebases the
  position, so no client is ever pushed.
- **The park.** While a download owns the card the loop only calls `idle_pump()` and never touches the
  decoder (`mini_icecast.c:726`), so a seek must be queued until unpark.
- **`stream_file()`'s break conditions.** `S.skip || S.prev || S.play_pending` tears the decoder down
  (`mini_icecast.c:725-735`). A seek must not be a fourth break condition: it has to be polled inside
  the loop and tagged with the track identity (index + generation, like `play_pending`), or a seek aimed
  at track N lands in track N+1.
- **Publishing the target.** `skip`/`prev` are bare volatiles; a 32-bit seek target cannot be published
  as "pending + value" atomically. Use the lock, or publish value-then-flag with a serial.
- **Tests and docs.** `t06_status.c:118-144` pins that the position advances monotonically with the
  audio and restarts with a new track; a rewind breaks the first of those, so it becomes "advances
  between seeks". `t10_cassette.c:386-403` pins that joining a song mid-way is *not* a track change, and
  nothing covers a forward wind. `t12_transport.c` pins ordering only and survives. The MCP tool list is
  pinned at nine tools (`mcp_control_tests.py:92-97`, `sd-stream-server.md:453-459`), so a `player_seek`
  changes M2b. And `CONTEXT.md:57-59` plus `CONTEXT.md:156-163` (the drawer "do not command the live
  stream yet") both stop being true.

## 7. The cost, on this chip

### 7.1 An audible spool

Playing the program at N times speed is what "the music flying past" requires, and it is the one thing
that does not fit. To emit N seconds of program per second of stream time at a fixed 44.1 kHz output,
the decoder must produce N x 44 100 frames per second: **decode cost is linear in the spool speed**, and
so is card traffic. The resampler's kernel grows too — `mi_rs_new()` sizes it as `T = 2 * ceil(ZERO_X /
fc)` with `fc` the anti-alias cutoff (`mi_resample.c:72-75`), so decimating by N takes `T ~ 16.8N` taps,
and `MI_RS_MAX_TAPS` is 256 (`mi_resample.c:25-27`), which refuses the format outright from N = 16.

| spool | MP3 decode | kernel | resample | card read (128 kbps source) | encoder | total |
|---|---|---|---|---|---|---|
| 1x | 7.6 % | 18 taps | ~4 % | 16 KB/s | 41-53 % | 53-65 % |
| 2x | 15 % | 34 | ~7.5 % | 32 KB/s | 41-53 % | 64-76 % |
| 3x | 23 % | 52 | ~11.5 % | 48 KB/s | 41-53 % | 76-88 % |
| 4x | 30 % | 68 | ~15 % | 64 KB/s | 41-53 % | 86-98 % |
| 10x | 76 % | 170 | ~38 % | 160 KB/s | 41-53 % | 155-167 % |
| 20x | 152 % | 338 (refused) | — | 320 KB/s | 41-53 % | 193-205 % if the kernel were allowed |

Labels: decode is the published *micro-mp3* figure times N (ESTIMATE — dr_mp3 is a different decoder and
may be slower); resample is ARITHMETIC from ~4 % at 18 taps and scales with the tap count (2 channels x
T taps x ~5 FP operations per output frame, at ~1.5-2 cycles per operation, 44 100 frames/s); card read
is ARITHMETIC from 16 KB/s per 128 kbps; encoder is MEASURED.

So: **N <= 2 is comfortable, N = 3 is marginal, N >= 4 does not fit, and a cassette's real spool speed
is outside the table.** The program can be played at double speed as a "cue" filter, but not as a wind.

The card is the second wall, and the honest number is missing. The only measured figure is 250 KB/s
through the *download* path (`mini_icecast.h:120-124`), which is socket-bound, not a read ceiling; the
bus is SPI at 20 MHz (`board_esp32.c:172`), whose raw ceiling is a couple of MB/s. At 20x, 320 KB/s is
below the raw bus and above the measured download figure, so a gated 20x spool is plausible and
unproven — the experiment is §9.

### 7.2 A gated spool

Decode at 1x and play only one chunk in N, discarding the rest. Cost becomes 1x decode (7.6 %) whatever
N is; the audio is chopped into snippets separated by jumps rather than pitch-shifted, and it sounds
like a scan — the music stays at its own pitch, in the right order, with holes in it. A legitimate
effect (many players' fast-forward does this), but a different fiction from ADR 0011's, where the tape
visibly runs backwards.

There is one variant worth knowing: if the skip is done by `fseek` past frames rather than by
read-and-discard, the card cost falls with it — but dr_mp3 has no "skip without reading" API, so today
the skipped bytes are read. That is why §9's card measurement decides whether a 20x gated spool is
possible at all.

### 7.3 The recorded bed

This is the owner's "rerecorded mp3", and against this pipeline it is not a compromise — it is the right
shape, for four reasons.

1. **The seam already exists, with a precedent.** `idle_pump()` (`mini_icecast.c:669-683`) already
   fabricates PCM and hands it to the same `chunk_append()` the decoder feeds, so a park "joins the bed
   seamlessly instead of flushing and restarting the encoder". A wind bed is the same call with a
   different source: `chunk_append(asset + offset, n, NULL)`, with its frames excluded from the position
   the way `idle_frames` already is.
2. **The cost is a memcpy.** Pre-decode the asset once at boot into PSRAM and loop it: 2 s of 44.1 kHz
   stereo 16-bit is 353 KB of PSRAM, read at 176 KB/s while it plays — well under a per cent of a core,
   against the 11 % of a core that the per-sample tape FX measured before it was retired
   (`tape-fx-output-stream.md` §6.7). Do *not* stream-decode the asset per chunk: that would pay the
   decoder's 7.6 % on every wind for nothing.
3. **Flash is not the constraint, and there is a precedent for embedded art.** The app partition is
   4 MB and the image is 2.82 MB (`build_sunton/sd_stream_server.bin`), so a ~32 KB MP3 (2 s at
   128 kbps) embedded in the binary is free; the 16 MB flash also leaves ~12 MB unallocated by
   `partitions.csv`. The screen component already embeds a 45 KB JPEG rather than a 768 KB raw frame and
   decodes it once into PSRAM, for exactly this reason (`screen_display/CMakeLists.txt:15-22`). But
   `mini_icecast` lives under `portable/` and must compile on the host, so the recording should not be
   embedded there: pass the bed into `mini_icecast_start()` as `const int16_t *bed; size_t bed_frames;`,
   let the app layer own the asset, and let the host tier pass generated noise for its tests. The loop
   itself stays a memcpy in the portable layer.
4. **At a cassette's speed there is very little program left to reproduce** (§2.2). The recorded bed
   does not "miss" the music, because at that speed the music is mostly gone.

What the asset cannot do is respond to the *speed* of the wind. Two answers: loop one seam-matched bed
for the whole hold (a wind is a steady sound; a deck does not change speed much once spooling), or play
the loop through a cheap single-tap resampler so its pitch follows the holding speed.

Three details that decide whether it sounds like a machine or like a ringtone:

- **Wind and rewind are different sounds.** One is tape being pulled off a pack, the other spooled on,
  with the capstan and pinch roller turning the other way. Two recordings, or one recorded in both
  directions.
- **The seam.** A 2 s loop repeated for a 10 s hold is audible as a period; record 4-6 s, alternate two
  takes, or start each hold at a random offset in the buffer.
- **The level.** This is a machine in the room, not a UI blip. The station's idle bed sits at -40 dBFS
  (`mini_icecast.c:666`); a wind bed should be far louder than that, or it reads as a fault rather than
  as a transport.

The synthesised alternative — a noise source through one or two biquads plus a tone whose frequency
follows the rate — costs no memory and is responsive, but it is a sound design exercise rather than a
recording, and §3 says which parts have to be there.

## 8. Recommendation

**Stage 1 — a transport state, a recorded bed, one seek.**

- `mini_icecast_wind(int direction)` starts a wind, `mini_icecast_wind_stop()` ends it. While winding,
  the broadcast task emits the bed through `chunk_append()` and moves a virtual position at the design's
  spool rate (say 20x, §2.1), clamped to `[0, duration]`.
- On release, seek once to the virtual position and resume. Emit nothing for the skipped audio and never
  let the skip reach the client rings (§6). If the format has no seek table, build one from the scan the
  firmware already performs at open, or budget the brute-force decode across the hold — but do not do it
  inline and un-budgeted (§5).
- Rebase `S.track_frames` at the seek, zero `S.chunk_n`, and clear the burst ring (§6).
- Give the screen an explicit winding input with a sign, and drive the window's progress from `now_ms`:
  do not let the release's backward jump open a second eight-second rewind (§1.2).
- Add the long-press path in `cassette_buttons` and one intent out of the view; `prev`/`next` are
  untouched. A rewind that reaches 0 either clamps or falls through to `prev` — a product choice.

**Stage 2, optional — let the music go past, cheaply.** If the owner wants to hear the program travelling
rather than only the mechanism, add gated snippets *under* the bed: decode at 1x, play one chunk in N
(§7.2). ~8 % of a core, no new format support, and it sounds like a scan — so mix it low, because the bed
is what sells the fiction.

**Also cheap, and probably asked for next:** `S` as pause is the idle-bed path with a flag
(`mini_icecast.c:669-683` plus the `idle_frames` subtraction), about ten lines, and "stop" is pause plus
a seek to 0. The drawer is a transport; the other two buttons will want to work.

**Not recommended: true varispeed.** It is the only option that reproduces the pitch of a real spool, and
at the speed a cassette actually spools it costs more than the whole core (§7.1).

## 9. What still has to be measured

The note turns on numbers this repo does not have. In the order they would change the design:

1. **dr_mp3's decode cost on this board.** Every decode figure here is borrowed from micro-mp3
   (PacketVideo), not the vendored dr_mp3. Instrument `mi_dec_read()` the way `mi_enc_feed()` is
   instrumented and log it in the telemetry line; one soak closes the largest ESTIMATE in this note.
2. **The card's read throughput alone**, not through the download path: a raw `fread` at 20 MHz SPI. It
   decides whether a gated 20x spool is possible at all.
3. **The encoder's cost on a noise-like bed.** The bed is the synthetic-noise case; if it measures near
   53 % rather than 41 %, the wind's budget is 12 points worse than a track's.
4. **Seek latency per format, with and without a table** — `esp_timer_get_time()` around the seek, for
   an MP3 with no table, an MP3 with 256 points, a FLAC with a SEEKTABLE and a WAV. This is the number
   that says whether a seek can be inline or must be budgeted.
5. **The broadcast task's total utilisation** — encoder + decode + resample, not the encoder alone.
   `lag_ms` is already the instrument: a wind that drives `lag_ms` up is a wind that does not fit.

## 10. What this note does not decide

- **Hold or tap.** A tap that winds a fixed 10 s and a hold that winds while pressed are different
  gestures and different code.
- **What P and S do.** Play/stop means a paused broadcast, which is a larger product question than
  rewind even though the mechanism is small.
- **Whether the wind is audible to every listener.** It is: the station is one stream, and there is no
  per-client transport without a second encoder.
- **Whether rewind crosses tracks.** A cassette does (it goes onto the other side). This design can clamp
  at zero and leave crossing to `prev`.
- **What the wind sounds like.** §3 says which components have to be there and §7.3 says how to ship
  them; choosing the recording is a listening decision, not a measurement.
