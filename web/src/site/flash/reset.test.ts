import { describe, expect, it } from 'vitest';

import { resetIntoApp } from './reset';

describe('the reset that ends a flash', () => {
  it('moves the enable line instead of releasing a line that is already released', async () => {
    const calls: [boolean | undefined, boolean | undefined][] = [];
    const waits: number[] = [];

    await resetIntoApp(
      {
        async setSignals(dtr, rts) {
          calls.push([dtr, rts]);
        },
      },
      async (ms) => {
        waits.push(ms);
      },
    );

    /* EN low (RTS high) with IO0 high, then EN high: the edge the board boots on. */
    expect(calls).toEqual([
      [false, true],
      [false, false],
    ]);
    expect(waits).toEqual([150, 150]);
  });
});
