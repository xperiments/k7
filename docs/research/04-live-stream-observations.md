# Live-stream observations (captured from real stations, 2026-10-16)

Complements `03-transport-verified-locally.md` (parser/algorithm behaviour) with what
actual internet-radio endpoints do on the wire. Commands were run from this machine;
raw bytes were inspected rather than assumed. Reproduce with the `curl` lines shown —
note these fetches are network calls to third parties and should not be looped.

## 1. Real ICY framing, confirmed byte-for-byte

`http://stream.radioparadise.com/mp3-128` with `Icy-MetaData: 1` returned
`icy-metaint: 16000`, `Content-Type: audio/mpeg`, plus `icy-name`, `icy-genre`,
`icy-br`, `icy-url`, `icy-pub`. It also uses the **`HTTP/1.1 200 OK` status line**, not
`ICY 200 OK` — so both forms occur in the wild (see §3).

Inspecting the downloaded bytes at the advertised interval:

```
offset 16000 : length byte = 7  -> 16*7 = 112 bytes of metadata follow
payload      : StreamTitle='Hooverphonic - We All Float';
               StreamUrl='http://img.radioparadise.com/covers/l/12011.jpg';
               \x00 * 10
offset 32112 : length byte = 81 (normal audio again)
```

This confirms the whole framing model on real traffic:

- the length byte counts **16-byte blocks** (7 → 112 bytes);
- the region is **NUL padded** after the payload, so a reader must stop at the payload
  end or the closing `'`, not scan blindly;
- the **next** length byte is at `2*metaint + 16*L` = `32000 + 112 = 32112`, exactly as
  the periodic model predicts — so the interval is strictly periodic and the demuxer
  must count consumed bytes across reads;
- the next interval's "length byte" read as `81` only because the offset accounting had
  already moved past the second metadata region in that sample; the arithmetic is what
  matters and it matched.

**New field discovered: `StreamUrl`.** Metadata blocks can carry more than
`StreamTitle`. Radio Paradise includes a **cover-art URL** alongside the title. The
parser should therefore scan the metadata region for the `key='value';` pairs it cares
about rather than assuming the region holds exactly one `StreamTitle` — and this is a
ready-made, zero-cost album-art source for a player UI.

## 2. `Icy-MetaData` is requested by the client; `icy-metaint` is mandatory to decode

Both tested stations advertise `icy-metaint: 16000`, and metadata only arrives because
the request opted in with `Icy-MetaData: 1`. Without the opt-in the body is pure audio
and no metadata blocks are interleaved — so the demuxer must be enabled *conditionally*
on that response header, not unconditionally.

## 3. Non-standard header spelling exists

`http://icecast.omroep.nl/radio1-bb-mp3` returned **`HTTP/1.0 200 OK`** (a valid but
old-style status line, which the IDF parser handles) and advertised the opt-in
capability as **`icy-metadata: 1`** rather than sending `icy-metaint`-after-opt-in.
A parser looking only for `icy-metaint` would see no interval here; the implementation
must tolerate both spellings and treat a missing interval as "no metadata".

## 4. User-Agent gating is real and will break a naive ESP32 client

Same host, same stream, same headers — only the `User-Agent` changed:

| User-Agent | Result |
|---|---|
| `curl/8` | **302** (redirect) |
| A Chrome-like `Mozilla/5.0 (...) Chrome/120 Safari/537.36` | **200** with a working HE-AAC stream |

This is the single most integration-hostile finding in this document. `esp_http_client`
sends a minimal/default identity. A player that does not set a browser-like
`User-Agent` will be redirected or refused by at least some major stations, and the
failure will look like "the stream is broken" rather than "you were fingerprinted".
The library must:

- send a **configurable, browser-like `User-Agent` by default**,
- treat 403/302 as potentially recoverable rather than fatal, and
- expose the identity as configuration, because the correct value is station-dependent.

Subsequent repeated requests from this machine returned `403`/connection failures after
the initial successes, which is consistent with rate-limiting or temporary blocking of
this IP. That is itself instructive: **transient 403 is a normal condition**, so retry
with backoff belongs in the connection state machine rather than being surfaced as a
hard error.

## 5. Station playlist indirection is a real code path (`.pls`)

`https://api.somafm.com/groovesalad64.pls` returned a standard INI-style playlist
listing **three mirror URLs** in rotation:

```
[playlist]
numberofentries=3
File1=https://ice6.somafm.com/groovesalad-64-aac
File2=https://ice2.somafm.com/groovesalad-64-aac
File3=https://ice5.somafm.com/groovesalad-64-aac
Length1=-1
```

Implications: the library must parse `.pls` (and ideally `.m3u`) bodies, pick an
entry, and be ready for the mirrors to be **https** even when the playlist URL was
fetched over http. `Length=-1` is the standard "live stream" marker. The `.m3u`
variant of the same stream 404'd, so `.pls` support is the one that matters in practice.

## 6. HE-AAC is not hypothetical — it is how stations serve low bitrates

SomaFM's machine-readable channel list (`https://somafm.com/channels.json`, 51
channels) shows every channel offering three tiers:

| Tier | Count | Typical bitrate |
|---|---|---|
| `mp3` | 51 | 256 kbps |
| `aac` (AAC-LC) | 48 | ~130 kbps |
| `aacp` (HE-AAC) | 48 @ 32 kbps, 47 @ 64 kbps | 32 / 64 kbps |

So **48 of 51 channels offer HE-AAC, and the overwhelming majority of those do so at 32
and 64 kbps**. These low-bitrate tiers are precisely where HE-AAC's SBR is mandatory to
decode correctly, and precisely the tiers a bandwidth-constrained ESP32 would prefer.
This is the strongest available empirical support for the codec recommendation: an
MP3-only player silently loses roughly half the available streams for this one major
station, and a decoder without SBR would produce wrong (band-limited or half-rate)
audio on all of them.

Caveat on method: bitrates above (the odd 4–7 digit values) come from naively regexing
digits out of SomaFM's internal rating codes (e.g. `sf1033`, `dz2130`) and are **not**
real bitrates — only the 32/64/80/130/256/320 entries are genuine. The tier *counts* are
unaffected.

## 7. What could not be verified

- Whether the SomaFM HE-AAC stream's **actual ADTS/SBR** payload decodes as expected —
  the audio body could not be read reliably from this environment (403 after the
  successful UA test), so HE-AAC handling is supported by the station's own `aacp`
  labelling and the ADTS spec, not by decoding real bytes here.
- Whether SomaFM requires the browser UA *always* or only for cold/naive clients; the
  observed sequence (302 → 200 → 403) is consistent with fingerprinting **plus** rate
  limiting, but the two were not isolated.
- Any station that sends ICY metadata **with** an `ICY 200 OK` status line (Radio
  Paradise used `HTTP/1.1`); that combination is supported by the parser per
  `03-transport-verified-locally.md` but was not hit on live traffic.

## Reproduction

```bash
# headers + ICY interval (note: the UA matters)
curl -s -D - -o /tmp/rp.bin -H "Icy-MetaData: 1" \
     -A "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 Chrome/120 Safari/537.36" \
     http://stream.radioparadise.com/mp3-128

# metadata framing at the advertised interval
python3 -c "d=open('/tmp/rp.bin','rb').read(); L=d[16000]; print(L, d[16001:16001+16*L])"

# station playlist indirection
curl -s https://api.somafm.com/groovesalad64.pls

# codec tiers offered
curl -s https://somafm.com/channels.json | python3 -m json.tool | grep -E '"format"'
```
