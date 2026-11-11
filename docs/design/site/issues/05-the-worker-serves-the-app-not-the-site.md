# 05 — The worker serves the App and stays out of the Site's way

**What to build:** the delivery in [`docs/specs/k7-site.md`](../../../specs/k7-site.md) §1 and §5.

- `web/public/sw.js` stops treating `/` as its shell. It precaches the App's document and the
  App's assets, answers a navigation for `/app` and `/app.html` only, and lets every other
  navigation go to the network. The Site is never served from a cache-first path — it is the page
  that names a firmware version, and a cached copy would offer a release the origin may have
  replaced.
- The Site registers `/sw.js` itself, so an install offered on `/` has the worker Chrome
  requires even when the visitor has never opened `/app`.
- `web/vercel.json`: `/` and `/app.html` on `no-cache` with `sw.js` and the manifest; `/assets/`
  stays `immutable`; the `/app` rewrite from ticket 01 is the only rewrite.
- The worker's cache version changes, so an old installed worker drops its `/`-as-shell cache
  instead of serving the Site under the App's name.

**Blocked by:** 01.

**Status:** pending

- [ ] With the network off, `/app` loads from the cache and the App renders what it can offline.
- [ ] A navigation to `/` goes to the network, even while a worker is installed.
- [ ] The Site registers the worker before its install control is offered.
- [ ] `curl -sI` shows `no-cache` on `/`, `/app.html`, `/sw.js` and the manifest, `immutable` on
      `/assets/`, and no `COOP`.
- [ ] An existing install survives the change: the icon opens `/app`, not the Site.

**Files:** `web/public/sw.js`, `web/vercel.json`, `web/src/pwa.ts`, the Site's entry (the
registration).

**Tests:** k7-site.md T8; the existing worker tests.

**Source:** ADR 0078.

## Comments
