# The assistant writes filters, and runs on a remote provider

ADR 0050 refused an in-browser assistant and wrote down why: a local model needs a secure origin,
so it needs either a published Chrome extension or a name in public DNS — an install for every
user or a certificate renewal for the owner — and neither was worth one unmeasured feature. It
also settled the shape for whoever reopened it: **the model produces the filters, never the
results**, because "a wrong filter is visible and correctable, a confident paragraph about music
that does not exist is not."

One of those arguments no longer holds, and the other still does. The model is **remote** now: an
OpenAI-compatible endpoint with a key the user supplies. Nothing needs `navigator.gpu`, so
`http://xtape.local` is no longer a reason to refuse; the page can call an HTTPS provider from a
plain-HTTP origin; nothing is installed and no DNS record is bought. What stays true is the shape,
because a model that chooses results is a model inventing tracks.

## Decision

- **The verdict is superseded; the shape is adopted.** "No assistant is built" no longer holds.
  The rule that the model returns filters does.
- **An Intent is the whole output.** A sentence becomes `library_search` arguments — BPM range,
  key, mood, energy, text, sort, count. The Intent is shown on screen and editable before it is
  run, and only the app runs it, against the device's own MCP endpoint.
- **No track leaves the browser.** The provider receives the sentence and the list of *filter*
  names; it never receives a title, an artist, a path, a measurement or a library size.
- **The provider is a setting, not a build.** Base URL, model and key live in this browser's
  settings, are never sent to the device, and a page with none of them is the filter form.
- **Degrade to the form.** With no provider, or an answer that does not parse, the filter fields
  stand on their own; the assistant is an accelerator, never the only way to ask.

## What this costs

- A key in `localStorage`, readable by anything running on the device's origin. The origin is the
  appliance on the LAN (ADR 0049), so the exposure is the LAN; the key is never written to flash.
- A network dependency for one feature, and a provider whose filters can be wrong in ways the
  visible Intent cannot make obvious — a key typo is not a filter typo.
- The owner's own tokens and money, on the owner's own key.

## What would reopen it

Local inference, which needs the secure origin ADR 0050 declined to pay for; or a recency
constraint ("not the last hour"), which is not in the Snapshot and would need a **Play** log in
the client, exactly as 0050 described.
