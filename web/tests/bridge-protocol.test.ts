import { describe, expect, it } from 'vitest';
import {
  BRIDGE_VERSION,
  OP_SPECS,
  bye,
  fail,
  hello,
  isOpName,
  ok,
  parseMessage,
  req,
  welcome,
  type ErrorCode,
  type Message,
  type OpName,
  type ResponseMessage,
} from '../src/bridge/protocol';

/** §3, transcribed. A test that reads the table from the implementation proves nothing. */
const OPS: OpName[] = [
  'status',
  'playlists',
  'playlist',
  'select',
  'rescan',
  'play',
  'library.search',
  'library.list',
  'library.stats',
  'library.track',
  'stream.play',
  'stream.pause',
  'stream.volume',
  'stream.state',
];

/** §4, transcribed: the closed vocabulary, one code at a time. */
const CODES: ErrorCode[] = [
  'bad-message',
  'unknown-op',
  'bad-args',
  'too-large',
  'timeout',
  'unreachable',
  'refused',
  'blocked',
  'version',
];

/** §3's timeout column, in milliseconds. */
const TIMEOUTS: Record<OpName, number> = {
  status: 4_000,
  playlists: 6_000,
  playlist: 6_000,
  select: 10_000,
  rescan: 10_000,
  play: 10_000,
  'library.search': 10_000,
  'library.list': 10_000,
  'library.stats': 10_000,
  'library.track': 10_000,
  'stream.play': 2_000,
  'stream.pause': 2_000,
  'stream.volume': 2_000,
  'stream.state': 2_000,
};

function parsed(value: unknown): Message {
  const message = parseMessage(value);
  if (message === null) throw new Error(`expected a message, got null for ${JSON.stringify(value)}`);
  return message;
}

function responseOf(value: unknown): ResponseMessage {
  const message = parsed(value);
  if (message.t !== 'res') throw new Error(`expected a res, got ${message.t}`);
  return message;
}

describe('the bridge wire', () => {
  it('is version 1', () => {
    expect(BRIDGE_VERSION).toBe(1);
  });

  it('knows exactly the fourteen operations, with the spec timeouts', () => {
    expect(Object.keys(OP_SPECS).sort()).toEqual([...OPS].sort());
    for (const op of OPS) {
      expect(OP_SPECS[op].timeout).toBe(TIMEOUTS[op]);
    }
  });

  it('recognises an operation name and nothing else', () => {
    for (const op of OPS) expect(isOpName(op)).toBe(true);
    for (const value of ['', 'status ', 'library.delete', 'control_play', 'toString', '__proto__']) {
      expect(isOpName(value)).toBe(false);
    }
    for (const value of [null, undefined, 7, {}, [], true]) {
      expect(isOpName(value)).toBe(false);
    }
  });
});

