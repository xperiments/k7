import { useEffect, useSyncExternalStore } from 'react';

import { fetchArtPack, fetchArtStatus, resetArt, uploadArt, type ArtStatus } from '../device/art';
import { getDevice, subscribeDeviceLink } from '../device/device';
import { DeviceError } from '../device/transport';
import {
  dropCachedArtPack,
  putCachedArtPack,
  readCachedArtPack,
  type CachedArtPack,
} from '../storage/artPacks';
import { readJcardPack } from './model';
import { readArtPack, type ArtPack } from './pack';
import { resetUserJcards, setUserJcards } from './pool';

/**
 * The active user pack, as the App knows it: the device's status, the J-cards it draws, and
 * the browser's cache (`art-packs.md` §4/§6).
 *
 * Two things happen on different clocks, and this is the seam between them:
 *
 * - **The cache is read first.** The covers draw from the last installed pack at boot,
 *   before the player has answered, so a cold `k7.local` lookup does not turn a shelf into
 *   the factory for a frame.
 * - **The device is asked next.** When it answers, the status says whether the partition
 *   holds a pack the device reads; if it does, the App fetches the pack's bytes, re-checks
 *   them with `readArtPack`, and replaces the pool and the cache only when the bytes differ.
 *   A factory status (or an invalid pack the device fell back from) resets both.
 *
 * A failed call is not a reset. With no player, the cached designs stay and the factory is
 * always behind them; an upload or a reset is offered only while the player answers.
 */

export interface ArtPackSnapshot {
  /** The device's last answer, or null before it has answered. */
  status: ArtStatus | null;
  /** The last installed pack this browser cached; it draws before the device answers. */
  cached: CachedArtPack | null;
  /** False while the player did not answer the art routes at all. */
  reachable: boolean;
  /** A status or pack read is in flight. */
  loading: boolean;
  /** A real refusal from a player that answered (a route, a pack, a parse). */
  error: string | null;
  /** An upload or a reset is in flight; the page disables its two writes. */
  busy: boolean;
}

const INITIAL: ArtPackSnapshot = {
  status: null,
  cached: null,
  reachable: false,
  loading: true,
  error: null,
  busy: false,
};

let snapshot: ArtPackSnapshot = INITIAL;
const listeners = new Set<() => void>();

function publish(patch: Partial<ArtPackSnapshot>): void {
  snapshot = { ...snapshot, ...patch };
  for (const listener of listeners) listener();
}

export function subscribeArtPack(listener: () => void): () => void {
  listeners.add(listener);
  return () => {
    listeners.delete(listener);
  };
}

export function artPackState(): ArtPackSnapshot {
  return snapshot;
}

/** The Art packs page's store, and the shelf's when a pack arrives mid-visit. */
export function useArtPackState(): ArtPackSnapshot {
  return useSyncExternalStore(subscribeArtPack, artPackState, artPackState);
}

function messageOf(error: unknown): string {
  return error instanceof Error ? error.message : String(error);
}

/**
 * Put the cached pack's J-cards into the pool. Safe to call more than once: a cache this
 * browser cannot read is dropped, and a cache whose J-cards no longer parse is dropped
 * too, because the pool it would feed must never be a half pack.
 */
export async function restoreArtPacks(): Promise<void> {
  let cached: CachedArtPack | null;
  try {
    cached = await readCachedArtPack();
  } catch {
    return;
  }
  if (!cached) return;

  const designs = readJcardPack(cached.jcards);
  if (!designs) {
    try {
      await dropCachedArtPack();
    } catch {
      /* the pool is still the factory; a cache that cannot be cleared is not a face */
    }
    publish({ cached: null });
    return;
  }

  setUserJcards(designs);
  publish({ cached });
}

let refreshing: Promise<void> | null = null;
let refreshAgain = false;
let revision = 0;

/**
 * Ask the player what pack is active, and make the pool and the cache agree with it.
 * One request at a time; a call while one is in flight answers the same promise and asks
 * once more when it settles, because a link that arrived mid-read deserves a fresh answer.
 */
export function refreshArtPacks(): Promise<void> {
  if (snapshot.busy) return Promise.resolve();
  if (refreshing) {
    refreshAgain = true;
    return refreshing;
  }
  const mine = ++revision;
  refreshing = runRefresh(mine).finally(() => {
    refreshing = null;
    if (refreshAgain) {
      refreshAgain = false;
      void refreshArtPacks();
    }
  });
  return refreshing;
}

