# HE-AAC verified from a live stream (2026-10-16)

The codec recommendation rests on a mechanism claim that is easy to state and rarely
demonstrated: **HE-AAC streams advertise themselves as plain AAC-LC in their framing, and
only the decoder can recover the doubled sample rate.** This was checked against real
bytes from a real station rather than taken from documentation.

## The capture

`http://ice2.somafm.com/groovesalad-64-aac` — SomaFM's `aacp` (HE-AAC) tier at 64 kbps,
fetched with `Icy-MetaData: 1` and a browser `User-Agent` (required, see
`04-live-stream-observations.md`). Response headers: `Content-Type: audio/aac`,
`icy-metaint: 45000`.

The body parse, walking ADTS frames from offset 0:

```
bytes: 338159   first4: fff95c80
ADTS sync at offset 0
  MPEG version      : MPEG-2
  profile bits (2b) : 1  (0=Main, 1=LC, 2=SSR, 3=LTP)  -> LC
  sampling freq idx : 7 -> 22050 Hz
  channel config    : 2 (stereo)
  frame length      : 382 bytes
```

Walking 122 consecutive frames by their own `frame_length` field, the parse stayed
self-consistent and did not desynchronise:

```
walked 122 consecutive ADTS frames, cursor=45344
  MPEG-2 profile=1(LC) 22050Hz chan_cfg=2: 122 frames  (all frames identical in format)
```

## What this proves

1. **The transport is plain ADTS.** No container, no `moov`, no length prefix — a
   decoder just needs to find `0xFFF` and read 7-byte headers, and the frames chain by
   their own length field. This is the simplest possible framing for a streaming
   decoder and validates the "ADTS push API" requirement.
2. **The station calls it `aacp` but the header says profile = LC.** The 2-bit profile
   field has no value for HE-AAC; SBR is an extension carried inside the AAC payload
   and applied *implicitly* by the decoder. A decoder that does not implement SBR will
   happily decode this stream **at 22050 Hz** — half rate — producing audio that is
   slow and band-limited. This is the concrete failure mode the library must avoid,
   and it is invisible in every header.
3. **The core is 22050 Hz, so the intended output is 44100 Hz.** This is a *second*
   reason the resampler seam matters: the rate a player finally outputs (44100) is not
   the rate any header advertises (22050), and it is only known after the first frame
   is decoded. The library cannot fix its I2S clock from the HTTP response alone.
4. **Independent cross-check of the metadata interval.** The frame walk reached
   `cursor=45344`, and the stream's advertised `icy-metaint` is `45000`. The first
   metadata block therefore sits at 45000 and the walk overran it by 344 bytes — i.e.
   the frame-walking estimate and the header's interval agree to within a fraction of
   one 382-byte frame. Two independent mechanisms (ADTS frame lengths and the ICY
   interval) corroborate each other, which is good evidence both were read correctly.
   It also demonstrates the real demuxer problem: **frame boundaries and metadata
   boundaries do not align**, so a decoder must be fed from a byte stream with the
   metadata regions excised, not asked to walk the socket directly.

## Consequences for the library design

- The decoder backend interface must be **push-shaped and frame-agnostic**, and must
  report the **output** sample rate (post-SBR), not the header's.
- HE-AAC support is not optional polish; for SomaFM (48/51 channels at 32–64 kbps) it
  is the difference between playing the low-bandwidth tiers and playing them wrong.
- Because SBR rate doubling is invisible up front, the code must be able to
  (re)configure the I2S clock and re-create the resampler **after** the first decoded
  frame — the output path cannot be fixed at connection time.
- Channel count must be taken from the stream (here `chan_cfg=2`), never assumed; other
  HE-AAC v2 stations signal a mono core plus Parametric Stereo.

## Not verified

- The actual *audible* result of decoding core-only (the "half-rate/slow" description is
  the standard consequence of SBR inference, but no decoder was run here to hear it).
- Whether SomaFM's 32 kbps tier uses **HE-AAC v2 (PS)** with a mono core — that would
  show `chan_cfg=1`. Only the 64 kbps tier was captured.
- Whether any station in this sample uses MPEG-4 ADTS rather than this stream's MPEG-2.

## Reproduction

```bash
UA="Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 Chrome/120 Safari/537.36"
curl -s --max-time 10 -A "$UA" -H "Icy-MetaData: 1" \
     http://ice2.somafm.com/groovesalad-64-aac -o /tmp/soma64.bin
python3 - <<'PY'
d=open('/tmp/soma64.bin','rb').read()
rates=[96000,88200,64000,48000,44100,32000,24000,22050,16000,12000,11025,8000,7350]
i=n=0
while i<len(d)-7 and n<200:
    h=d[i:i+7]
    if not(h[0]==0xFF and (h[1]&0xF0)==0xF0 and (h[1]&0x06)==0): break
    fl=((h[3]&0x3)<<11)|(h[4]<<3)|((h[5]>>5)&0x7)
    if n==0: print("MPEG2" if h[1]&0x08 else "MPEG4", "profile", (h[2]>>6)&3,
                   rates[(h[2]>>2)&0xF], "chan", ((h[2]&1)<<2)|((h[3]>>6)&3))
    i+=fl; n+=1
print("frames:",n,"cursor:",i)
PY
```