describe('parseMessage', () => {
  it('parses each message type', () => {
    expect(parseMessage({ t: 'hello', v: 1, role: 'app', app: 'https://k7.xperiments.dev' })).toEqual({
      t: 'hello',
      v: 1,
      role: 'app',
      app: 'https://k7.xperiments.dev',
    });
    expect(parseMessage({ t: 'welcome', v: 1, role: 'launcher', audio: { playing: false, volume: 1 } })).toEqual({
      t: 'welcome',
      v: 1,
      role: 'launcher',
      audio: { playing: false, volume: 1 },
    });
    expect(parseMessage({ t: 'req', id: '7', op: 'status', args: null })).toEqual({
      t: 'req',
      id: '7',
      op: 'status',
      args: null,
    });
    expect(parseMessage({ t: 'res', id: '7', ok: true, data: { tracks: 3 } })).toEqual({
      t: 'res',
      id: '7',
      ok: true,
      data: { tracks: 3 },
    });
    expect(
      parseMessage({ t: 'res', id: '7', ok: false, error: { code: 'timeout', message: 'no answer' } }),
    ).toEqual({ t: 'res', id: '7', ok: false, error: { code: 'timeout', message: 'no answer' } });
    expect(parseMessage({ t: 'bye' })).toEqual({ t: 'bye' });
  });

  it('ignores unknown fields instead of carrying them', () => {
    expect(parsed({ t: 'hello', v: 1, role: 'app', extra: 1, nested: { a: true } })).toEqual(hello('app'));
    expect(parsed({ t: 'req', id: '1', op: 'status', args: null, from: 'nowhere' })).toEqual(req('1', 'status'));
    expect(parsed({ t: 'res', id: '1', ok: true, data: [], note: 'x' })).toEqual(ok('1', []));
    expect(parsed({ t: 'res', id: '1', ok: false, error: { code: 'refused', message: 'no', status: 409 }, x: 1 })).toEqual(
      fail('1', 'refused', 'no', 409),
    );
    expect(parsed({ t: 'bye', why: 'pagehide' })).toEqual(bye());
  });

  it('rejects wrong types, missing fields and unknown codes', () => {
    const malformed: unknown[] = [
      null,
      undefined,
      42,
      'hello',
      true,
      [],
      {},
      { t: 7 },
      { t: 'shout' },

      { t: 'hello' },
      { t: 'hello', v: '1', role: 'app' },
      { t: 'hello', v: 1.5, role: 'app' },
      { t: 'hello', v: 0, role: 'app' },
      { t: 'hello', v: 1, role: 'device' },
      { t: 'hello', v: 1, role: 'app', app: 42 },

      { t: 'welcome', v: 1, role: 'launcher' },
      { t: 'welcome', v: 1, role: 'launcher', audio: { playing: 'no', volume: 1 } },
      { t: 'welcome', v: 1, role: 'launcher', audio: { playing: true, volume: '1' } },
      { t: 'welcome', v: 1, role: 'launcher', audio: null },

      { t: 'req' },
      { t: 'req', id: 1, op: 'status', args: null },
      { t: 'req', id: '', op: 'status', args: null },
      { t: 'req', id: '1', op: 42, args: null },
      { t: 'req', id: '1', op: 'library.delete', args: null },
      { t: 'req', id: '1', op: 'status' },
      { t: 'req', id: '1', op: 'status', args: [] },
      { t: 'req', id: '1', op: 'status', args: 'x' },

      { t: 'res', id: '1' },
      { t: 'res', id: '1', ok: 'true', data: {} },
      { t: 'res', id: '1', ok: true },
      { t: 'res', id: '1', ok: false },
      { t: 'res', id: '1', ok: false, error: {} },
      { t: 'res', id: '1', ok: false, error: { code: 'timeout' } },
      { t: 'res', id: '1', ok: false, error: { code: 'teapot', message: 'no' } },
      { t: 'res', id: '1', ok: false, error: { code: 'timeout', message: 7 } },
      { t: 'res', id: '1', ok: false, error: { code: 'refused', message: 'no', status: '409' } },
      { t: 'res', id: '1', ok: false, error: { code: 'refused', message: 'no', status: 409.5 } },
    ];
    for (const value of malformed) {
      expect(parseMessage(value), `expected null for ${JSON.stringify(value)}`).toBeNull();
    }
  });

  it('rejects an op outside the table; the id survives so the peer can answer', () => {
    expect(parseMessage({ t: 'req', id: '7', op: 'library.delete', args: null })).toBeNull();
    expect(parseMessage({ t: 'req', id: '7', op: 'control_play', args: { path: 'a' } })).toBeNull();
  });

  it('does not police duplicate ids, because a session owns that (X6)', () => {
    // §X6 puts duplicate in-flight ids on the peer, not on the wire. The shape has two
    // legal requests with the same id; the App and the Launcher track what is in flight.
    expect(parseMessage(req('7', 'status'))).toEqual(req('7', 'status'));
    expect(parseMessage(req('7', 'status'))).not.toBeNull();
  });

  it('reaches every error code in the closed vocabulary, and refuses invented ones', () => {
    expect(CODES).toHaveLength(9);
    for (const code of CODES) {
      const message = fail('1', code, 'a displayable sentence');
      expect(message).toEqual({ t: 'res', id: '1', ok: false, error: { code, message: 'a displayable sentence' } });
      expect(parseMessage(message)).toEqual(message);
    }
    expect(parseMessage({ t: 'res', id: '1', ok: false, error: { code: 'teapot', message: 'no' } })).toBeNull();
  });

  it('carries the device status only on a refusal that had one', () => {
    expect(parseMessage({ t: 'res', id: '1', ok: false, error: { code: 'refused', message: 'no', status: 409 } })).toEqual(
      { t: 'res', id: '1', ok: false, error: { code: 'refused', message: 'no', status: 409 } },
    );
    expect(parseMessage(fail('1', 'timeout', 'no'))).toEqual({
      t: 'res',
      id: '1',
      ok: false,
      error: { code: 'timeout', message: 'no' },
    });
  });
});