async function runRefresh(mine: number): Promise<void> {
  publish({ loading: true, error: null });

  let status: ArtStatus;
  try {
    status = await fetchArtStatus();
  } catch (error) {
    if (mine !== revision) return;
    /* A 404 or another refusal is a player that answered, so the page can name what is
     * wrong; an unreachable or timed-out player is simply not there, which is the normal
     * card-in-the-reader case and needs no error sentence. */
    const answered =
      error instanceof DeviceError && error.code !== 'unreachable' && error.code !== 'timeout';
    publish({
      loading: false,
      reachable: false,
      error: answered ? messageOf(error) : null,
    });
    return;
  }
  if (mine !== revision) return;

  if (!status.installed || !status.valid) {
    /* The factory is the pool: no user pack, or one the device refused and fell back from.
     * Drop the cache too, or the next boot would draw a pack the device says is not there. */
    resetUserJcards();
    try {
      await dropCachedArtPack();
    } catch {
      /* a cache that cannot be cleared is not a face */
    }
    if (mine !== revision) return;
    publish({ status, cached: null, reachable: true, loading: false, error: null });
    return;
  }

  /* The status names the pack before its bytes arrive, so the page can show what the
   * player says it holds while the App reads the J-cards. */
  publish({ status, reachable: true });

  try {
    const pack = readArtPack(await fetchArtPack());
    if (mine !== revision) return;
    setUserJcards(pack.designs);

    let cached = snapshot.cached;
    if (!cached || cached.packCrc !== pack.packCrc || cached.size !== pack.size) {
      try {
        cached = await putCachedArtPack(pack);
      } catch {
        /* the covers still draw from the pool this visit */
        cached = null;
      }
    }
    if (mine !== revision) return;
    publish({ status, cached, reachable: true, loading: false, error: null });
  } catch (error) {
    if (mine !== revision) return;
    /* The status says there is a pack, but its bytes did not come back as one. Keep the
     * pool that was already drawing (the cache, or the factory) and name the failure. */
    publish({
      status,
      reachable: true,
      loading: false,
      error: `The player's pack could not be read: ${messageOf(error)}`,
    });
  }
}

/**
 * Install one already-checked pack. The device checks it again before it writes; only when
 * it answers does the App adopt the designs and cache them, so a refusal changes nothing.
 */
export async function uploadArtPack(pack: ArtPack): Promise<ArtStatus> {
  revision += 1;
  publish({ busy: true, error: null });
  try {
    const status = await uploadArt(pack.bytes);
    setUserJcards(pack.designs);
    let cached: CachedArtPack | null = null;
    try {
      cached = await putCachedArtPack(pack);
    } catch {
      /* the covers draw this visit; the next boot starts from the factory */
    }
    publish({
      status,
      cached,
      reachable: true,
      loading: false,
      busy: false,
      error: null,
    });
    return status;
  } catch (error) {
    publish({ busy: false });
    throw error;
  }
}

/** Erase the user pack. The factory sheet is the device's and is never touched. */
export async function resetArtPacks(): Promise<ArtStatus> {
  revision += 1;
  publish({ busy: true, error: null });
  try {
    const status = await resetArt();
    resetUserJcards();
    try {
      await dropCachedArtPack();
    } catch {
      /* the pool is the factory even when the cache cannot be cleared */
    }
    publish({
      status,
      cached: null,
      reachable: true,
      loading: false,
      busy: false,
      error: null,
    });
    return status;
  } catch (error) {
    publish({ busy: false });
    throw error;
  }
}

/**
 * The App's one boot-time wiring: the cache first, then the player, and again whenever the
 * link arrives or dies. It lives above the routes so the covers are ready before the first
 * shelf is drawn, not when Settings happens to be opened.
 */
export function useArtPackSync(): void {
  useEffect(() => {
    let disposed = false;
    void (async () => {
      await restoreArtPacks();
      if (!disposed) await refreshArtPacks();
    })();
    /* A probe notifies while the link is still `none`; only a link that actually changed
     * is worth another status and pack read. */
    let known = getDevice().link();
    const off = subscribeDeviceLink(() => {
      if (disposed) return;
      const link = getDevice().link();
      if (link === known) return;
      known = link;
      void refreshArtPacks();
    });
    return () => {
      disposed = true;
      off();
    };
  }, []);
}
