/**
 * An identity for a queue item.
 *
 * `crypto.randomUUID` is secure-context-only, and this page is served over plain HTTP, so
 * calling it is a TypeError on the very first dropped folder. `getRandomValues` is not
 * gated by the secure context and gives the same 122 bits of randomness; the shape is a
 * UUID v4 so it reads like one wherever it is logged.
 */
export function newId(): string {
  const bytes = new Uint8Array(16);
  if (globalThis.crypto?.getRandomValues) {
    globalThis.crypto.getRandomValues(bytes);
  } else {
    /* A browser without getRandomValues is a browser from before this app's floor; a
     * counter still keeps the queue's keys unique, which is all this identity has to do. */
    counter += 1;
    bytes.set(new TextEncoder().encode(`${Date.now().toString(36)}-${counter}`), 0);
  }
  bytes[6] = (bytes[6] & 0x0f) | 0x40;
  bytes[8] = (bytes[8] & 0x3f) | 0x80;

  const hex = Array.from(bytes, (byte) => byte.toString(16).padStart(2, '0')).join('');
  return `${hex.slice(0, 8)}-${hex.slice(8, 12)}-${hex.slice(12, 16)}-${hex.slice(16, 20)}-${hex.slice(20)}`;
}

let counter = 0;
