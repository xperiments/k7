import 'fake-indexeddb/auto';

import { beforeEach, describe, expect, it, vi } from 'vitest';

import type { ArtStatus } from '../src/device/art';
import { DeviceError } from '../src/device/transport';
import type { ArtPack } from '../src/jcard/pack';

/**
 * The active pack as the App holds it (`art-packs.md` §4/§6): a cached pack draws first,
 * the player's answer replaces it only when the bytes differ, and a failed call changes
 * nothing. The pool and the cache are the two facts the covers and the Art packs page read.
 */

vi.mock('../src/device/art', () => ({
  fetchArtStatus: vi.fn(),
  fetchArtPack: vi.fn(),
  uploadArt: vi.fn(),
  resetArt: vi.fn(),
}));

import * as art from '../src/device/art';
import * as active from '../src/jcard/active';
import { FACTORY_JCARDS } from '../src/jcard/factory';
import { activePool, designFor, resetUserJcards, userJcards } from '../src/jcard/pool';
import { faceIndexFor } from '../src/jcard/seed';
import {
  dropCachedArtPack,
  putCachedArtPack,
  readCachedArtPack,
} from '../src/storage/artPacks';
import { buildPack, seedDesigns } from './fixtures/art-pack';

const FACTORY_STATUS: ArtStatus = {
  installed: false,
  valid: true,
  name: '',
  author: '',
  designCount: 0,
  sheetCrc: 0,
  factoryCount: FACTORY_JCARDS.length,
  why: '',
};

function installedStatus(pack: ArtPack): ArtStatus {
  return {
    installed: true,
    valid: true,
    name: pack.name,
    author: pack.author,
    designCount: pack.designCount,
    sheetCrc: pack.sheetCrc,
    factoryCount: FACTORY_JCARDS.length,
    why: '',
  };
}

const PACK_A = buildPack({
  designs: seedDesigns(1),
  packId: 'pack-a',
  name: 'Pack A',
  author: 'Mara Vidal',
});

const PACK_B = buildPack({
  designs: seedDesigns(2),
  packId: 'pack-b',
  name: 'Pack B',
  author: 'Ilse Korr',
});

/** The ten reference albums, used to reach the user half of the pool. */
const REFERENCE: Array<[string, string]> = [
  ['Night Drive', 'Neon Harbor'],
  ['Sunday Tapes', 'Mara Vidal'],
  ['Lo-Fi Vol. 2', 'Various'],
  ['Analog Dreams', 'Ilse Korr'],
  ['Side Project', 'K. Ando'],
  ['Dub Plate 04', 'Sub Halo'],
  ['Midnight FM', 'Radio Varo'],
  ['Glass Coast', 'Ola Brenn'],
  ['Heat Wave', 'Tessa Lund'],
  ['Quiet Engine', 'Ruben Ito'],
];

function userDesignIds(): string[] {
  return userJcards().map((design) => design.id);
}

const status = vi.mocked(art.fetchArtStatus);
const packBytes = vi.mocked(art.fetchArtPack);
const upload = vi.mocked(art.uploadArt);
const reset = vi.mocked(art.resetArt);

beforeEach(async () => {
  vi.resetAllMocks();
  resetUserJcards();
  await dropCachedArtPack();
  status.mockResolvedValue(FACTORY_STATUS);
  await active.refreshArtPacks();
});

describe('the cached pack', () => {
  it('draws before the player has answered', async () => {
    await putCachedArtPack(PACK_A);
    resetUserJcards();

    await active.restoreArtPacks();

    expect(userDesignIds()).toEqual(PACK_A.designs.map((design) => design.id));
    expect(activePool()).toHaveLength(FACTORY_JCARDS.length + 1);
    expect(active.artPackState().cached?.packId).toBe('pack-a');
  });

  it('is dropped when its J-cards no longer parse, leaving the factory', async () => {
    await putCachedArtPack({ ...PACK_A, jcards: { v: 2, designs: [] } });
    resetUserJcards();

    await active.restoreArtPacks();

    expect(activePool()).toEqual(FACTORY_JCARDS);
    expect(await readCachedArtPack()).toBeNull();
  });
});

