# Server-side code review: `resources/` as it actually is

Findings from reading the existing server code directly, independent of the external
research in this directory. These are code-level facts about `resources/`, not research
claims, so they carry no external citations — every line reference is to the file itself.

Scope: `mini_icecast.c/.h`, `mi_decode.c/.h`, `mi_resample.c/.h`, `main.c`,
`CMakeLists.txt`, `sdkconfig.defaults`.

## What the server actually is

- **Input formats: MP3, FLAC, WAV only** — chosen by file extension (`mi_decode.c:62-80`,
  `strcasecmp` on the extension), then decoded to PCM by dr_libs.
- **Wire format: raw PCM inside a WAV container.** `wav_header()` (`mini_icecast.c:181-191`)
  writes `RIFF`/`data` sizes as `0xFFFFFFFF`, and `handle_new()` sends
  `Content-Type: audio/wav` (`mini_icecast.c:224`).
- **No encoder exists anywhere in the tree.** So "should we support AAC?" is not a
  modification — it is a new subsystem.
- **ICY is implemented as an emitter**: `Icy-MetaData: 1` opt-in, `icy-metaint`
  advertisement, `StreamTitle='...';` blocks interleaved into the PCM body
  (`mini_icecast.c:75-109`, `221-228`).

## Bandwidth arithmetic (the number that decides the encoder question)

At the default station format (stereo 16-bit, 44 100 Hz):

| Format | Bytes/s | Mbit/s | MB/hour per client |
|---|---|---|---|
| PCM 44.1 kHz stereo16 | 176 400 | 1.41 | 635 |
| PCM 48 kHz stereo16 | 192 000 | 1.54 | 691 |
| PCM 32 kHz stereo16 | 128 000 | 1.02 | 461 |
| PCM 22.05 kHz stereo16 | 88 200 | 0.71 | 318 |
| MP3 128 kbit/s | 16 000 | 0.13 | 57.6 |
| MP3 192 kbit/s | 24 000 | 0.19 | 86.4 |
| Opus 64 kbit/s | 8 000 | 0.06 | 28.8 |
| FLAC (≈60 % of PCM typical) | ≈105 800 | ≈0.85 | ≈381 |

N clients at 44.1 kHz PCM: 1 → 1.41, 2 → 2.82, 3 → 4.23, 4 → 5.64 Mbit/s. (The code caps
`max_clients` at 2 by default, with a hard `MAX_CLIENTS 4`.)

**Reading:** ~1.4 Mbit/s per client is comfortable for a LAN and for two or three Wi-Fi
clients, which is what the code's own comment claims. The binding constraint is not Wi-Fi
throughput but the **635 MB/hour per client**, which is what matters for a remote listener
or a metered connection. So an encoder is justified by *bandwidth and reach*, not by CPU
headroom — and that tradeoff is exactly what `s2-encoder-feasibility.md` quantifies.

**Verified against Espressif's own measurements** ([ESP-IDF Wi-Fi docs, v5.5.1,
`docs/en/api-guides/wifi.rst`](https://raw.githubusercontent.com/espressif/esp-idf/v5.5.1/docs/en/api-guides/wifi.rst),
"ESP32-S3 Wi-Fi Throughput" table): the S3 achieves **TCP TX 20 Mbit/s "Air In Lab"** and
83 Mbit/s in a shield box (TCP RX 20 / 73 Mbit/s; UDP TX 30 / 98 Mbit/s; raw 802.11 130 Mbit/s).
Against a 20 Mbit/s realistic TCP TX ceiling, raw PCM is **~7 % of the link per client**, so
the hardware can in principle carry ~14 clients at 1.41 Mbit/s each — far beyond the code's
own cap of 4. **This is the decisive fact for the encoder question: bandwidth is not why this
server would need an encoder.** The reasons would be reach over the internet and data volume
(635 MB/h/client), not Wi-Fi capacity or CPU.

Cheaper lever worth noting before reaching for an encoder: **dropping the station format**
from 44.1 kHz stereo to 22.05 kHz mono cuts bandwidth by 4× (176 400 → 44 100 B/s, well
under MP3 128k) with no encoder at all, at the cost of fidelity. For talk radio that is
often the right answer. The resampler already supports any output rate.

## Concurrency and robustness defects (server-side)

