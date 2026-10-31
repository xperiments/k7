import { describe, expect, it } from 'vitest';
import { newId } from '../src/lib/id';

describe('a queue item identity on an origin without crypto.randomUUID', () => {
  it('is a v4 uuid shape', () => {
    expect(newId()).toMatch(/^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/);
  });

  it('is different every time', () => {
    const ids = new Set(Array.from({ length: 500 }, () => newId()));
    expect(ids.size).toBe(500);
  });
});
