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
 * fetched from the device's own `/stream`; a **card track** is a file read off the card in the
 * computer, through the handle the page holds. The bar shows which, because "playing here"
 * means something different in each case — one is a live radio, the other a file with a
 * beginning, an end, and neighbours.
 *
 * A card play is always **an album**: the provider is handed the album's tracks in order and
 * one loader for their bytes, so `next()` needs nothing from the page that started it. A
 * track that ends asks for the next; the last one stops. A stream has no next, because there
 * is nothing to be next to — and no end either: when its connection drops, the page
 * reconnects to the live edge instead of reporting silence to someone listening to a radio.
 */

export interface TrackMeta {
  /** The path below `music`, which is also what the loader is asked for. */
  path: string;
  title: string;
  artist: string;
  cover: CachedCover | null;
}

/** Reads one track's bytes off the card; null when the card cannot give them. */
export type TrackLoader = (path: string) => Promise<File | null>;

export type Source =
  | { kind: 'stream' }
  | { kind: 'card'; track: TrackMeta; index: number; length: number };

export interface PlaybackApi {
  /** What the element is pointed at; null before anything has been chosen. */
  source: Source | null;
  playing: boolean;
  /** The browser or the device refused the source: the sentence to show. */
  error: string | null;
  /** Seconds into a card track; 0 for a live stream. */
  time: number;
  /** A card track's length; `Infinity` for a stream, which has none. */
  duration: number;
  volume: number;
  /** The album has a track before this one. False for a stream and for a first track. */
  hasPrevious: boolean;
  /** The album has a track after this one. False for a stream and for a last track. */
  hasNext: boolean;
  toggle: () => void;
  playStream: () => void;
  /**
   * Play one track of an album, with its neighbours. Resolves false when the card cannot give
   * the track's bytes — the bar then says so and nothing else changes.
   */
  playAlbum: (tracks: readonly TrackMeta[], start: number, load: TrackLoader) => Promise<boolean>;
  next: () => void;
  previous: () => void;
  /** Stop and forget the source and the album; the bar goes back to "nothing playing". */
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
  /* The album and the way to read it. Refs, not state: they are not rendered, and a new
   * array on every render would be an identity that changes for no reason. */
  const queue = useRef<readonly TrackMeta[]>([]);
  const loader = useRef<TrackLoader | null>(null);
  /** What the element should do when a track ends, kept current by an effect below. */
  const advance = useRef<() => void>(() => {});