describe('the player’s answer', () => {
  it('installs the active pack, caches it, and re-deals the pool', async () => {
    status.mockResolvedValue(installedStatus(PACK_A));
    packBytes.mockResolvedValue(PACK_A.bytes);

    await active.refreshArtPacks();

    expect(userDesignIds()).toEqual(['k7.test-01']);
    expect(activePool()).toHaveLength(FACTORY_JCARDS.length + 1);
    expect(active.artPackState()).toMatchObject({
      reachable: true,
      loading: false,
      error: null,
      status: { name: 'Pack A', designCount: 1 },
    });
    expect((await readCachedArtPack())?.packId).toBe('pack-a');

    /* The album's stable pair now reaches the user design: the hash is over the whole pool,
     * factory first (art-packs.md §6). */
    const pair = REFERENCE.find(
      ([title, artist]) =>
        faceIndexFor(title, artist, FACTORY_JCARDS.length + 1) === FACTORY_JCARDS.length,
    );
    expect(pair).toBeDefined();
    expect(designFor(pair![0], pair![1])?.id).toBe('k7.test-01');
  });

  it('keeps a cached pack whose bytes the player repeats', async () => {
    await putCachedArtPack(PACK_A);
    await active.restoreArtPacks();
    status.mockResolvedValue(installedStatus(PACK_A));
    packBytes.mockResolvedValue(PACK_A.bytes);

    await active.refreshArtPacks();

    expect(userDesignIds()).toEqual(['k7.test-01']);
    expect((await readCachedArtPack())?.key).toBe(
      `${PACK_A.packCrc.toString(16)}:${PACK_A.size}`,
    );
  });

  it('drops the user pack and the cache when the player reports the factory', async () => {
    await putCachedArtPack(PACK_A);
    await active.restoreArtPacks();
    status.mockResolvedValue(FACTORY_STATUS);

    await active.refreshArtPacks();

    expect(activePool()).toEqual(FACTORY_JCARDS);
    expect(await readCachedArtPack()).toBeNull();
    expect(active.artPackState().cached).toBeNull();
  });

  it('drops the user pack when the player refused the pack it holds', async () => {
    await putCachedArtPack(PACK_A);
    await active.restoreArtPacks();
    status.mockResolvedValue({
      ...FACTORY_STATUS,
      installed: true,
      valid: false,
      why: 'the pack’s checksum disagrees',
    });

    await active.refreshArtPacks();

    expect(activePool()).toEqual(FACTORY_JCARDS);
    expect(await readCachedArtPack()).toBeNull();
    expect(active.artPackState().status?.why).toBe('the pack’s checksum disagrees');
  });

  it('keeps the cache and the pool when the player is unreachable', async () => {
    await putCachedArtPack(PACK_A);
    await active.restoreArtPacks();
    status.mockRejectedValue(
      new DeviceError('the player did not answer; is it on the same network?', 'unreachable', 0),
    );

    await active.refreshArtPacks();

    expect(userDesignIds()).toEqual(['k7.test-01']);
    expect(active.artPackState()).toMatchObject({ reachable: false, error: null });
    expect(await readCachedArtPack()).not.toBeNull();
  });

  it('keeps the drawing pool when the pack’s bytes do not read', async () => {
    await putCachedArtPack(PACK_A);
    await active.restoreArtPacks();
    status.mockResolvedValue(installedStatus(PACK_A));
    packBytes.mockResolvedValue(new Uint8Array([1, 2, 3]));

    await active.refreshArtPacks();

    expect(userDesignIds()).toEqual(['k7.test-01']);
    expect(active.artPackState().reachable).toBe(true);
    expect(active.artPackState().error).toMatch(/could not be read/);
  });

  it('names a player that answered the status route with a refusal', async () => {
    status.mockRejectedValue(new DeviceError('no such path', 'refused', 404));

    await active.refreshArtPacks();

    expect(active.artPackState()).toMatchObject({
      reachable: false,
      error: 'no such path',
    });
  });
});

describe('installing and resetting', () => {
  it('adopts an installed pack only after the player answers', async () => {
    upload.mockResolvedValue(installedStatus(PACK_B));

    await expect(active.uploadArtPack(PACK_B)).resolves.toMatchObject({ designCount: 2 });
    expect(userDesignIds()).toEqual(['k7.test-01', 'k7.test-02']);
    expect(activePool()).toHaveLength(FACTORY_JCARDS.length + 2);
    expect((await readCachedArtPack())?.packId).toBe('pack-b');
    expect(active.artPackState().cached?.designCount).toBe(2);
    expect(active.artPackState().busy).toBe(false);
  });

  it('changes nothing when the player refuses the upload', async () => {
    upload.mockRejectedValue(
      new DeviceError(
        'the pack was built against 3 fonts; this firmware has 2',
        'refused',
        422,
      ),
    );

    await expect(active.uploadArtPack(PACK_B)).rejects.toMatchObject({
      status: 422,
      message: 'the pack was built against 3 fonts; this firmware has 2',
    });
    expect(activePool()).toEqual(FACTORY_JCARDS);
    expect(await readCachedArtPack()).toBeNull();
    expect(active.artPackState().busy).toBe(false);
  });

  it('erases the user pack and its cache on a reset', async () => {
    await putCachedArtPack(PACK_B);
    await active.restoreArtPacks();
    reset.mockResolvedValue(FACTORY_STATUS);

    await expect(active.resetArtPacks()).resolves.toEqual(FACTORY_STATUS);
    expect(activePool()).toEqual(FACTORY_JCARDS);
    expect(await readCachedArtPack()).toBeNull();
    expect(active.artPackState()).toMatchObject({
      status: FACTORY_STATUS,
      cached: null,
      busy: false,
    });
  });

  it('keeps the pack when the reset is refused', async () => {
    await putCachedArtPack(PACK_B);
    await active.restoreArtPacks();
    reset.mockRejectedValue(new DeviceError('the screen is busy; try again', 'refused', 503));

    await expect(active.resetArtPacks()).rejects.toMatchObject({ status: 503 });
    expect(userDesignIds()).toEqual(['k7.test-01', 'k7.test-02']);
    expect(await readCachedArtPack()).not.toBeNull();
    expect(active.artPackState().busy).toBe(false);
  });
});
