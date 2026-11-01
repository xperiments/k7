# The assistant waits for a secure origin

> **Status: superseded by [ADR 0053](./0053-the-assistant-writes-filters.md)** — the verdict
> ("no assistant is built") no longer holds now that the model is remote rather than local. The
> shape rule this ADR settled — the model produces filters, never results — is adopted there
> unchanged. The reasoning below is kept as the record of why it waited.

The owner wanted an in-browser assistant: a local LLM that turns a sentence — "something for a
rainy Sunday that doesn't repeat the last hour" — into a query over the **Library**. It was
grilled down to its dependencies, and then dropped. This records why, so it is not re-argued
from scratch.

## What the idea needed

- **A secure origin.** `navigator.gpu` is secure-context-only, and WebLLM is WebGPU. The page
  the device serves is `http://xtape.local`, so the model cannot run there at all.
- **A way to reach the device from that origin.** A page on a public HTTPS site cannot fetch
  `http://xtape.local`: mixed content, plus Chrome's Local Network Access restrictions.
- **Listening history.** "Doesn't repeat the last hour" is not in the Snapshot, the firmware or
  the app. It is the listener's hour, not the player's, so it belongs in the client — which
  means the client keeps a **Play** log and applies it to search results.

## The two routes that existed

1. **An MV3 Chrome extension.** `chrome-extension://…/app.html` is a secure context with
   WebGPU, OPFS and a service worker, and `host_permissions: ["http://xtape.local/*"]` lets it
   fetch the device without CORS, without LNA prompts and without a certificate. The cost is
   installation: Chrome installs extensions only from the Web Store, by enterprise policy, or
   through Developer mode → *Load unpacked*. None of those is a link, and the last one is more
   ceremony than the certificate dialog that was already rejected. The extension would also
   become the product's face, with `xtape.local` reduced to a landing page.
2. **A public name pointing at the device.** `xtape.xperiments.in A 192.168.1.69` plus a
   publicly signed certificate, and the device serves HTTPS again. The page is then an ordinary
   website — secure context, WebGPU, same origin — and nothing is installed on the client. The
   cost falls on the owner: one DNS record per network, and a certificate to renew.

## Decision

**Neither. The front door stays plain HTTP (ADR 0049), and no assistant is built.**

The reasoning is about who pays: the extension charges every user an install and the product a
published extension; the public name charges the owner a DNS record and a renewal, and only
works on networks the owner controls. Neither is worth paying for one feature whose value was
never measured, and the rest of the app — analysis, snapshot, upload, the player — needs
neither.

## What would reopen it

A secure origin that costs nobody anything, which today means: the device gets a name in public
DNS that resolves to its LAN address (route 2), or a published extension is wanted for other
reasons. If either happens, the assistant's shape is already settled:

- an **Intent** is a sentence parsed into *filters* (the `library_search` arguments) plus a
  *recency* constraint;
- the model produces the filters, never the results — a wrong filter is visible and correctable,
  a confident paragraph about music that does not exist is not;
- **Play** history lives in the client, because "the last hour" is the listener's hour.