  const [source, setSource] = useState<Source | null>(null);
  /* The same value for the element's handlers, which are attached once and must not go
   * stale: whether a stopped stream is a stream is the whole of the reconnect rule. */
  const sourceRef = useRef<Source | null>(null);
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
    sourceRef.current = source;
  }, [source]);

  const releaseObjectUrl = useCallback(() => {
    if (objectUrl.current !== null) {
      URL.revokeObjectURL(objectUrl.current);
      objectUrl.current = null;
    }
  }, []);

  useEffect(() => releaseObjectUrl, [releaseObjectUrl]);

  /* A live stream that stops is not an error to report and forget. The device drops a
   * client that falls ten seconds behind (its ring is a valve, not a queue that grows), a
   * phone loses Wi-Fi, the player reboots — and the broadcast goes on without any of them.
   * So the page reconnects on its own, with a backoff that tops out at ten seconds, and the
   * listener hears a gap instead of silence until a reload. */
  const stream = useRef<{ timer: number | null; attempt: number }>({ timer: null, attempt: 0 });

  const cancelReconnect = useCallback(() => {
    if (stream.current.timer !== null) {
      window.clearTimeout(stream.current.timer);
      stream.current.timer = null;
    }
    stream.current.attempt = 0;
  }, []);

  const reconnectStream = useCallback(() => {
    if (sourceRef.current?.kind !== 'stream' || stream.current.timer !== null) return;
    const wait = Math.min(1000 * 2 ** stream.current.attempt, 10_000);
    stream.current.attempt += 1;
    stream.current.timer = window.setTimeout(() => {
      stream.current.timer = null;
      const element = audio.current;
      if (!element || sourceRef.current?.kind !== 'stream') return;
      /* A fresh query on each try: the browser must open a new connection rather than
       * reuse the one the device has already dropped. */
      const url = new URL(streamUrl(getDevice().link()), location.href);
      url.searchParams.set('reconnect', String(stream.current.attempt));
      element.src = url.href;
      element.play().catch(() => {
        setError('The stream stopped, and the browser would not restart it. Press play.');
      });
    }, wait);
  }, []);

  const start = useCallback(async () => {
    const element = audio.current;
    if (!element) return;
    setError(null);
    try {
      await element.play();
    } catch {
      setError('The browser would not start the audio. Press play to try again.');
    }
  }, []);

  const forgetAlbum = useCallback(() => {
    queue.current = [];
    loader.current = null;
  }, []);

  /** One track of the album, replacing whatever was sounding. */
  const playAt = useCallback(
    async (index: number): Promise<boolean> => {
      const element = audio.current;
      const track = queue.current[index];
      const read = loader.current;
      if (!element || !track || !read) return false;

      let file: File | null;
      try {
        file = await read(track.path);
      } catch {
        file = null;
      }
      if (file === null) {
        setError('This browser could not read that track from the card.');
        return false;
      }

      releaseObjectUrl();
      const url = URL.createObjectURL(file);
      objectUrl.current = url;
      element.src = url;
      element.preload = 'metadata';
      setSource({ kind: 'card', track, index, length: queue.current.length });
      setTime(0);
      setDuration(0);
      void start();
      return true;
    },
    [releaseObjectUrl, start],
  );

  const playAlbum = useCallback(
    async (tracks: readonly TrackMeta[], startIndex: number, read: TrackLoader) => {
      queue.current = tracks;
      loader.current = read;
      const played = await playAt(startIndex);
      if (!played) {
        forgetAlbum();
        setError('This browser could not read that track from the card. Play again to allow access.');
      }
      return played;
    },
    [forgetAlbum, playAt],
  );

  const playStream = useCallback(() => {
    const element = audio.current;
    if (!element) return;
    cancelReconnect();
    releaseObjectUrl();
    forgetAlbum();
    const url = streamUrl(getDevice().link());
    if (element.src !== new URL(url, location.href).href) element.src = url;
    setSource({ kind: 'stream' });
    setTime(0);
    setDuration(0);
    void start();
  }, [cancelReconnect, forgetAlbum, releaseObjectUrl, start]);

  const hasPrevious = source?.kind === 'card' && source.index > 0;
  const hasNext = source?.kind === 'card' && source.index < source.length - 1;

  const next = useCallback(() => {
    if (source?.kind !== 'card' || source.index >= source.length - 1) return;
    void playAt(source.index + 1);
  }, [playAt, source]);

  const previous = useCallback(() => {
    if (source?.kind !== 'card' || source.index === 0) return;
    void playAt(source.index - 1);
  }, [playAt, source]);

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
    cancelReconnect();
    const element = audio.current;
    if (element) {
      element.pause();
      element.removeAttribute('src');
      element.load();
    }
    releaseObjectUrl();
    forgetAlbum();
    setSource(null);
    setPlaying(false);
    setTime(0);
    setDuration(0);
  }, [cancelReconnect, forgetAlbum, releaseObjectUrl]);

  const seek = useCallback((seconds: number) => {
    const element = audio.current;
    if (!element || !Number.isFinite(seconds)) return;
    element.currentTime = Math.max(0, seconds);
    setTime(element.currentTime);
  }, []);

  const setVolume = useCallback((level: number) => {
    const element = audio.current;
    const nextLevel = Math.min(1, Math.max(0, level));
    if (element) element.volume = nextLevel;
    setLevel(nextLevel);
  }, []);

  /* The handler the element runs: it sees today's album through the refs, not the album that
   * existed when the listener was attached. */
  useEffect(() => {
    advance.current = () => {
      if (hasNext) next();
      else stop();
    };
  }, [hasNext, next, stop]);

  useEffect(() => {
    const element = audio.current;
    if (!element) return;
    const onPlay = () => {
      setPlaying(true);
      setError(null);
      stream.current.attempt = 0;
    };
    const onPause = () => setPlaying(false);
    const onTime = () => setTime(element.currentTime);
    const onDuration = () => setDuration(element.duration);
    const onError = () => {
      setPlaying(false);
      if (sourceRef.current?.kind === 'stream') {
        /* A dropped client, a lost Wi-Fi link, a rebooted player: all the same from the
         * page's side, and all recovered the same way. */
        setError('The stream stopped; reconnecting…');
        reconnectStream();
        return;
      }
      setError(mediaError(element));
    };
    /* A track that ends moves to the next one; the last one is the end of the album, so it
     * stops rather than looping. */
    const onEnded = () => {
      /* A live stream has no end: what ended is the connection, and the listener should
       * not have to notice. */
      if (sourceRef.current?.kind === 'stream') {
        setPlaying(false);
        reconnectStream();
        return;
      }
      element.currentTime = 0;
      setTime(0);
      setPlaying(false);
      advance.current();
    };
    element.addEventListener('play', onPlay);
    element.addEventListener('pause', onPause);
    element.addEventListener('timeupdate', onTime);
    element.addEventListener('durationchange', onDuration);
    element.addEventListener('loadedmetadata', onDuration);
    element.addEventListener('error', onError);
    element.addEventListener('ended', onEnded);
    return () => {
      element.removeEventListener('play', onPlay);
      element.removeEventListener('pause', onPause);
      element.removeEventListener('timeupdate', onTime);
      element.removeEventListener('durationchange', onDuration);
      element.removeEventListener('loadedmetadata', onDuration);
      element.removeEventListener('error', onError);
      element.removeEventListener('ended', onEnded);
    };
  }, [reconnectStream]);

  useEffect(() => cancelReconnect, [cancelReconnect]);

  const api: PlaybackApi = {
    source,
    playing,
    error,
    time,
    duration,
    volume,
    hasPrevious,
    hasNext,
    toggle,
    playStream,
    playAlbum,
    next,
    previous,
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
