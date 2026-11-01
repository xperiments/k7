# The front door is a shell of areas, with hash routes

ADR 0048 built the front door as one page with two **views**, and the card state chose between
them: card in → the player's view, card out → the analyser's, with a banner offering the switch
while a queue was running (front-door spec S2/S3). The same document wrote down, in R1, that
unknown paths are `404` because the page is one screen with no client-side routes.

A sidebar with four areas is not two views. Nothing is "the view" any more, and every one of the
nine destinations the owner listed needs an address it can be opened at, refreshed on and sent
to someone.

## Decision

- **One shell, four areas** — Card, Playlists, Media, Player — and the sidebar switches between
  them. The two-view vocabulary (**Prepare view**, **Player view**) is retired; the areas do not
  own state, they own routes.
- **Routes are hashes**: `#/process`, `#/playlists/:id`, `#/media/video/:id`. The device still
  serves one page, still answers `404` for every unregistered path, and needs no catch-all. A
  refresh and the back button work; a list can be linked.
- **The card state chooses the first area and nothing after that.** A cold load lands on Player
  with a card in, Process with none. Afterwards the sidebar is the user's. The S3 banner is
  gone, and what it protected moves inside the app: the measuring area stays mounted while it is
  not shown, so navigating can no longer destroy a running queue.
- **No router dependency.** A hash listener and a matcher over a table of routes; the shell
  budget is documented in `web/README.md` and a router would be the largest thing in it.

## Why hashes and not paths

The alternative is real: `pushState` paths read better and never show a `#`. It costs a firmware
catch-all route, a new ambiguity (which path is a page, which is a missing file), and the host
test that proves unknown paths `404` — T1 — would have to change to prove something weaker. The
hash buys the same user-visible behaviour for nothing on the device, and the device is the half
that cannot be fixed by redeploying.

## What this costs

The address bar shows a `#`. A route is not shareable outside the LAN anyway, because the page
is served by an appliance with no name in public DNS (ADR 0049).
