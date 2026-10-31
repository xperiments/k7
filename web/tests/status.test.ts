import { describe, expect, it } from 'vitest';
import { parseStatus, parseStatusText } from '../src/device/status';

const valid = {
  card: true,
  tracks: 12,
  generation: 3,
  library: 'sha256:abc',
  playing: {
    title: 'A track',
    path: 'music/Artist/a.mp3',
    elapsed: 12.5,
    duration: 203,
  },
};

describe('the device status contract', () => {
  it('reads the card state, the library and what is playing', () => {
    expect(parseStatus(valid)).toEqual({
      card: true,
      tracks: 12,
      generation: 3,
      library: 'sha256:abc',
      playing: {
        title: 'A track',
        path: 'music/Artist/a.mp3',
        elapsed: 12.5,
        duration: 203,
      },
    });
  });

  it('accepts a card that is out with nothing playing', () => {
    expect(parseStatus({ ...valid, card: false, playing: null })).toMatchObject({
      card: false,
      playing: null,
    });
  });

  it('treats a missing playing value as nothing playing', () => {
    const { playing: _playing, ...withoutPlaying } = valid;
    expect(parseStatus(withoutPlaying)?.playing).toBeNull();
  });

  it('treats an unrecognised playing value as nothing playing', () => {
    expect(parseStatus({ ...valid, playing: 'a string' })?.playing).toBeNull();
    expect(parseStatus({ ...valid, playing: { elapsed: 'soon' } })?.playing).toEqual({});
  });

  it('ignores fields the page does not know', () => {
    expect(parseStatus({ ...valid, future: true })).toEqual(valid);
  });

  it('rejects a response without a boolean card state', () => {
    expect(parseStatus({ ...valid, card: 'yes' })).toBeNull();
    expect(parseStatus({ ...valid, card: undefined })).toBeNull();
  });

  it('rejects a response without a library name', () => {
    expect(parseStatus({ ...valid, library: 7 })).toBeNull();
  });

  it('rejects a response whose counts are not numbers', () => {
    expect(parseStatus({ ...valid, tracks: '12' })).toBeNull();
    expect(parseStatus({ ...valid, generation: Number.NaN })).toBeNull();
  });

  it('rejects a body that is not an object', () => {
    expect(parseStatus(null)).toBeNull();
    expect(parseStatus([])).toBeNull();
    expect(parseStatus('{}')).toBeNull();
  });

  it('rejects text that is not JSON and text that is not this contract', () => {
    expect(parseStatusText('<html></html>')).toBeNull();
    expect(parseStatusText('{"card":true}')).toBeNull();
    expect(parseStatusText(JSON.stringify(valid))).toEqual(valid);
  });
});