1. **The accept loop is blocked by client handling — new connections are refused while one
   client is being set up.** `accept_task` calls `handle_new(fd)` directly in its loop
   (`mini_icecast.c:272-275`), and `handle_new` does a blocking header read with a **2 s**
   `SO_RCVTIMEO` (`mini_icecast.c:195`), then sends the 44-byte header and the whole burst
   under the broadcast lock with a **1 s** send timeout (`mini_icecast.c:233-247`). A client
   that connects and sends nothing occupies the accept loop for up to 2 seconds; two such
   clients in a row make the server effectively unresponsive. The standard fix is to hand the
   new socket to a per-connection task or to a single-threaded poll loop, so authentication
   and burst delivery never run on the accepting thread.
2. **`listen(ls, 2)` backlog of 2** (`mini_icecast.c:267`) compounds the above — very little
   room for pending connections while the loop is stalled.
3. **`max_clients` is documented 1..4 in the header** (`mini_icecast.h:22`) **but silently
   coerced to 2** if unset or out of range (`mini_icecast.c:395`), while the array is sized
   `MAX_CLIENTS 4`. A caller asking for 4 with any invalid value gets 2 with no diagnostic.
4. **Client disconnects are only detected on a failed `send`** (`mini_icecast.c:168-172`).
   With a 400 ms send timeout that is acceptable, but there is no handling of the case where
   a client half-closes and the send buffer still drains.
5. **`mi_decode.c` mixes the decoder's channel count into a fixed stereo output by
   indexing** (`mi_decode.c:102-105`): `out[2*i+1] = d->tmp[i*d->ch + (d->ch > 1)]`. Correct
   for mono and stereo, and it keeps the first two channels for >2, as documented — but note
   a >2-channel file is silently downmixed by *discarding* channels rather than mixing, which
   can drop content. Worth a documented decision rather than an accident.
6. **`title_ver` correctness depends on initialisation order.** In a static struct
   `S.title_ver` starts at 0 and `S.title` is empty, so a client connecting *before* the
   radio task sets a title gets `c->title_ver = 0` and therefore the "title unchanged" path,
   emitting a single `0x00` byte rather than a title. That is harmless but means the first
   metadata block can be empty; the client-visible consequence is "no title yet", which is
   the correct semantic (length byte 0 = keep previous title), so this is fine — just worth
   knowing when writing tests.

## Header-format bug worth fixing (affects interoperability)

`handle_new` emits **`"icy-name: %s\r\n"` with a space after the colon**
(`mini_icecast.c:225`), whereas real Icecast emits `icy-name:value` with **no** space. Both
are legal HTTP, and a permissive parser accepts both — but a client (or a test) that is
written against this server may be accidentally strict about the space and then fail against
real stations. Since this server is the obvious loopback fixture for a client-side parser,
that fixture should emit canonical no-space headers, or deliberately emit both forms.

## Stack and memory notes

- `radio_task` is created with a **24 KB** stack (`mini_icecast.c:407`) explicitly for
  "MP3 decoder scratch". That is the decoder's working set, in internal RAM — correct.
- The resampler's `mi_rs_t` holds a `(NPH+1)*T*4`-byte table plus history, malloc'd with no
  heap caps, so it lands in internal RAM by default. At the server's common upsampling rates
  that is ~9 KB, so it is not a problem here — but the unbounded `double row[256]` overflow
  documented in `../00-audit-of-resources.md` is reachable if `out_hz` is configured far below
  the input rate (e.g. a 44.1 kHz file to an 8 kHz station gives T=102, fine; to 3 kHz it
  overflows). A server that lets `out_hz` be configured must clamp or assert.
- The burst ring is sized from `out_hz * 4 * burst_ms / 1000 + CHUNK_BYTES + 2`
  (`mini_icecast.c:397`), i.e. **88 KB** at the 500 ms default, allocated with a
  `MALLOC_CAP_SPIRAM` preference and an internal-RAM fallback. Reasonable; note it scales
  linearly with `out_hz`, so a 48 kHz station with a 2 s burst wants ~384 KB.

## Build blockers (unchanged from the main audit)

`CMakeLists.txt` declares `INCLUDE_DIRS "include"` and `PRIV_INCLUDE_DIRS "third_party"`,
but neither directory exists and the headers sit flat beside the sources. `mi_decode.c`
includes `dr_mp3.h`, `dr_flac.h`, `dr_wav.h`, which are absent from the tree entirely. As
committed, this component does not compile. For a *library* the missing pieces are
`README.md`, `LICENSE`, and `idf_component.yml`, all of which the registry requires.