describe('the constructors', () => {
  it('stamp the version and round-trip through the parser', () => {
    expect(hello('app')).toEqual({ t: 'hello', v: BRIDGE_VERSION, role: 'app' });
    expect(hello('app', 'https://k7.xperiments.dev')).toEqual({
      t: 'hello',
      v: BRIDGE_VERSION,
      role: 'app',
      app: 'https://k7.xperiments.dev',
    });
    expect(welcome({ playing: true, volume: 0.4 })).toEqual({
      t: 'welcome',
      v: BRIDGE_VERSION,
      role: 'launcher',
      audio: { playing: true, volume: 0.4 },
    });
    expect(req('7', 'play', { path: 'music/a.mp3' })).toEqual({
      t: 'req',
      id: '7',
      op: 'play',
      args: { path: 'music/a.mp3' },
    });
    expect(req('7', 'status', null)).toEqual({ t: 'req', id: '7', op: 'status', args: null });
    expect(ok('7', null)).toEqual({ t: 'res', id: '7', ok: true, data: null });
    expect(fail('7', 'refused', 'no', 409)).toEqual({
      t: 'res',
      id: '7',
      ok: false,
      error: { code: 'refused', message: 'no', status: 409 },
    });
    expect(bye()).toEqual({ t: 'bye' });

    for (const message of [
      hello('launcher'),
      welcome({ playing: false, volume: 1 }),
      req('1', 'play', { path: 'A/x.mp3' }),
      ok('1', {}),
      fail('1', 'blocked', 'press play'),
      bye(),
    ]) {
      expect(parseMessage(message)).toEqual(message);
    }
  });

  it('tolerates a peer on another version instead of calling it malformed (W8)', () => {
    expect(parseMessage(hello('app', undefined))).toEqual(hello('app'));
    expect(parseMessage({ t: 'hello', v: BRIDGE_VERSION + 1, role: 'app' })).toEqual({
      t: 'hello',
      v: 2,
      role: 'app',
    });
    expect(parseMessage({ t: 'welcome', v: 99, role: 'launcher', audio: { playing: false, volume: 1 } })).toEqual({
      t: 'welcome',
      v: 99,
      role: 'launcher',
      audio: { playing: false, volume: 1 },
    });
  });

});

describe('the App-side response reader', () => {
  it('narrows ok responses to their data and failures to their code', () => {
    const good = responseOf(ok('1', { cards: [] }));
    if (!good.ok) throw new Error('expected ok');
    expect(good.data).toEqual({ cards: [] });

    const bad = responseOf(fail('1', 'version', 'firmware and app disagree'));
    if (bad.ok) throw new Error('expected a failure');
    expect(bad.error.code).toBe('version');
    expect(bad.error.status).toBeUndefined();
  });
});
