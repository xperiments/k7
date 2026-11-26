# Playlist format

**Status:** accepted · **Updated:** 2026-11-16

A **Playlist** is one file on the card, at `k7.playlists/<id>.k7list`. It is written by the app
when the card is in a computer, and by the device when the card is in the player
([ADR 0094](../adr/0094-the-device-writes-the-lists-it-is-given.md)); nothing else writes it
([ADR 0095](../adr/0095-a-playlist-is-a-file-on-its-card.md)). This document defines the
directory, the file and the values. It says nothing about how a list is played: the broadcast's
rules are the stream server's, and the path resolution is `library_find`'s.

## 1. Conventions

- **UTF-8, no BOM, LF line endings.** A reader accepts CRLF and accepts a final line without its
  LF; a writer emits LF after every line, including the last.
- **One value per line**, and no blank lines and no comments. A key is the text up to the first
  space; the value is everything after it, verbatim.
- **`id`** is the list's 64-bit identity written as exactly 16 lowercase hex characters — the same
  string `POST /api/playlists/select` takes and `k7.select` carries. Zero is not a list.
- Numbers are unsigned decimal, no sign, no leading zeros except for `0` itself.

## 2. The directory

`k7.playlists/` at the card root, beside `k7.db`, `k7.card`, `k7.select` and `k7.covers`. FAT32
folds case, so every name here is matched folded.

- **0 to 64 files.** A file is a list when its name is 16 hex characters and `.k7list`; anything
  else in the directory is ignored, never read and never deleted.
- **The name is the id.** `<id>.k7list` — a rename of the list edits the file, it never moves it.
- **No other directory.** The device creates this one and nothing else, and a card without it has
  no lists rather than an error.

## 3. The file

```text
K7LIST 1
id 00000000000000a1
name Late night 8A
created 1700000000
rows 2
A/B/one.mp3
A/B/two.mp3
```

| Line | Content |
|---|---|
| 1 | `K7LIST ` and the format version, `1` |
| 2 | `id ` and the list's 16 hex characters, which must equal the file name's |
| 3 | `name ` and the list's name: 1 to 63 bytes, no CR and no LF, never a leading or trailing space |
| 4 | `created ` and Unix seconds (UTC), informational |
| 5 | `rows ` and the number of paths that follow, `0` to `8192` |
| 6… | one **Snapshot path** per line, in play order: the Track's location below the card's `music/` directory, `/`-separated, no leading slash, 1 to 1024 bytes ([`snapshot-format.md`](./snapshot-format.md) §6) |

A list with no rows is valid and plays nothing; selecting it is the command's `409`, not a broken
file. The same path may appear more than once: a list may play a Track twice.

## 4. Reading

A reader validates the whole file before it uses any of it: a list is either valid or it is not,
and a half-understood list is never played.

It refuses, and says which line:

1. a first line that is not `K7LIST 1`; an unknown version is refused, never guessed;
2. a key out of order, an unknown key, or a missing one;
3. an `id` that is not 16 hex characters, is zero, or disagrees with the file name;
4. a name that is empty, over 63 bytes, or carries CR or LF or an edge space;
5. a `created` or a `rows` that is not a decimal number, or a `rows` above 8,192;
6. a row count that disagrees with the lines that follow, or a line after them;
7. a path that is empty, over 1 KB, absolute, or containing `..`.

A directory with more than 64 list files is refused as a whole: the device answers without lists
rather than with an arbitrary 64.

## 5. Writing

- **Temporary, then renamed.** The writer creates `<id>.k7list.tmp`, writes the whole file,
  closes it and renames it onto `<id>.k7list` on the same volume. A failure at any point leaves the
  previous file intact; a card that fills mid-write has the old list, not half of a new one.
- **This order, these bytes.** The six header lines in the order above, one path per line, LF after
  every line, no trailing spaces. The same list written twice produces byte-identical files. A
  `PUT` stores the body it is given once that body has been read back and validated; the app
  writes this canonical form, and a client that writes another line ending still hashes the same.
- **The name is trimmed** before it is written; a name that is empty after trimming is refused at
  the boundary rather than written as an invalid file.

## 6. Limits

| Limit | Value | Why |
|---|---|---|
| Lists in the directory | 64 | the index the device holds is bounded, and 64 is what the image's list area already allowed |
| Rows in one list | 8,192 | the same figure the image's list area allowed |
| Bytes in a name | 63 | a line is read into a fixed buffer, and the name is the longest field |
| Bytes in a path | 1,024 | a FAT32 path below `music/` is far shorter; the bound is the reader's |

## 7. Fingerprint

`GET /api/playlists` reports a **fingerprint** per list, and the app compares it with its own to
say whether the card's copy is current. It is FNV-1a 64 over the list's content, not over its
bytes, so a file re-saved with CRLF line endings hashes the same:

```
hash = FNV1a64( name + "\n" )
hash = FNV1a64( hash, decimal(created) + "\n" )
for each row, in order: hash = FNV1a64( hash, path + "\n" )
```

written as 16 lowercase hex characters, zero-padded. A writer that stores the same name, creation
time and rows produces the same fingerprint, which is what makes "edited, the player has the older
copy" a comparison rather than a guess.

## 8. Versioning

`K7LIST 1` is the only version this project reads or writes. There is no compatibility shim: an
unknown version is refused, the list is not shown, and the app never writes a version it does not
know. A list is a few hundred paths and is cheap to re-author, so there is no reason to guess at a
half-understood file.
