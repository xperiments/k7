# Phase 2 — The soul of the product

**Read with:** [`01-project-understanding.md`](./01-project-understanding.md). Citations are the
same convention: `path:line` for facts, **INFERENCE** and **PROPOSAL** marked in the text.

---

## 1. The emotional core

### The feeling, stated badly first

The obvious answer is nostalgia, and it is wrong. Nostalgia is a feeling about the past; this
product is a feeling about **the present tense of your own library**. The repo gives three pieces of
evidence that point away from nostalgia and towards something quieter and more useful.

**It re-imposes scarcity on purpose.** A broadcast cannot be paused or seeked; every client hears
the same moment (`README.md:1272-1273`). One device per LAN, because the hostname is fixed
(`README.md:1264-1265`). One selected list at a time, written to the card as a file
(`docs/specs/playlist-format.md`). The MCP endpoint is read-only by default and a control build is a
separate, flagged build (`components/portable/mcp/src/mcp.c:19-22`). None of that is a limitation
that survived engineering; it is a rule set the product chose. In a world of infinite shuffle, this
thing has **edges**.

**It defers to your judgement.** The analyser reads a tagged BPM, key, mood or energy and *trusts it*
rather than overwriting it — "it can overwrite a value a human checked in Rekordbox, Serato or Mixed
In Key" is the reason given for not re-measuring (`docs/adr/0009-tagged-fields.md`). The assistant
writes filters, never results, and never sees a track, an artist or a path
(`docs/specs/k7-app.md:496-503`). The card carries a name because "the mark answers *is this the card
I prepared?*" (`docs/specs/card-mark.md:6-11`). Every one of those is a decision to **not decide for
you**.

**It makes the collection an object again.** The card is the tape. A list is a file on the card. A
label is printed on the deck's screen. You can hand someone the card (`docs/specs/label-templates.md:3-8`;
`docs/adr/0095`).

### The core, in one line

**The quiet relief of a collection that is finished, chosen and yours — and the small ceremony of
making a tape out of it.**

Three feelings sit inside that sentence, and they are not the same audience:

| Feeling | What it is | Where the product earns it |
|---|---|---|
| **Relief** | "I know what I have, and it is not infinite." | a bounded card, a measured library, a list that ends |
| **Agency** | "Nothing here decides for me." | no recommendations, trusted tags, filters not results, read-only by default |
| **Craft** | "I made this, and I understand it." | a documented build, a label editor, an MCP surface, MIT firmware |

INFERENCE: the first two are the *buyer's* feelings; the third is the *builder's*. A campaign that
leads with craft sells to hundreds. A campaign that leads with relief and agency can sell to
thousands — but only if there is an object to buy, which today there is not (§5 of Phase 1).

### What need it actually serves

Not "I want a cassette". The need is: **I want my music to have a shape again, and I want to stop
being served.** A tape had two sides, an order, and an end. This product gives those back without
giving back the hiss, the wow, the 90-minute dubbing or the box in the loft.

PROPOSAL: write that as the internal brief, and never as copy. It is the argument the copy has to
win, not the sentence the copy says.

---

## 2. The tension it resolves

The user's framing — old form factor vs new capability — is right, but the interesting version is
sharper than "retro shell, modern guts". Both halves of this product are *extremes*:

| The form factor | The capability |
|---|---|
| linear, one side at a time | infinite storage on a card the size of a stamp |
| no seek, no skip worth the name | a network with a seek bar in every client |
| ~45 minutes, then you turn it over | 20 000 tracks, 4 MiB of index, 96 kHz sources |
| mono, hissy, fragile | a 44.1 kHz / 256 kbps MP3 encoder in half a core |
| one deck, one room | a LAN, four listeners, an MCP endpoint |

The usual retro product resolves this by putting the new capability *inside* the old shell and
hiding the seams — a Bluetooth speaker in a wood grain box. **K7 does the opposite: it makes the new
capability obey the old constraint.** The broadcast is a side. The list is the tape. One device per
LAN is one deck in one room. The assistant writes a filter, not a queue, because a tape has no
recommendations.

