import { describe, expect, it } from 'vitest';
import {
  MARK_NAME,
  MARK_VERSION,
  decideMark,
  fingerprintScope,
  isCardRoot,
  newCardMark,
  parseCardMark,
  scopeFor,
  serialiseCardMark,
  type CardMark,
  type RandomSource,
} from '../src/card/mark';

/**
 * The mark's fixtures are stated by hand, not derived from the implementation: an id is a
 * UUID v4, `created` is an ISO 8601 instant, and nothing else is required.
 */
const ID = 'f0b1c2d3-4e5f-4a6b-8c7d-9e0f1a2b3c4d';
const CREATED = '2026-11-03T01:20:00Z';

const mark = (overrides: Partial<CardMark> = {}): CardMark => ({
  v: 1,
  id: ID,
  created: CREATED,
  ...overrides,
});

/** A random source that fills the buffer, exactly as `crypto.getRandomValues` does. */
const filled = (byte: number): RandomSource => (bytes) => bytes.fill(byte);

describe('the mark file', () => {
  it('is named k7.card, with a format version of its own', () => {
    expect(MARK_NAME).toBe('k7.card');
    expect(MARK_VERSION).toBe(1);
  });
});

describe('parsing k7.card', () => {
  it('calls empty text absent', () => {
    expect(parseCardMark('')).toBe('absent');
  });

  it('treats whitespace as unparseable, not as absent', () => {
    // An empty file is a card without a mark. A file containing something we cannot read
    // is not - treating it as absent would overwrite it, and P1 forbids that.
    expect(parseCardMark('   ')).toBe('foreign');
    expect(parseCardMark('\n')).toBe('foreign');
  });

  it('reads a valid mark', () => {
    expect(parseCardMark(JSON.stringify(mark()))).toEqual(mark());
  });

  it('reads the optional label', () => {
    expect(parseCardMark(JSON.stringify(mark({ label: 'kitchen card' })))).toEqual(
      mark({ label: 'kitchen card' }),
    );
  });

  it('ignores fields it does not know', () => {
    const text = JSON.stringify({ ...mark(), label: 'kitchen card', checksum: 'abc123' });
    expect(parseCardMark(text)).toEqual(mark({ label: 'kitchen card' }));
  });

  it('ignores a label that is not a string, because the id is still usable', () => {
    const text = JSON.stringify({ ...mark(), label: 42 });
    expect(parseCardMark(text)).toEqual(mark());
  });

  it('rejects a valid-looking file whose id is missing or malformed', () => {
    const missing = JSON.stringify({ v: 1, created: CREATED });
    const numeric = JSON.stringify({ v: 1, id: 123, created: CREATED });
    const truncated = JSON.stringify({ v: 1, id: ID.slice(0, -1), created: CREATED });
    const words = JSON.stringify({ v: 1, id: 'not-a-uuid', created: CREATED });
    const empty = JSON.stringify({ v: 1, id: '', created: CREATED });

    for (const text of [missing, numeric, truncated, words, empty]) {
      expect(parseCardMark(text), text).toBe('foreign');
    }
  });

  it('rejects a file that is not an object', () => {
    for (const text of ['null', '[]', '"mark"', '42', 'true']) {
      expect(parseCardMark(text), text).toBe('foreign');
    }
  });

  it('rejects malformed JSON', () => {
    expect(parseCardMark('{')).toBe('foreign');
    expect(parseCardMark('{"v":1,')).toBe('foreign');
  });

  it('requires the fields our own shape carries', () => {
    expect(parseCardMark(JSON.stringify({ v: 1, id: ID }))).toBe('foreign');
    expect(parseCardMark(JSON.stringify({ id: ID, created: CREATED }))).toBe('foreign');
    expect(parseCardMark(JSON.stringify({ v: '1', id: ID, created: CREATED }))).toBe(
      'foreign',
    );
    expect(parseCardMark(JSON.stringify({ v: 0, id: ID, created: CREATED }))).toBe('foreign');
    expect(parseCardMark(JSON.stringify({ v: 1.5, id: ID, created: CREATED }))).toBe(
      'foreign',
    );
  });

  it('calls a mark from a newer format newer', () => {
    expect(parseCardMark(JSON.stringify({ v: 2, id: ID, created: CREATED }))).toBe('newer');
    // A newer format may spell its other fields however it likes; only the id has to be
    // something this build could name a scope after.
    expect(parseCardMark(JSON.stringify({ v: 2, id: ID }))).toBe('newer');
  });

  it('calls a newer version with no usable id foreign', () => {
    expect(parseCardMark(JSON.stringify({ v: 2, created: CREATED }))).toBe('foreign');
    expect(parseCardMark(JSON.stringify({ v: 2, id: 'nope', created: CREATED }))).toBe(
      'foreign',
    );
  });
});

