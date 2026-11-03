import { describe, expect, it } from 'vitest';
import { EMBED_REFERRER, embedUrl } from '../src/ui/areas/Video';

/**
 * The one frame the App is allowed to create.
 *
 * `frame-src` in `vercel.json` names exactly these hosts. If this ever points somewhere else —
 * an Invidious instance chosen in Settings, say — the browser refuses the frame and the page
 * shows nothing, so the URL is pinned here with the policy.
 */
describe('the inline player', () => {
  it('is YouTube’s no-cookie embed, and nothing else', () => {
    const url = new URL(embedUrl('dQw4w9WgXcQ'));
    expect(url.origin).toBe('https://www.youtube-nocookie.com');
    expect(url.pathname).toBe('/embed/dQw4w9WgXcQ');
    expect(url.searchParams.get('autoplay')).toBe('1');
  });

  it('encodes an id rather than pasting it into the path', () => {
    expect(embedUrl('a/b')).toContain('a%2Fb');
  });

  it('sends an origin as the referrer, because YouTube refuses a bare frame', () => {
    // Error 153 inside the player is `embedder.identity.missing.referrer`: the pages answer
    // `Referrer-Policy: no-referrer`, so the frame states its own. `origin` sends the host
    // and nothing else, which is the least that makes the player work.
    expect(EMBED_REFERRER).toBe('origin');
  });
});