PROPOSAL: that inversion is the product's intellectual property, in the marketing sense. It is the
one thing a competitor with the same board, the same encoder and the same case cannot copy in a
weekend, because it is a *decision*, not a feature.

### The tension, stated as a sentence

> The most constrained music format ever invented, rebuilt with the least constrained technology —
> and then deliberately constrained again.

That is the line to keep in the internal brief. It is not a tagline (too long, too clever); it is
the reason the tagline can be short.

---

## 3. Positioning

### Statement A — the inherited one, tightened

> **For people who own their music and miss the object it came in, K7 is a cassette-shaped deck that
> turns a card into a tape and your network into a radio station. It plays to the speakers already in
> the room, and it cannot recommend anything: it only plays what you chose.**

*Rationale:* it keeps the sentence the live site already publishes (`web/src/site/Site.tsx:42-44`),
keeps the ownership axis, and fixes the one thing that sentence gets wrong — "a player" implies
sound, and the device has none. "Plays to the speakers already in the room" is true, and it turns
the missing amplifier from a gap into the argument: *use the good speakers you own.*

### Statement B — constraint as the offer

> **K7 is a music player for people who are tired of being served: one tape, one room, one order, no
> algorithm. Put a card in and it plays exactly what you put on it — the reels turn, the tape ends,
> and nothing suggests what to hear next.**

*Rationale:* it leads with the ennui of streaming rather than with ownership, and it makes the
product's strictest limits (no seek, one list, one room) read as the promise. Strongest against a
subscription. Weakest for a design buyer, who is not tired of anything and wants to see the thing.

### Statement C — the instrument, for the build audience

> **K7 is an open cassette deck: MIT firmware, a browser app that measures your files, and a screen
> that turns a card into a tape. Every number in it was measured, every gap is written down, and you
> can change any of it.**

*Rationale:* it sells the project's real differentiator — the documentation discipline — to the
audience that reads before it buys. It is also the most defensible statement in the set, because
every clause is checkable in the repo. It cannot carry a consumer launch: "MIT firmware" is a reason
to trust, not a reason to want.

### Recommendation

**A, with B's second half.**

> **For people who own their music and miss the object it came in, K7 is a cassette-shaped deck that
> turns a card into a tape and your network into a radio station. It plays to the speakers already in
> the room, and it cannot recommend anything. It only plays what you chose.**

Why:

1. **It is continuous with what is already live.** The site, the meta description and the hero all
   carry this sentence today (`web/index.html:10`; `Site.tsx:42-44`). Changing it costs the small
   amount of search presence the product has, and buys nothing.
2. **It contains the objection.** "A cassette player with no speaker?" is the first question any
   careful reader asks. The clause "plays to the speakers already in the room" answers it inside the
   positioning, before it becomes an objection — and it is the honest answer, not a dodge.
3. **It draws the line the whole campaign rests on.** "It cannot recommend anything" is a claim no
   competitor in the smart-speaker category can make, it is verifiable in the firmware, and it is
   the sentence a reader remembers.
4. **It does not require the object to exist yet.** A deck that plays to the room is a *system*, not
   an appliance. That is exactly the current state, so the positioning does not expire when the case
   ships — it strengthens.

C is not a positioning statement; it is the **proof layer** underneath A. Use it on the page for
builders and in the Show HN post, never in the hero.

---

## 4. Taglines

Numbers are not claims; none of these asserts a spec.

| # | Line | Why it works | Risk |
|---|---|---|---|
| 1 | **Your music, on a cassette.** | Plain, ownable, says the promise without claiming a tape. Already the page title (`web/index.html:15`). | Slightly flat as a campaign line; it is a *label*, not an argument. |
| 2 | **No recommendations. Just the tape you made.** | Two beats: the enemy, then the object. The second half is literally true (a list is a file on the card). | "Just" can read as defensive; keep it for the campaign, not the packaging. |
| 3 | **The card is the tape.** | The best one-line explanation of the whole metaphor, and it is factually exact — the card carries the mark, the lists and the music. | Needs the product in frame or it reads as a riddle. |
| 4 | **One tape per LAN.** | Specification as poetry: it turns the fixed-hostname limitation into a sentence about intimacy (`README.md:1264-1265`). | Only lands with an audience that knows what a LAN is. |
| 5 | **It ends. That is the point.** | The boldest idea in the set — a defence of finitude, which is what a side actually is. | One step from a perfume ad; it needs a film or a photograph to earn it. Never use it on a button. |

