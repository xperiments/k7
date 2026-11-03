# A Provider's failure is named, and a local Provider has to allow this page

ADR 0053 made the **Provider** a setting: an OpenAI-compatible endpoint, its model and its key,
held in this browser. The page asking it is served by the device at `http://xtape.local`
(ADR 0049), which is a plain-HTTP origin on the LAN. The first Provider tried against it was
Ollama, on the same computer's `http://localhost:11434/v1`.

That does not work out of the box, and the way it fails is the point. Ollama allows a fixed set
of origins — `localhost`, `127.0.0.1`, `app://*`, `file://*` — and `http://xtape.local` is not
in it. The preflight for `POST /v1/chat/completions` came back `403` with no
`Access-Control-Allow-Origin`, the browser discarded the reply, and the page was handed a bare
`TypeError: Failed to fetch`. Settings' Test reported "The provider could not be reached", which
was false: the provider was running and answering the whole time. Nothing in the error, the
status or the response headers is visible to script — a CORS refusal and a dead socket are the
same exception.

## Decision

- **The failure is named, not guessed at.** One module, `web/src/provider.ts`, owns the
  completions URL and the sentence a thrown request is worth, and Settings' Test and Ask's
  Intent both use it.
- **The page settles the question it can settle.** A failed `fetch` is followed by one `GET`
  of `<base>/models` in `no-cors` mode — a request the browser sends without checking the
  reply's origin — so an opaque response proves that something at that address answered. If it
  did, the sentence says the Provider answered and that its CORS answer does not allow this
  page, and names the origin to add. If it did not, the sentence says nothing answered there
  and points at the base URL.
- **The origin named is the page's own**, `location.origin`, because that — not the Provider's
  address — is the string its allowlist has to contain. For an endpoint on this machine, where
  the knob is the owner's, `OLLAMA_ORIGINS` is named as the setting, in the app and in
  `web/README.md`. A hosted Provider is somebody else's policy, so the sentence says that
  instead: it either allows browsers or it does not, and nothing here can change it.
- **A hosted Provider needs none of it.** Nothing about this depends on the endpoint being
  local; the same module reports a genuine outage as one.

## Why not a device proxy

The obvious alternative is for the firmware to forward the call, which would remove CORS from
the picture. It breaks the rule ADR 0053 settled — the key is never sent to the device — and it
puts a TLS client, a request buffer and a second network context on the half of the system that
has the least room for them, to fix a header on the other half. The page is the right place to
call an endpoint the page chose.

## What this costs

- One extra request, only after a failure, with its own 2.5 s cap: the probe must not make a
  dead host feel slower than it already does.
- `no-cors` hides everything but "something answered" — an endpoint that is up but returning
  `500` on `/models` still counts as up, and that is the right reading here.
- The remedy is on the Provider's side, where the page cannot go. The page can only say which
  origin to allow, and — when the Provider is on this machine — what the knob is called.
- For a loopback endpoint the sentence names Ollama, which is product copy tied to one vendor.
  It is the vendor an owner on a LAN is most likely to run, the sentence is still true without
  it, and the hint is withheld for any other host, where `OLLAMA_ORIGINS` would be noise.

## What stays possible

A Provider that allows the origin just works, and a build that forwards the call through a host
helper could replace `provider.ts` without touching either call site.
