# The Site owns the root, and the App moves to /app

The App was the whole of `https://k7.xperiments.dev`: `/` served it, and the first thing a
stranger met was the Local Network Access gate — a wall built for someone who already owns a
player. Two acts now need that origin and cannot live behind the gate: **installing** the App,
which only the App's own origin can do, and **flashing** a bare board, which a person with no
player must reach. So the root becomes the **Site** — the product page, the flash, the install —
and the App moves to `/app`.

## Decision

- `https://k7.xperiments.dev/` is the **Site**; `https://k7.xperiments.dev/app` is the **App**.
  Both documents come from one artifact and one deploy; the origin, its certificate and its CSP
  do not move ([ADR 0057](./0057-the-app-lives-at-a-public-origin.md) stands).
- The manifest keeps `id: "/"` and gains `start_url: "/app"` and `scope: "/app"`. Installing from
  either document opens the App; a link to `/` stays a website in a tab.
- The App's routes become hash paths under `/app` — `/app#/process`, `/app#/media/album/:album`.
- The device's doorbell follows: `FRONT_DOOR_LAUNCH_URL` becomes
  `https://k7.xperiments.dev/app#/player`.
- The App's gate points the other way: *No player yet? Flash one*, to `/` in a new tab.
- The Site is public and ungated, and it makes no request to the player: the device is written
  over USB, not the network.

## Considered options

- **A page at `/install` beside the App.** One new route, nothing moves — and the stranger who
  types the product's name still meets the gate, not the product. It is also an address to know
  rather than a front door to arrive at.
- **A first-run page inside the App.** The App already has a first screen; making it the product
  page would mean the installed app opens on something that is not the app, and the gate would
  have to become two gates.
- **A separate host for the Site.** Installability is per origin: a page on another hostname
  cannot install this App, and the page that exists to install would be a link. It also buys a
  second certificate and a second deploy for one product.
- **Moving the product to `experiments.dev`.** Not this project's domain, and a new origin is a
  new app to Chrome: every existing install would be left behind.

## Consequences

- **Every reference gains `/app`**: the device's 302, the README's address, bookmarks, the
  Playwright and HTTP tests, and the App's own deep links. A generated `sdkconfig` keeps the old
  doorbell until it is reconfigured; a build that is not reconfigured sends browsers to the Site.
- **The worker changes shape.** It precaches the App's document and answers navigation for
  `/app` only; the Site is never served from a cache-first path.
- **A9, A10 and A12 are amended.** `start_url`/`scope` are `/app`; the Safari *Add to Home Screen*
  sentence is retired, because installing an App that cannot render is the dead button A10
  exists to prevent. `id` is unchanged, so no install is orphaned.
- **The Site is now the page that must stay honest** about a Chrome-only App and a
  desktop-Chromium-only flash. That is the Site spec's job
  ([`k7-site.md`](../specs/k7-site.md)).