**Recommendation:** #2 as the campaign line, #1 as the standing site title. They do different jobs
and should not compete: #1 is what the page is *called*, #2 is what the campaign *argues*. #3 is the
caption on the product shot, #4 is the line for the builder audience, #5 is a poster — one asset,
once, if the film is good enough.

Do not use #5 in a paid placement. It is the sort of line that reads as profound in a still and as
pretentious in a feed.

---

## 5. Brand voice

### The voice in one line

**A technician who loves music, telling you what is true, including the parts that are not
flattering.**

The repo has already written most of this down, and I am not going to improve on it. What follows is
the rule set extracted from the best strings in the tree, with samples to copy from.

### Do

| Do | Sample (verbatim) |
|---|---|
| **Be specific where a marketer would be vague.** Name the cable, the number, the file. | "A USB-C cable that carries data, not just power." — `web/src/site/Flash.tsx:174-175` |
| **Say what will not happen, then what will.** | "That is all. Nothing is written until you confirm." — `Flash.tsx:182-183` |
| **Prefer the limitation to the promise.** | "The stream is a broadcast: it cannot be paused or seeked, and every client hears the same moment." — `README.md:1272-1273` |
| **Let a sentence end on a small, concrete noun.** | "It only plays what you chose." |
| **Treat the reader as competent.** Explain, do not persuade. | "K7 needs Chrome on a computer … an installed icon would open a closed door." — `web/src/site/Install.tsx:59-63` |
| **Be dry about failure.** A failure is a next step, not an apology. | "The live stream keeps playing while the page cannot ask what it is playing." — `web/src/ui/areas/Player.tsx:114-115` |
| **Use the product's own nouns.** Tape, card, list, deck, track — never "content", "experience", "journey". | "Every list is a file on its card." — commit `58c1f8d` |

### Don't

| Don't | Because | Instead |
|---|---|---|
| "Revolutionary", "game-changing", "seamless", "effortless" | The product is none of these and the repo never claims it | "It fits on a card." / "One cable, one press." |
| "Relive the golden age of cassettes" | It does not play cassettes; the repo's own refusals table already bans this (`docs/research/marketing/campaign.md:77-78`) | "A cassette for the music you already own." |
| "Studio quality" / "hi-fi" / "audiophile" | `shine` is below LAME and the repo says so (`README.md:1274-1276`) | Say nothing about fidelity. Show the EQ and the room instead. |
| "Smart", "AI-powered", "intelligent" | Ask writes filters, never results; 10/12 on a twelve-sentence spike (`docs/specs/k7-app.md:496-500`) | "Describe the set. It writes the filter. You keep the taste." |
| "Works with everything" | It is Chrome, on a computer, on your LAN | "Chrome on a computer. Your network, not someone else's." |
| "Your music, anywhere" | It is one device on one LAN with no auth (`README.md:23-39`) | "One tape, one room." |
| Exclamation marks, emoji, "🚀", "launching today" | The voice has never used one in 683 commits | A full stop. |
| "We believe music should be…" | It is a manifesto sentence about a device that costs nothing to try | Show the device, then the specs. |

### The voice test

Before publishing a sentence, apply the repo's own test: **could the README have said it?**

- "The reels turn at the speed the tape would." → yes. `README.md` says it, in more words.
- "It brings the magic of mixtapes back to life." → no. Nothing in the tree says "magic", and the
  glossary avoids "mixtape" as a product noun (`web/CONTEXT.md:337`).
- "One tape per LAN." → yes, and it is funnier than the README.

### Two registers, one voice

- **The site register** — short, declarative, second person, no adjectives. Close to the current
  `Site.tsx`.