describe('creating a mark', () => {
  it('writes a UUID v4 from the injected bytes', () => {
    expect(newCardMark(CREATED, undefined, filled(0x00)).id).toBe(
      '00000000-0000-4000-8000-000000000000',
    );
    expect(newCardMark(CREATED, undefined, filled(0xff)).id).toBe(
      'ffffffff-ffff-4fff-bfff-ffffffffffff',
    );
  });

  it('generates a UUID v4 without injected randomness', () => {
    const generated = newCardMark(CREATED);
    expect(generated.id).toMatch(
      /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/,
    );
    expect(newCardMark(CREATED).id).not.toBe(generated.id);
  });

  it('keeps the timestamp it was given and omits an absent label', () => {
    expect(newCardMark(CREATED, undefined, filled(0x11))).toEqual({
      v: 1,
      id: '11111111-1111-4111-9111-111111111111',
      created: CREATED,
    });
  });

  it('carries the label when it has one', () => {
    expect(newCardMark(CREATED, 'kitchen card', filled(0x11))).toEqual({
      v: 1,
      id: '11111111-1111-4111-9111-111111111111',
      created: CREATED,
      label: 'kitchen card',
    });
  });
});

describe('serialising a mark', () => {
  it('writes stable JSON in one field order', () => {
    expect(serialiseCardMark(mark())).toBe(
      `{"v":1,"id":"${ID}","created":"${CREATED}"}`,
    );
    expect(serialiseCardMark(mark({ label: 'kitchen card' }))).toBe(
      `{"v":1,"id":"${ID}","created":"${CREATED}","label":"kitchen card"}`,
    );
  });

  it('does not depend on the order the object was built in', () => {
    const reordered: CardMark = {
      label: 'kitchen card',
      created: CREATED,
      id: ID,
      v: 1,
    };
    expect(serialiseCardMark(reordered)).toBe(serialiseCardMark(mark({ label: 'kitchen card' })));
  });

  it('round-trips through the parser', () => {
    const labelled = mark({ label: 'kitchen card' });
    expect(parseCardMark(serialiseCardMark(labelled))).toEqual(labelled);
    expect(parseCardMark(serialiseCardMark(mark()))).toEqual(mark());
  });
});

describe('scopes', () => {
  it('prefixes a mark by its id', () => {
    expect(scopeFor(mark())).toBe(`mark:${ID}`);
  });

  it('prefixes a fingerprint by its hex', () => {
    expect(fingerprintScope('9e7fa3562dc401bb7b86facfdbcc32770048f0719668df0207c07826dfe8036e')).toBe(
      'fingerprint:9e7fa3562dc401bb7b86facfdbcc32770048f0719668df0207c07826dfe8036e',
    );
  });

  it('keeps the two concepts apart', () => {
    expect(scopeFor(mark())).not.toBe(fingerprintScope(ID));
    expect(scopeFor(mark())).not.toBe(fingerprintScope(mark().id));
  });
});

describe('a card root', () => {
  it('is a directory holding music', () => {
    expect(isCardRoot(['music'])).toBe(true);
    expect(isCardRoot(['k7.card', 'music', 'k7.db'])).toBe(true);
    expect(isCardRoot(['Music'])).toBe(true);
  });

  it('is not anything else', () => {
    expect(isCardRoot([])).toBe(false);
    expect(isCardRoot(['audio'])).toBe(false);
    expect(isCardRoot(['musics'])).toBe(false);
    expect(isCardRoot(['music.bak'])).toBe(false);
    expect(isCardRoot(['k7.card', 'k7.db'])).toBe(false);
  });
});

/**
 * §4's policy table, one case per row. `absent` is the only state that ever produces a
 * write, and only when the card can take one.
 */
describe('what the App does with the mark', () => {
  const created: CardMark = {
    v: 1,
    id: '00000000-0000-4000-8000-000000000000',
    created: CREATED,
  };
  const createOptions = { writable: true, now: CREATED, random: filled(0x00) };

  it('uses a valid mark and never rewrites it', () => {
    expect(decideMark(mark(), { writable: true })).toEqual({ action: 'use' });
    expect(decideMark(mark(), { writable: false })).toEqual({ action: 'use' });
  });

  it('creates a mark when there is none and the card is writable', () => {
    expect(decideMark('absent', createOptions)).toEqual({
      action: 'create',
      write: created,
    });
    expect(decideMark('absent', createOptions).write).toEqual(created);
  });

  it('carries a label into the mark it creates', () => {
    const decision = decideMark('absent', { ...createOptions, label: 'kitchen card' });
    expect(decision.write).toEqual({ ...created, label: 'kitchen card' });
  });

  it('leaves a read-only card unmarked', () => {
    expect(decideMark('absent', { writable: false })).toEqual({ action: 'unmarked' });
  });

  it('never overwrites an unparseable mark, even when the card is writable', () => {
    expect(decideMark('foreign', { writable: true })).toEqual({ action: 'unmarked' });
    expect(decideMark('foreign', { writable: false })).toEqual({ action: 'unmarked' });
  });

  it('never overwrites a newer mark, even when the card is writable', () => {
    expect(decideMark('newer', { writable: true })).toEqual({ action: 'unmarked' });
    expect(decideMark('newer', { writable: false })).toEqual({ action: 'unmarked' });
  });

  it('only ever writes when it creates', () => {
    for (const state of [mark(), 'absent', 'foreign', 'newer'] as const) {
      for (const writable of [true, false]) {
        const decision = decideMark(state, { ...createOptions, writable });
        if (decision.action === 'create') {
          expect(decision.write?.v).toBe(1);
        } else {
          expect(decision.write).toBeUndefined();
        }
      }
    }
  });
});
