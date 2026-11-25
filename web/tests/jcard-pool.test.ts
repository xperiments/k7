import { afterEach, describe, expect, it, vi } from 'vitest';

import { FACTORY_JCARDS, factoryDesigns } from '../src/jcard/factory';
import {
  activePool,
  designFor,
  pickDesign,
  resetUserJcards,
  setUserJcardPack,
  setUserJcards,
  subscribePool,
  userJcards,
} from '../src/jcard/pool';
import { faceIndexFor } from '../src/jcard/seed';

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

/** Every factory file's id, so the test fails if a design is silently left out. */
const ASSET_IDS = Object.values(
  import.meta.glob('../../assets/k7art/*.json', {
    eager: true,
    import: 'id',
  }) as Record<string, string>,
).sort();

/** The factory files as they are written: a user pack's design is the raw K7Art's shape. */
const RAW = import.meta.glob('../../assets/k7art/*.json', {
  eager: true,
}) as Record<string, Record<string, unknown>>;

/** The first factory design, used as the shape of a second, user-pack design. */
function secondDesign() {
  const key = Object.keys(RAW).sort()[0];
  const first = key ? RAW[key] : undefined;
  if (!first) throw new Error('the factory has no J-card to clone');
  return { ...first, id: 'k7.second', name: 'Second' };
}

afterEach(() => {
  resetUserJcards();
});

describe('the factory J-cards', () => {
  it('bundles every design in assets/k7art in id order', () => {
    const designs = factoryDesigns();
    const ids = designs.map((design) => design.id);
    expect(ids).toEqual(ASSET_IDS);
    expect(ids).toEqual([...ids].sort());
    for (const design of designs) {
      expect(design.id).toMatch(/^k7\.[a-z0-9][a-z0-9-]*$/);
      expect(design.jcard.space).toEqual({ w: 1300, h: 1280, unit: 320 });
      expect(design.jcard.background).toMatch(/^#[0-9a-f]{6}$/i);
      expect(design.jcard.fonts.numeral.size).toBeGreaterThanOrEqual(4);
      for (const prim of design.jcard.prims) {
        expect(prim.t === 'rect' ? prim.fill : prim.color).toMatch(/^#[0-9a-f]{6}$/i);
      }
    }
  });

  it('keeps the factory alone the active pool', () => {
    expect(activePool()).toEqual(FACTORY_JCARDS);
    expect(userJcards()).toEqual([]);
  });
});

describe('the Cover pool', () => {
  it('puts the factory first and the user pack behind it', () => {
    const base = FACTORY_JCARDS[0];
    if (!base) throw new Error('the factory has no J-card');
    const user = { ...base, id: 'k7.user', name: 'User' };
    setUserJcards([user]);

    expect(activePool()).toEqual([...FACTORY_JCARDS, user]);
    expect(userJcards()).toEqual([user]);
  });

  it('installs a parsed pack and resets to the factory', () => {
    const installed = setUserJcardPack({ v: 2, designs: [secondDesign()] });
    expect(installed).toHaveLength(1);
    expect(activePool()).toHaveLength(FACTORY_JCARDS.length + 1);
    expect(activePool().at(-1)?.id).toBe('k7.second');

    resetUserJcards();
    expect(activePool()).toEqual(FACTORY_JCARDS);
    expect(userJcards()).toEqual([]);
  });

  it('changes nothing when a pack payload is not a pack', () => {
    expect(setUserJcardPack({ v: 1, designs: [] })).toBeNull();
    expect(activePool()).toEqual(FACTORY_JCARDS);
  });

  it('notifies its subscribers when the active pack changes', () => {
    const listener = vi.fn();
    const off = subscribePool(listener);
    setUserJcards([]);
    resetUserJcards();
    off();
    setUserJcards([]);
    expect(listener).toHaveBeenCalledTimes(2);
  });

  it('picks by the pair, modulo the pool size, factory first', () => {
    const user = { ...FACTORY_JCARDS[0]!, id: 'k7.user', name: 'User' };
    setUserJcards([user]);
    const pool = activePool();

    for (const [title, artist] of REFERENCE) {
      const picked = pickDesign(pool, title, artist);
      const index = faceIndexFor(title, artist, pool.length);
      expect(picked).toBe(pool[index]);
      expect(picked).toBe(designFor(title, artist, pool));
    }

    /* A user design is reached: there is a pair whose index is the first user slot. */
    const userPair = REFERENCE.find(
      ([title, artist]) => faceIndexFor(title, artist, pool.length) === FACTORY_JCARDS.length,
    );
    expect(userPair).toBeDefined();
    expect(pickDesign(pool, ...userPair!)).toBe(user);

    /* The same pair always gets the same design, and the factory order never moves. */
    const pair = REFERENCE[0]!;
    expect(pickDesign(pool, ...pair)).toBe(pickDesign(pool, ...pair));
    expect(pool.slice(0, FACTORY_JCARDS.length)).toEqual(FACTORY_JCARDS);
  });

  it('has no design when there is no pool', () => {
    expect(pickDesign([], 'Night Drive', 'Neon Harbor')).toBeNull();
  });
});
