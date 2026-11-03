import {
  createContext,
  useCallback,
  useContext,
  useEffect,
  useRef,
  useState,
  type ReactNode,
} from 'react';

import { DEVICE_ORIGIN } from '../config';
import { getDevice } from '../device/device';
import type { CachedCover } from '../storage/media';

/**
 * The page's one audio element, and what it is playing.
 *
 * Two sources, and they are different facts: the **stream** is what the player is playing,
 * fetched from the device's own `/stream`; a **card file** is a track read off the card in the
 * computer, through the handle the page holds. The bar shows which, because "playing here"
 * means something different in each case — one is a live radio, the other a file with a
 * beginning and an end.
 *
 * The element is created with the provider rather than by the bar, so a bar that is re-rendered,
 * hidden or scrolled does not interrupt the sound, and so `playStream()` can be called from a
 * page that has no bar in view (the Player area's stream button).
 */

export interface CardTrack {
  /** The file as the card holds it now. */
  file: File;
  /** The path below `music`, so the bar can say where it came from. */
  path: string;
  title: string;
  artist: string;
  cover: CachedCover | null;
}

export type Source = { kind: 'stream' } | ({ kind: 'card' } & CardTrack);

export interface PlaybackApi {
  /** What the element is pointed at; null before anything has been chosen. */
  source: Source | null;
  playing: boolean;
  /** The browser or the device refused the stream: the sentence to show. */
  error: string | null;
  /** Seconds into a card file; 0 for a live stream. */
  time: number;
  /** A card file's length; `Infinity` for a stream, which has none. */
  duration: number;
  volume: number;
  toggle: () => void;
  playStream: () => void;
  playCard: (track: CardTrack) => void;
  /** Stop and forget the source; the bar goes back to "nothing playing". */
  stop: () => void;
  seek: (seconds: number) => void;
  setVolume: (level: number) => void;
}

const PlaybackContext = createContext<PlaybackApi | null>(null);

/** Where the device's stream lives from this page: its own origin when it served the page. */
export function streamUrl(link: string): string {
  return link === 'direct' ? '/stream' : `${DEVICE_ORIGIN}/stream`;
}

/** The browser's own refusal, said usefully: the code is the only thing it gives. */
function mediaError(element: HTMLMediaElement): string {
  const failure = element.error;
  switch (failure?.code) {
    case MediaError.MEDIA_ERR_NETWORK:
      return 'The stream could not be fetched. Is the player reachable from this page?';
    case MediaError.MEDIA_ERR_SRC_NOT_SUPPORTED:
      return 'This page may not play that source: the browser refused it (mixed content or a missing codec).';
    case MediaError.MEDIA_ERR_DECODE:
      return 'The audio could not be decoded.';
    case MediaError.MEDIA_ERR_ABORTED:
      return 'The audio was interrupted.';
    default:
      return failure?.message || 'The audio could not be played.';
  }
}

export function PlaybackProvider({ children }: { children: ReactNode }) {
  const audio = useRef<HTMLAudioElement | null>(null);
  const objectUrl = useRef<string | null>(null);
  const [source, setSource] = useState<Source | null>(null);
  const [playing, setPlaying] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [time, setTime] = useState(0);
  const [duration, setDuration] = useState(0);
  const [volume, setLevel] = useState(1);

  /* One element for the page. Created here so a re-render never replaces the sink. */
  if (audio.current === null && typeof Audio !== 'undefined') {
    audio.current = new Audio();
    audio.current.preload = 'none';
    audio.current.volume = volume;
  }

  useEffect(() => {
    const element = audio.current;
    if (!element) return;
    const onPlay = () => setPlaying(true);
    const onPause = () => setPlaying(false);
    const onTime = () => setTime(element.currentTime);
    const onDuration = () => setDuration(element.duration);
    const onError = () => {
      setPlaying(false);
      setError(mediaError(element));
    };
    element.addEventListener('play', onPlay);
    element.addEventListener('pause', onPause);
    element.addEventListener('timeupdate', onTime);
    element.addEventListener('durationchange', onDuration);
    element.addEventListener('loadedmetadata', onDuration);
    element.addEventListener('error', onError);
    return () => {
      element.removeEventListener('play', onPlay);
      element.removeEventListener('pause', onPause);
      element.removeEventListener('timeupdate', onTime);
      element.removeEventListener('durationchange', onDuration);
      element.removeEventListener('loadedmetadata', onDuration);
      element.removeEventListener('error', onError);
    };
  }, []);

  /* A blob URL lives until it is revoked; a page that plays twenty tracks should not keep
   * twenty files in memory. */
  const releaseObjectUrl = useCallback(() => {
    if (objectUrl.current !== null) {
      URL.revokeObjectURL(objectUrl.current);
      objectUrl.current = null;
    }
  }, []);

  useEffect(() => releaseObjectUrl, [releaseObjectUrl]);

  const start = useCallback(async () => {
    const element = audio.current;
    if (!element) return;
    setError(null);
    try {
      await element.play();
    } catch {
      // A refusal is not a failure of the source: the bar says which, and offers play again.
      setError('The browser would not start the audio. Press play to try again.');
    }
  }, []);

  const playStream = useCallback(() => {
    const element = audio.current;
    if (!element) return;
    releaseObjectUrl();
    const url = streamUrl(getDevice().link());
    if (element.src !== new URL(url, location.href).href) {
      element.src = url;
    }
    setSource({ kind: 'stream' });
    setTime(0);
    setDuration(0);
    void start();
  }, [releaseObjectUrl, start]);

  const playCard = useCallback(
    (track: CardTrack) => {
      const element = audio.current;
      if (!element) return;
      releaseObjectUrl();
      const url = URL.createObjectURL(track.file);
      objectUrl.current = url;
      element.src = url;
      element.preload = 'metadata';
      setSource({ kind: 'card', ...track });
      setTime(0);
      setDuration(0);
      void start();
    },
    [releaseObjectUrl, start],
  );

  const toggle = useCallback(() => {
    const element = audio.current;
    if (!element) return;
    if (source === null) {
      playStream();
      return;
    }
    if (element.paused) void start();
    else element.pause();
  }, [playStream, source, start]);

  const stop = useCallback(() => {
    const element = audio.current;
    if (element) {
      element.pause();
      element.removeAttribute('src');
      element.load();
    }
    releaseObjectUrl();
    setSource(null);
    setPlaying(false);
    setTime(0);
    setDuration(0);
  }, [releaseObjectUrl]);

  const seek = useCallback((seconds: number) => {
    const element = audio.current;
    if (!element || !Number.isFinite(seconds)) return;
    element.currentTime = Math.max(0, seconds);
    setTime(element.currentTime);
  }, []);

  const setVolume = useCallback((level: number) => {
    const element = audio.current;
    const next = Math.min(1, Math.max(0, level));
    if (element) element.volume = next;
    setLevel(next);
  }, []);

  const api: PlaybackApi = {
    source,
    playing,
    error,
    time,
    duration,
    volume,
    toggle,
    playStream,
    playCard,
    stop,
    seek,
    setVolume,
  };

  return <PlaybackContext.Provider value={api}>{children}</PlaybackContext.Provider>;
}

export function usePlayback(): PlaybackApi {
  const value = useContext(PlaybackContext);
  if (value === null) throw new Error('usePlayback must be used inside PlaybackProvider');
  return value;
}
