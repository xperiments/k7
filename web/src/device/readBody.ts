/**
 * A response's body, read in chunks so a caller can show how far a file has come.
 *
 * The device sends `Content-Length` (and exposes it: `http_server.c`'s
 * `Access-Control-Expose-Headers`), so a cross-origin page can name the fraction. A reply
 * without one still streams; the caller just gets no fraction to show.
 */
export async function readBody(
  response: Response,
  onProgress?: (fraction: number) => void,
): Promise<Uint8Array> {
  const total = Number(response.headers.get('content-length') ?? 0);
  const body = response.body;
  if (!body || !Number.isFinite(total) || total <= 0 || !onProgress) {
    return new Uint8Array(await response.arrayBuffer());
  }
  const reader = body.getReader();
  const chunks: Uint8Array[] = [];
  let loaded = 0;
  for (;;) {
    const { done, value } = await reader.read();
    if (done) break;
    if (!value) continue;
    chunks.push(value);
    loaded += value.byteLength;
    onProgress(Math.min(1, loaded / total));
  }
  const out = new Uint8Array(loaded);
  let at = 0;
  for (const chunk of chunks) {
    out.set(chunk, at);
    at += chunk.byteLength;
  }
  return out;
}
