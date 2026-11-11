# 02 — A firmware release is a version, a manifest and three committed parts

**What to build:** the release in [`docs/specs/k7-site.md`](../../../specs/k7-site.md) §4 and
k7-app.md's install section.

- `VERSION` at the repository root, semver, starting `0.5.0`. `CMakeLists.txt` passes it as
  `PROJECT_VER`, so `esp_app_get_description().version` — which the panel's About row already
  prints — carries it.
- `./tools/build.sh sunton release`, a new action: refuse a dirty tree; refuse a `VERSION` with no
  matching `v<version>` tag at `HEAD`; build; copy `bootloader.bin`, `partition-table.bin` and
  `sd_stream_server.bin` to `web/public/firmware/<version>/`; write `web/firmware.json` with the
  version, commit, IDF version, and each part's file, offset, size and sha256; print them. It
  commits nothing, tags nothing and deploys nothing.
- `web/firmware.json` is what the Site imports: the version, the offsets and the hashes have no
  other source.
- CI's `web` job proves `firmware.json` against the committed bytes: every part present, size and
  sha256 equal, and `VERSION` equal to the manifest's version. A missing part fails the build
  (k7-deploy.md D15).

**Blocked by:** none.

**Status:** pending

- [ ] `VERSION` is `0.5.0`; a `sunton` build reports it in the boot log and the About row.
- [ ] `release` on a dirty tree stops before building, with the reason.
- [ ] `release` on an untagged or mismatched `VERSION` stops before building, with the reason.
- [ ] A released `0.5.0` writes `web/firmware.json` and the three parts under
      `web/public/firmware/0.5.0/`, all committed.
- [ ] The guard fails on a hash mismatch, a missing part and a `VERSION`/manifest disagreement.
- [ ] `firmware.json` carries no absolute path and no build-directory path.

**Files:** `VERSION` (new), `CMakeLists.txt`, `tools/build.sh`, `web/firmware.json` (new),
`web/public/firmware/0.5.0/` (new, committed), the web CI guard (new test or script), the
`web` CI job.

**Tests:** k7-site.md T6, T7.

**Source:** ADR 0079.

## Comments
