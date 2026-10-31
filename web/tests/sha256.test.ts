import { describe, expect, it } from 'vitest';
import { sha256Bytes, sha256Hex } from '../src/lib/sha256';

const text = (value: string) => new TextEncoder().encode(value);

const vectors: Array<[string, string]> = [
  ['', 'e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855'],
  ['abc', 'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad'],
  [
    'abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq',
    '248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1',
  ],
];

describe('the hash the app uses when the browser withholds crypto.subtle', () => {
  it('matches the standard vectors', async () => {
    for (const [input, digest] of vectors) {
      expect(await sha256Hex(text(input))).toBe(digest);
    }
  });

  it('is the same algorithm on the slow path', () => {
    for (const [input, digest] of vectors) {
      const bytes = sha256Bytes(text(input));
      const hex = Array.from(bytes, (b) => b.toString(16).padStart(2, '0')).join('');
      expect(hex).toBe(digest);
    }
  });

  it('handles a message spanning many blocks', async () => {
    const long = new Uint8Array(1000).fill(0x61);
    expect(await sha256Hex(long)).toBe(
      '41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3',
    );
  });

  it('takes an ArrayBuffer as readily as a view', async () => {
    const bytes = text('xtape');
    expect(await sha256Hex(bytes.buffer as ArrayBuffer)).toBe(await sha256Hex(bytes));
  });
});