- **The build register** — long, first person plural or impersonal, numbers in tables, footnotes for
  what was not measured. Close to the README.

PROPOSAL: keep them visibly different. The site should never read like the README, and the build log
should never read like the site. The *values* are the same; the *density* is not.

---

## 6. What this product is not

Anti-positioning, written as refusals. Each one is a campaign decision, not a preference.

1. **Not a cassette player.** It does not accept, play, record, rewind or erase a cassette. The tape
   is a card. Saying otherwise is the fastest way to lose the only audience that would defend it.
2. **Not a hi-fi component.** No DAC, no amplifier, no speaker, no fidelity claim. It is a source.
3. **Not a streamer.** No Spotify, no Tidal, no account, no cloud, no per-track download. The
   absence is the product: nothing is licensed to you, so nothing can be revoked.
4. **Not a smart speaker.** It has no assistant in the room and no recommendation engine. Ask writes
   filters on your computer; the deck has no microphone, no wake word and no opinion.
5. **Not a multiroom system.** One device per LAN, fixed hostname, two devices collide
   (`README.md:1264-1265`).
6. **Not a nostalgia brand.** No sunset filters, no film grain, no "remember when". The palette is
   near-black and orange, the type is Jura at 300, the motion is mechanical
   (`docs/specs/k7-look.md:98-136,526-547`). It looks like an instrument, not a memory.
7. **Not a finished consumer product** — today. No case, no BOM, no certification, no OTA, no price.
   It is a build, a firmware and an app. Marketing it as "available" would be a false claim and the
   repo's own compliance file says so (`docs/research/marketing/compliance-2026-11-03.md:113-120`).
8. **Not secure, and not for the internet.** No authentication, no TLS, plain HTTP on port 80, a
   default hotspot password. The README's warning is a brand asset — keep it visible
   (`README.md:23-39`).
9. **Not a general-purpose media box.** MP3, FLAC and WAV in; MP3 out. No video, no Ogg, no AAC, no
   Opus (`README.md:1252-1253`).

INFERENCE: item 7 is the one that hurts, and it is the one that has to be said out loud in the
campaign's first sentence. Every other refusal makes the product look *principled*. This one just
makes it look unfinished — which it is, and which the audience that matters will respect more than a
preorder button.

---

## 7. Where I disagree with the existing plan

The prior campaign (`docs/research/marketing/campaign.md`) is unusually good and I am building on it,
not replacing it. Four places I would change the order of things.

1. **"The object" cannot be the first pillar, because there is no object.** The plan's three pillars
   are the object, the tape and the room (`campaign.md:284-290`), and its week −3 is a two-day
   shoot of the device (`campaign.md:795`). There is nothing to photograph: no case, no BOM, no
   photograph (`docs/specs/k7-site.md:19-20`). PROPOSAL: swap the first pillar for **the screen** —
   the thing that actually exists, is genuinely beautiful, and can be filmed today off-device
   because it is SVG in a browser (`web/src/ui/cassette-screen.tsx`).
2. **The €0 route is forecast below its own stop rule.** §17.4 forecasts 15–40 qualified builders
   (`campaign.md:1100`) while §14 stops the campaign under 40 (`campaign.md:886`). That is a plan
   designed to fail its own test. PROPOSAL: redefine the goal for the first 90 days as *one
   artefact that travels* (a film, a post, a build log) rather than a signup count, and set the
   threshold after the first two weeks of real numbers.
3. **The order of audiences should be B, then A and C — not A and C with B underneath.**
   `campaign.md:340-347` seeds builders "quietly and early". But the only proof that exists is
   buildable software: the repo, the flash flow, the host tests. The collector needs an object; the
   design buyer needs a photograph; **the builder needs only what is already here.** Start with the
   one audience the current state can satisfy, and let it produce the photographs the others need.
4. **The word "player" should not survive into paid media** until there is an audio output. It is
   the single most likely source of a "this thing doesn't even play music" comment thread, and the
   fix is one clause (§3, statement A), not a redesign.

None of this contradicts the plan's central verdict — *make the object wanted, do not sell units*.
It changes what the object is: **a screen and a build, not a box.**
