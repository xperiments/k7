import { describe, expect, it } from 'vitest';
import type { DeviceStatus } from '../src/device/status';
import { classifyStreamFailure } from '../src/ui/stream-slot';

/* A status body with only what the rule reads; everything else is the device's own. */
function statusWith(slots?: { used: number; slots: number }): DeviceStatus {
  return {
    card: true,
    mark: null,
    tracks: 1,
    generation: 1,
    library: 'ok',
    order: 'tree',
    playlist: null,
    playing: null,
    ...(slots ? { listeners: slots } : {}),
  };
}

describe('what a live stream error means', () => {
  it('is the station saying no when the stream never played and every slot is taken', () => {
    expect(classifyStreamFailure(false, statusWith({ used: 1, slots: 1 }))).toBe('busy');
  });

  it('is a dropped link once this page has played, even if its own slot is still counted', () => {
    expect(classifyStreamFailure(true, statusWith({ used: 1, slots: 1 }))).toBe('drop');
  });

  it('is a dropped link when the slot is free', () => {
    expect(classifyStreamFailure(false, statusWith({ used: 0, slots: 1 }))).toBe('drop');
  });

  it('does not guess when the device never answered', () => {
    expect(classifyStreamFailure(false, null)).toBe('drop');
  });

  it('reads firmware that does not report its slots as a drop, not as a full station', () => {
    expect(classifyStreamFailure(false, statusWith())).toBe('drop');
    expect(classifyStreamFailure(false, statusWith({ used: 0, slots: 0 }))).toBe('drop');
  });
});
