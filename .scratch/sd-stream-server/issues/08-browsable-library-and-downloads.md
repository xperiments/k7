# 08 — The card's library is browsable and downloadable

**What to build:** Opening the device's root address in a browser shows the library on the card —
each track's name and size, and what the live stream is playing right now. Clicking a track
downloads that track's original, unmodified bytes. Comparing the downloaded file with the file on
the card shows them identical.

Downloads deliberately do not match the live stream's format: the stream exists to be reachable by
anything, while a download retrieves what the card actually holds. A client that wants the lossless
original gets the lossless original.

Two behaviours protect the live stream, because the broadcast and any download read from the same
card: malformed or non-audio names are refused before touching the filesystem, and only one
download may proceed at a time. A second simultaneous request is declined rather than queued, so
the stream never stalls behind someone else's large file.

**Blocked by:** 07 — The HTTP surface is separated from the broadcast.

**Status:** done (host tier). T9b and the FATFS half of T6/T7 have not been run on the board (no card, no board).

- [ ] Opening the root address lists every audio track in the music directory exactly — no extras, nothing missing — with each entry's size matching the file's real size.
- [ ] The page states which track the live stream is currently playing.
- [ ] A requested track is returned byte-identical to the file on the card. Verified with a byte comparison, on the host and on the board.
- [ ] A returned file's length and media type are correct, so a client sees a download that can be resumed or sized rather than an endless response.
- [ ] Names containing a path separator, containing a parent-directory reference, beginning with a dot, or carrying a non-audio extension are refused without any filesystem access. Requesting a parent-directory escape never returns file contents.
- [ ] A second simultaneous download is declined with a service-unavailable response. The first download continues, and the live stream keeps playing without audible interruption.
- [ ] The download path reads in bounded chunks and honours a send timeout, so a client that stops reading cannot hold the card indefinitely.
- [ ] The card's decoder and the download path never access the card concurrently in an unserialised way.

Spec: `docs/specs/sd-stream-server.md` §6.2 (I1–I3), §6.3 (R7a–R7d), §5 D5, D6, §7 (A1, A2), §9 T6, T7, T8, T9, T9b.
Decision: ADR 0001.

## Comments

- [x] The index lists exactly the audio tracks with their real sizes: `host/tests/check_index.py` compares the rendered page against the directory, in both directions, and the suite passes against the 11-file corpus.
- [x] The page states the current title and links the stream.
- [x] Downloads are byte-identical: every fixture is downloaded and `cmp`-compared (11 files, all identical).
- [x] Length and media type are correct: `Content-Length` from `stat()` and `audio/mpeg`, `audio/flac`, `audio/wav` by extension.
- [x] Names with a path separator, a parent reference, a leading dot, a non-audio extension, an embedded `%00`, or a decode that would overflow the buffer are refused before any filesystem call; a traversal attempt never returns file contents (11 guard cases, all 400 or 404).
- [x] A second simultaneous download gets 503 while the first continues byte-intact (asserted with a deliberately slow reader).
- [x] Downloads read in bounded 16 KB chunks and honour the same 400 ms send-timeout discipline as the stream, so a client that stops reading is dropped rather than pinning the card.
- [x] Exactly one download at a time: a mutex taken with a zero timeout, so a 40 MB FLAC cannot queue behind another and starve the broadcast.

Two implementation notes: the guard also rejects a name whose decoded form contains a NUL (`tone.flac%00.txt` used to serve `tone.flac`), and `max_files` on the mount was raised from 4 to 6 because the file-descriptor accounting is two directory handles (the broadcast's scan and the index) plus the decoder's file plus one download — exactly 4 with no headroom.

**Board bug, found and fixed: the two library failures shared a page.** With the card
mounted but no `/sdcard/music`, the index answered "No card mounted" - the wrong problem,
on the one surface that exists to say which problem it is. They are now distinct pages:
`No card mounted` (quoting the mount error) and `No music directory` (naming the directory
and the `opendir` error). The broadcast log distinguishes them too
(`cannot open /sdcard/music: No such file or directory` once, rather than only "no
playable files").

Verified on the device: `GET /` returns 200, 722 bytes, ~20 ms, three times in a row, with
the device staying up.

**Board run of T13: documented.** A wrong-case request on this card returns `200`:

```
note  T13 wrong-case request for TONE-44100.FLAC -> 200
```

FATFS as configured here resolves names case-insensitively, so `/music/TONE-44100.FLAC`
reaches `tone-44100.flac`. The specification asks for the behaviour to be documented
rather than asserted, so the suite prints it rather than checking it - and it is board-only
by construction, because macOS would give the same answer for the wrong reason.
