import { describe, expect, it } from 'vitest';

import { DEVICE_NEEDS_PERMISSION, DEVICE_ORIGIN } from '../src/config';

/**
 * The build the test suite and the release share. `vite build` compiles the shipping shape
 * (the public origin, the gate), and only `mode === 'development'` sets the development
 * build's exception (ADR 0082). If the define is ever keyed on something else, this fails
 * before an artifact could ship with the gate stripped.
 */
describe('the shipping build', () => {
  it('reaches the device directly', () => {
    expect(DEVICE_ORIGIN).toBe('http://k7.local');
  });

  it('keeps the Local Network Access gate', () => {
    expect(DEVICE_NEEDS_PERMISSION).toBe(true);
  });
});
