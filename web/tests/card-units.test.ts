import { describe, expect, it } from 'vitest';
import { UNSCOPED } from '../src/card/session';
import type { VirtualCard } from '../src/storage/vcards';
import { cardUnits, type CardUnitsInput } from '../src/ui/card-units';

const card = (overrides: Partial<VirtualCard> = {}): VirtualCard => ({
  id: 'f0b1c2d3-4e5f-4a6b-8c7d-9e0f1a2b3c4d',
  label: 'Road trip',
  folder: 'road-trip-f0b1c2d3',
  sizeBytes: 32_000_000_000,
  scope: 'mark:f0b1c2d3-4e5f-4a6b-8c7d-9e0f1a2b3c4d',
  created: '2026-11-12T00:00:00.000Z',
  ...overrides,
});

const device = { card: true, mark: 'device-card-mark', tracks: 81 };

const input = (overrides: Partial<CardUnitsInput> = {}): CardUnitsInput => ({
  cards: [],
  device: null,
  scope: UNSCOPED,
  deviceView: false,
  hasCardHandle: false,
  ...overrides,
});

describe('the card bar units', () => {
  it('always offers the SD, and nothing is current until a card is picked', () => {
    const { units, activeKey } = cardUnits(input());
    expect(units).toEqual([{ kind: 'sd', key: 'sd', label: 'SD card', loaded: false }]);
    expect(activeKey).toBeNull();
  });

  it('puts the player card first, and only when it reports a marked card', () => {
    expect(cardUnits(input({ device })).units.map((unit) => unit.key)).toEqual(['device', 'sd']);
    expect(
      cardUnits(input({ device: { ...device, mark: null } })).units.map((unit) => unit.key),
    ).toEqual(['sd']);
    expect(
      cardUnits(input({ device: { ...device, card: false } })).units.map((unit) => unit.key),
    ).toEqual(['sd']);
  });

  it('marks the device current only while it is the view and the device offers one', () => {
    expect(cardUnits(input({ device, deviceView: true })).activeKey).toBe('device');
    expect(cardUnits(input({ deviceView: true })).activeKey).toBeNull();
  });

  it('offers every virtual card after the SD, oldest first', () => {
    const first = card();
    const second = card({ id: 'aaaaaaaa-0000-4000-8000-000000000000', label: 'Kitchen' });
    const { units } = cardUnits(input({ cards: [first, second] }));
    expect(units.map((unit) => unit.key)).toEqual([
      'sd',
      `virtual:${first.id}`,
      `virtual:${second.id}`,
    ]);
  });

  it('marks the virtual card whose scope is the current one', () => {
    const current = card();
    expect(cardUnits(input({ cards: [current], scope: current.scope })).activeKey).toBe(
      `virtual:${current.id}`,
    );
  });

  it('falls back to the folder when a virtual card has no name', () => {
    const unnamed = card({ label: '   ' });
    const { units } = cardUnits(input({ cards: [unnamed] }));
    expect(units[1]).toMatchObject({ kind: 'virtual', label: unnamed.folder });
  });

  it('marks the SD current for a real card, a fingerprint or a dropped in-place card', () => {
    expect(cardUnits(input({ scope: 'mark:real-card' })).activeKey).toBe('sd');
    expect(cardUnits(input({ scope: 'fingerprint:abc123' })).activeKey).toBe('sd');
  });

  it('calls the SD loaded when it was opened this visit, even while a virtual card is current', () => {
    const current = card();
    const { units } = cardUnits(
      input({ cards: [current], scope: current.scope, hasCardHandle: true }),
    );
    expect(units.find((unit) => unit.kind === 'sd')).toMatchObject({ loaded: true });
  });

  it('reads a scope whose virtual card is gone as the SD, as the shelf does', () => {
    // A virtual card removed while its scope is still the remembered one leaves no unit to
    // mark; the SD is the only thing the scope can name, exactly as the shelf shows it.
    expect(cardUnits(input({ scope: 'mark:removed-card' })).activeKey).toBe('sd');
  });
});
