# Virtual local SD cards in the browser

Can the K7 app let a person compose a "virtual SD card" — a card-shaped tree of a chosen
capacity into which they drag folders of music, measure the files, and keep `k7.db` + the card
mark + `k7.covers` up to date — to be written onto a real SD later? This note reads the primary
sources (WHATWG/WICG specs, the WHATWG File System Standard, MDN, Chromium source and developer
docs) and states what the platform actually allows. It does not design the feature; it lists the
hard constraints a design must respect.

Every claim is cited inline to its primary source. `spec §n` means the section of the named
specification.

## 1. OPFS: a real, writable, nested file tree — yes

The Origin Private File System is the one place the platform gives a site a private file tree it
can write without any permission prompt. It is specified as the **bucket file system** in the
[File System Standard](https://fs.spec.whatwg.org/#origin-private-file-system) and surfaced in
MDN's [Origin private file system](https://developer.mozilla.org/en-US/docs/Web/API/File_System_API/Origin_private_file_system)
guide. The File System Access spec describes it as "a bucket file system which websites can get
access to without having to first prompt the user for access" ([File System Access, introduction](https://wicg.github.io/file-system-access/#introduction)).

The exact API surface:

- **Root.** `navigator.storage.getDirectory()` resolves to a `FileSystemDirectoryHandle` that is
  the OPFS root ([MDN `getDirectory()`](https://developer.mozilla.org/en-US/docs/Web/API/StorageManager/getDirectory);
  [File System Standard](https://fs.spec.whatwg.org/#dom-storagemanager-getdirectory)).
- **Nested directories.** `dirHandle.getDirectoryHandle(name, {create: true})` creates child
  directories; `dirHandle.getFileHandle(name, {create: true})` creates files
  ([MDN OPFS](https://developer.mozilla.org/en-US/docs/Web/API/File_System_API/Origin_private_file_system#manipulating_the_opfs_from_the_main_thread)).
  So a `music/Artist/Album/01.mp3` tree is a first-class thing in OPFS.
- **Writing.** `fileHandle.createWritable()` returns a `FileSystemWritableFileStream`; `write(data)`
  accepts an `ArrayBuffer`, `TypedArray`, `DataView`, `Blob`, or string, and changes are flushed
  when the stream is `close()`d ([MDN `createWritable()`](https://developer.mozilla.org/en-US/docs/Web/API/FileSystemFileHandle/createWritable);
  [MDN `write()`](https://developer.mozilla.org/en-US/docs/Web/API/FileSystemWritableFileStream/write)).
  Writing a large audio file is therefore a sequence of `write()` calls on one stream — streaming,
  not one in-memory buffer.
- **Byte-level access.** In a worker, `createSyncAccessHandle()` gives synchronous in-place
  read/write/truncate/flush ([MDN OPFS](https://developer.mozilla.org/en-US/docs/Web/API/File_System_API/Origin_private_file_system#manipulating_the_opfs_from_a_web_worker)).
- **Not user-visible.** OPFS is origin-private; the files are "not visible to the user like the
  regular file system", and "you cannot expect to find the created files matched one-to-one" on
  disk ([MDN OPFS](https://developer.mozilla.org/en-US/docs/Web/API/File_System_API/Origin_private_file_system)).

**Persistence and capacity.** OPFS is "subject to browser storage quota restrictions, just like
any other origin-partitioned storage mechanism", its usage is reported by
`navigator.storage.estimate()`, and clearing site data deletes it
([MDN OPFS](https://developer.mozilla.org/en-US/docs/Web/API/File_System_API/Origin_private_file_system)).
The spec draws the same line from the other side: "**Other than files in a bucket file system**,
files written by this API are not subject to storage quota" ([File System Access, "Filling up a
users disk"](https://wicg.github.io/file-system-access/#filling-up-disk)) — i.e. OPFS writes *are*
quota-limited, while writes into a picked directory are not.

The budget surface is the Storage API:

- `navigator.storage.estimate()` resolves to `{usage, quota}`, both "imprecise" and `quota` "a
  conservative approximation" ([MDN `estimate()`](https://developer.mozilla.org/en-US/docs/Web/API/StorageManager/estimate);
  [Storage Standard](https://storage.spec.whatwg.org/#dom-storagemanager-estimate)).
- `navigator.storage.persist()` requests persistent storage and resolves `true`/`false`; it is
  best-effort and "the browser may or may not honor the request"
  ([MDN `persist()`](https://developer.mozilla.org/en-US/docs/Web/API/StorageManager/persist);
  [Storage Standard](https://storage.spec.whatwg.org/#dom-storagemanager-persist)).
- `FileSystemWritableFileStream.write()` throws `QuotaExceededError` "if the new size of the file
  … exceeds the browser's storage quota" ([MDN `write()`](https://developer.mozilla.org/en-US/docs/Web/API/FileSystemWritableFileStream/write)).

**Conclusion for the feature.** OPFS can hold a real `music/` tree with streaming writes, and
`estimate()`/`persist()` give a quota to budget against. But a "32 GB" or "128 GB" card is a
**soft cap the app enforces**, not a guarantee: the hard ceiling is the origin's storage quota
(commonly far below those sizes), it is evictable unless `persist()` is granted, and a budget is
a `usage + next file ≤ cap` check plus a `QuotaExceededError` catch, never an allocation.

## 2. File System Access: picking, saving, dropping, and the write-permission model

The write-capable entry points to the *user-visible* file system are the picker factories in
[File System Access §3](https://wicg.github.io/file-system-access/#local-filesystem):

- `showDirectoryPicker({mode: 'readwrite'})` returns a `FileSystemDirectoryHandle`; `mode` defaults
  to `"read"` ([spec §api-showdirectorypicker](https://wicg.github.io/file-system-access/#api-showdirectorypicker);
  [MDN](https://developer.mozilla.org/en-US/docs/Web/API/Window/showDirectoryPicker)).
- `showSaveFilePicker()` returns a single `FileSystemFileHandle` whose `readwrite` permission "should
  be granted" at pick time ([spec §local-file-system-permissions](https://wicg.github.io/file-system-access/#local-file-system-permissions);
  [MDN](https://developer.mozilla.org/en-US/docs/Web/API/Window/showSaveFilePicker)).
- `DataTransferItem.getAsFileSystemHandle()` returns a `FileSystemFileHandle` or
  `FileSystemDirectoryHandle` for a dropped item ([spec §drag-and-drop](https://wicg.github.io/file-system-access/#drag-and-drop);
  [MDN](https://developer.mozilla.org/en-US/docs/Web/API/DataTransferItem/getAsFileSystemHandle)).

The permission model is two modes, `"read"` and `"readwrite"`, with
`FileSystemHandle.requestPermission({mode})` as the upgrade mechanism
([spec §permissions](https://wicg.github.io/file-system-access/#permissions),
[§api-filesystemhandle-requestpermission](https://wicg.github.io/file-system-access/#api-filesystemhandle-requestpermission)).

**The key asymmetry.** The spec grants `read` at pick time for the pickers, and `readwrite` for
`showSaveFilePicker`, as "the user intending to grant" access ([spec §local-file-system-permissions](https://wicg.github.io/file-system-access/#local-file-system-permissions)).
Drag-and-drop has **no equivalent grant**: the `getAsFileSystemHandle()` algorithm only constructs a
handle and carries an open issue about sensitive directories, but never sets a permission state
([spec §drag-and-drop](https://wicg.github.io/file-system-access/#drag-and-drop)). Consequently a
handle not returned by a factory "is also likely to return `"prompt"`" for read
([spec §api-filesystemhandle-querypermission](https://wicg.github.io/file-system-access/#api-filesystemhandle-querypermission)).

**Is a dropped folder read-only forever?** Against the current spec, the repo's claim in ADR 0010
and its code comments — "a dropped folder is read-only for good", "a dropped handle's permission
cannot be raised to `readwrite`" — is **stronger than the specification states**. The spec defines
`requestPermission({mode:'readwrite'})` generically for *any* handle and its permission-request
algorithm contains no drag-and-drop prohibition; the grant is delegated to "request permission to
use" ([spec §2.2, §2.3.2](https://wicg.github.io/file-system-access/#permissions)). There is no
normative "read-only forever" rule.

What is true, and what makes the repo's stance the right operational rule for a Chrome-only app
([ADR 0067](../adr/0067-chrome-only-direct-to-the-device.md)):

- A dropped handle grants **read**, not write. The only picker that pre-grants `readwrite` is
  `showSaveFilePicker`; `showDirectoryPicker({mode:'readwrite'})` is the write path for a directory
  ([spec §local-file-system-permissions](https://wicg.github.io/file-system-access/#local-file-system-permissions)).
- Chromium (the only shipping implementation) gates write permission behind a content-setting guard
  in `ChromeFileSystemAccessPermissionContext::CanObtainWritePermission` /
  `GetWriteGuardContentSetting`
  ([Chromium source](https://chromium.googlesource.com/chromium/src/+/main/chrome/browser/file_system_access/chrome_file_system_access_permission_context.cc)),
  and its developer guidance treats dropped items as read-only with the picker as the write path
  ([Chrome for Developers, File System Access](https://developer.chrome.com/docs/capabilities/web-apis/file-system-access)).

**Consequence.** A design should treat a dropped source folder as read-only and never attempt to
write `k7.db`, the mark or `k7.covers` back into it. But that is irrelevant to composing a *virtual*
card: the dropped folder only needs to be **read** and **copied** into the writable virtual-card
store (§1), which is exactly what a drop grants.

## 3. Authoring a FAT32/exFAT image in the browser: no first-party path

There is **no browser API** that builds a FAT32 or exFAT filesystem image, and no spec to cite
because none exists. The File System Access API exposes only file and directory handles on the
user's filesystem ([spec §2](https://wicg.github.io/file-system-access/#files-and-directories)); it
has no volume-formatting, no raw-sector, and no disk-image surface. The web platform has no
`mkfs.fat` equivalent and no primitive that emits a valid boot sector / FAT / root-directory
structure.

The closest supported alternatives, in order of realism:

1. **Write individual files into a picked SD directory** — `showDirectoryPicker({mode:'readwrite'})`
   on the mounted SD, then `createWritable()` per file (§2). This is what the app already does for
   `k7.db` and is the only fully supported way to get bytes onto a real FAT32 card: the OS's own
   FAT driver performs the on-disk format. It requires the card to be mounted and picked, so it is
   a "materialize the tree onto an SD" step, not an image.
2. **Download the tree as one archive.** There is no native "download a folder"; the
   [Compression Streams API](https://developer.mozilla.org/en-US/docs/Web/API/Compression_Streams_API)
   provides only gzip/deflate streams ([Compression spec](https://compression.spec.whatwg.org/#compression-stream)),
   **not** the ZIP container format, so a `.zip` requires a third-party library.
3. **Hand-rolling a FAT32 image in JavaScript.** A FAT32 image is, in principle, just bytes, so a
   page *could* generate the BPB, two FATs, root directory and cluster chains and hand the result to
   `showSaveFilePicker()` for the user to `dd`/flash later. This is **not a first-party path**: it is
   a hand-authored binary format with no spec, no validation, and real correctness risk (LFN entries,
   cluster allocation, the 4 GB single-file limit), and exFAT adds licensing and greater complexity.
   Nothing on the platform checks it, and an ESP32 FatFs mount would be the only judge.

The honest answer to "can the browser author a FAT32 `.img` an ESP32 SD reader would mount" is:
**no first-party path exists.** If a one-file artifact is wanted, the supported route is a
downloadable archive of the composed tree (option 2), or the app writes the tree directly onto a
picked SD (option 1) and lets the OS format/allocate it.

## 4. Writing a raw block device: the web platform has none

The web platform has **no API that writes a raw block device**, so a "virtual card" can never be a
bare image flashed in the browser, and must always be a tree the user materializes onto an SD by
copying files.

- **WebUSB** explicitly blocklists Mass Storage. The spec's "Protected interface classes" table
  lists class code `0x08` (Mass Storage) as an interface the UA must not expose, alongside Audio,
  HID, Hub, Smart Card, Video, and Audio/Video Devices ([WebUSB spec, "Protected interface classes"](https://wicg.github.io/webusb/#usbinterface-interface)).
  The rationale is stated in the spec: these are classes "for which such high level APIs exist and
  provide greater protection … than low level access through this API would" (same section). A USB
  SD reader is Mass Storage, so it is hidden from WebUSB.
- **Web Serial** exposes serial (CDC/ACM) ports only, not block devices, and provides no
  sector/SCSI surface ([Web Serial API](https://developer.mozilla.org/en-US/docs/Web/API/Web_Serial_API)).
- The **File System Access API** writes files on a mounted filesystem, never raw sectors (§2).

Conclusion: there is no in-browser route from a `FileSystemFileHandle`/`FileSystemDirectoryHandle`
(or any other handle) to the raw sectors of a physical SD. Materialization onto a real SD is a file
copy — done by the OS file manager, or by the app writing into a `showDirectoryPicker({mode:'readwrite'})`
directory that is the mounted SD.

---

## Consequences for the design

Hard constraints a design must respect:

1. **A virtual card is an OPFS tree, never a `.img`.** Build `music/` + `k7.db` + `k7.card` +
   `k7.covers` under `navigator.storage.getDirectory()`; there is no block-device or
   filesystem-image API (§3, §4).
2. **Capacity is a soft cap, not an allocation.** Enforce "32 GB" as a usage budget against
   `navigator.storage.estimate()` and catch `QuotaExceededError`; the real ceiling is the origin
   quota, which is smaller, imprecise, and evictable unless `navigator.storage.persist()` is granted
   (§1).
3. **Dropped source folders are read-only; copy, never write back.** Compose by reading the drop
   handle and streaming the bytes into OPFS. Do not attempt to write the mark or `k7.db` into a
   dropped folder — `showDirectoryPicker({mode:'readwrite'})` is the only write path (§2).
4. **The byte contracts are unchanged.** `k7.db`, `k7.card` and `k7.covers` are the same
   `snapshot-format.md`, `card-mark.md` and `coverArchive.ts` bytes, written through
   `createWritable()` into the virtual card's OPFS tree. Only the backing store differs.
5. **Materialization onto a real SD is a copy the user performs** (file manager) **or a picked-SD
   write** (`showDirectoryPicker({mode:'readwrite'})`). There is no "flash this card" in the browser
   (§4).
6. **The picker/drop surfaces are Chromium-only.** `showDirectoryPicker`, `showSaveFilePicker` and
   `getAsFileSystemHandle` are not in Firefox/Safari ([MDN browser support on each page](https://developer.mozilla.org/en-US/docs/Web/API/Window/showDirectoryPicker#browser_compatibility));
   the app is already Chrome-only by ADR 0067 and gates on these capabilities (`k7-app.md` A6/A7).
   OPFS itself is Baseline and available more widely.
7. **Large-file writes should be chunked.** Stream audio into OPFS with successive
   `FileSystemWritableFileStream.write()` calls (or `createSyncAccessHandle` in a worker), not one
   giant in-memory buffer (§1).
