/**
 * The video search: Invidious, because it is the one public index of YouTube that answers
 * a plain GET with JSON, no key and no client id.
 *
 * A **Video** is something to watch about a Track -- never a Track. It cannot become a
 * Playlist row, it is never downloaded, and the device never sees it (front door spec
 * §4.3, and the glossary in `web/CONTEXT.md`). So this module is deliberately small: one
 * GET, a strict mapping, and an error that names what went wrong. The instance comes from
 * Settings, because public instances come and go.
 *
 * The mapping is exported so it can be tested without a network: the shape an instance
 * answers with is the contract, and a malformed entry is dropped rather than rendered as
 * an empty tile.
 */

export interface Video {
  /** YouTube's eleven-ish character id, as the instance reported it. */
  id: string;
  title: string;
  /** The channel, or `''` when the instance named neither an author nor an id. */
  channel: string;
  /** Seconds; `0` when the instance did not say. */
  duration: number;
  thumbnail: string;
}

export class VideoError extends Error {
  constructor(
    message: string,
    /** The HTTP status, or `0` for "no answer at all". */
    readonly status: number,
  ) {
    super(message);
    this.name = 'VideoError';
  }
}

/** Where a Video is watched: YouTube itself, in a new tab. Nothing is embedded. */
export function watchUrl(id: string): string {
  return `https://www.youtube.com/watch?v=${encodeURIComponent(id)}`;
}

/** The search returns a handful: the page shows 3 to 5 and a longer list is a list. */
export const MAX_VIDEOS = 5;

/** A YouTube id is a short token; anything else is a parse artefact, not a video. */
const VIDEO_ID = /^[A-Za-z0-9_-]{6,20}$/;

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}

/** A trimmed, non-empty string, or null. */
function text(value: unknown): string | null {
  if (typeof value !== 'string') return null;
  const trimmed = value.trim();
  return trimmed.length > 0 ? trimmed : null;
}

/** Seconds as a finite, non-negative number; a string or a negative becomes 0. */
function seconds(value: unknown): number {
  return typeof value === 'number' && Number.isFinite(value) && value > 0 ? value : 0;
}

function httpUrl(value: unknown): string | null {
  const candidate = text(value);
  if (!candidate) return null;
  try {
    const url = new URL(candidate);
    if (url.protocol === 'http:') {
      // An https page cannot show an http picture; the same host over TLS usually can.
      url.protocol = 'https:';
    }
    return url.protocol === 'https:' ? url.toString() : null;
  } catch {
    return null;
  }
}

/** The first usable thumbnail, or YouTube's own still for the id. */
function thumbnailOf(entry: Record<string, unknown>, id: string): string {
  const list = entry.videoThumbnails;
  if (Array.isArray(list)) {
    for (const item of list) {
      if (!isRecord(item)) continue;
      const url = httpUrl(item.url);
      if (url) return url;
    }
  }
  return `https://i.ytimg.com/vi/${encodeURIComponent(id)}/mqdefault.jpg`;
}

/**
 * The instance as a base URL: no trailing slash, `http` or `https` only.
 *
 * A bare word would make the request a relative one and the failure a mystery, so it is
 * refused here with the reason instead.
 */
export function instanceBase(instance: string): string {
  const trimmed = instance.trim();
  if (!trimmed) throw new VideoError('no Invidious instance is set', 0);
  try {
    const url = new URL(trimmed);
    if (url.protocol !== 'http:' && url.protocol !== 'https:') {
      throw new VideoError('the Invidious instance must be an http or https URL', 0);
    }
    return `${url.origin}${url.pathname.replace(/\/+$/, '')}`;
  } catch (error) {
    if (error instanceof VideoError) throw error;
    throw new VideoError(`"${trimmed}" is not a URL`, 0);
  }
}

/**
 * Invidious's search results, filtered and capped. An entry without an id or a title is
 * dropped: a playlist or a channel in a `type=video` answer is a real thing that happens,
 * and it has no video id, so it cannot be embedded.
 */
export function mapSearchResults(value: unknown): Video[] {
  if (!Array.isArray(value)) return [];
  const videos: Video[] = [];
  for (const entry of value) {
    if (!isRecord(entry)) continue;
    const id = text(entry.videoId);
    const title = text(entry.title);
    if (!id || !title || !VIDEO_ID.test(id)) continue;
    videos.push({
      id,
      title,
      channel: text(entry.author) ?? text(entry.authorId) ?? '',
      duration: seconds(entry.lengthSeconds),
      thumbnail: thumbnailOf(entry, id),
    });
    if (videos.length === MAX_VIDEOS) break;
  }
  return videos;
}

/** True for the error `fetch` throws when a search is superseded or the page unmounts. */
export function isAbort(error: unknown): boolean {
  return (
    typeof error === 'object' &&
    error !== null &&
    (error as { name?: unknown }).name === 'AbortError'
  );
}

/**
 * Search the instance. An empty query is an empty answer, not a request.
 *
 * A caller that cancels through `signal` gets the abort error back unchanged, so it can
 * tell "the user typed again" from "the instance is down".
 */
export async function searchVideos(
  query: string,
  instance: string,
  signal?: AbortSignal,
): Promise<Video[]> {
  const trimmed = query.trim();
  if (!trimmed) return [];

  const base = instanceBase(instance);
  const url = `${base}/api/v1/search?q=${encodeURIComponent(trimmed)}&type=video&region=US`;

  let response: Response;
  try {
    response = await fetch(url, { signal, headers: { accept: 'application/json' } });
  } catch (error) {
    if (isAbort(error)) throw error;
    throw new VideoError('the instance did not answer', 0);
  }

  const body = await response.text();
  if (!response.ok) {
    // The instance's own reason is more useful than the status when it gives one.
    let reason = '';
    try {
      const parsed = JSON.parse(body) as { error?: unknown };
      reason = typeof parsed.error === 'string' ? parsed.error : '';
    } catch {
      /* an HTML error page says nothing the status does not */
    }
    throw new VideoError(
      reason || `the instance answered ${response.status}`,
      response.status,
    );
  }

  let payload: unknown;
  try {
    payload = JSON.parse(body);
  } catch {
    throw new VideoError('the instance answered something that is not JSON', response.status);
  }
  if (!Array.isArray(payload)) {
    throw new VideoError('the instance answered a shape this page does not know', response.status);
  }
  return mapSearchResults(payload);
}
