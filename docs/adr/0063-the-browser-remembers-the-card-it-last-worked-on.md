# The browser remembers the card it last worked on

The App's card-level stores are keyed by a **Scope** — `mark:<id>` or `fingerprint:<sha256>`
([card-mark.md](../specs/card-mark.md) §2, M1) — and the scope was set in a module variable,
once, at the writable pick. It started as `local` on every page load.

That made everything the App keeps for a card unreachable on the next visit. The pick cannot
be repeated once the card is in the player: no directory handle, no `k7.card` reader in the
firmware yet (D1), nothing in `/api/status` that names the card. So a reload left the covers
and the **Last image** filed under `mark:<id>` and the page asking for `local`:

- Media showed no albums and no artists, having measured the card the day before.
- Every album route answered "this album is not in this browser".
- Playlists refused to send, because a list change is patched onto the Last image
  ([ADR 0051](./0051-a-playlist-lives-in-the-snapshot.md)) and that image had become invisible.

## Decision

- **The last scope is kept in `localStorage`** (`k7.scope.v1`), beside the Settings, and read
  when `card/session.ts` first loads. A pick writes it; nothing else does.
- **Only the two shapes this module produces are adopted** — `mark:<id>`, `fingerprint:<hex>`,
  or `local`. An unrecognised value is not a card this build can name, so it is ignored rather
  than used to point the stores somewhere.
- **Card-scoped reads re-run when the scope changes.** `useCardScope()` is the scope as a
  React value; Media's index and the provider's Last image and cover count follow it, and the
  provider drops the previous card's image before loading the new one — a list change must
  never be patched onto the wrong card's bytes.

## What this is not

It is not the device naming the card. That stays D1: `status` reporting a `mark`, which would
let the App *check* that the card it remembers is the one in the player. What is restored is
the weaker, honest fact the views already claim: **the card this browser last worked on**. A
card with a different mark replaces it at the next pick, and a card re-formatted without one
gets a fresh fingerprint, so the old scope is orphaned rather than misapplied.

## Consequences

- **A scope outlives the card's presence.** If the card in the player is not the one
  remembered, Media shows the remembered one's names over the device's paths. That is visible
  in the empty states and in `Presence`, which filters cache rows the device does not list —
  but until D1 it cannot be detected, only described.
- **`localStorage` may be refused.** Then the scope holds for the page's life and the next
  visit is unscoped, which is the behaviour the App shipped with — degraded, not broken.
- **Orphaned scopes accumulate.** `clearMedia` exists for a "Forget this card" action the
  Settings area does not offer yet; the mark's spec asks for one (§P3).


## Update

The premise above — "nothing in `/api/status` that names the card" — is no longer true. The
device reads `k7.card` at mount and reports its id as `mark` in `/api/status` (card-mark.md
D1). When the player is reachable and names its card, Media reads that card's scope instead of
the browser's last pick; the remembered scope is still the offline and unmarked-card answer.
