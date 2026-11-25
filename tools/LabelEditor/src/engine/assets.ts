/**
 * A file the editor publishes from `public/`, at the base its build is served under.
 *
 * The editor is one Vite project at two addresses (ADR 0112): the root in development, and
 * `/k7art/` on the Site. A path written from the document — `./fonts/a.ttf` — is resolved
 * against the page's URL, so at `/k7art` (no trailing slash) it names `/fonts/a.ttf`: the
 * Site's own page, HTML where a face is expected. `BASE_URL` is the build's base, so the
 * faces load at either address and the canvas wears the sheet's type rather than the
 * browser's fallback at a fallback's widths.
 */
export function publicAsset(path: string): string {
  return `${import.meta.env.BASE_URL}${path.replace(/^\/+/, "")}`;
}

/** One of the sheet's faces, as this build serves it. */
export function faceUrl(file: string): string {
  return publicAsset(`fonts/${file}`);
}
