import { UNSCOPED } from '../card/session';
import type { VirtualCard } from '../storage/vcards';

/**
 * The units the card bar can switch between, and which one is current.
 *
 * This is the pure half of the bar: the device's own card when it reports a marked one, the
 * SD in this computer (always offered, because an empty slot is still the way in), and every
 * virtual card. The bar renders it; the tests pin the selection rule without a browser.
 */

/** The part of the device's status the bar needs. */
export interface DeviceCardStatus {
  card: boolean;
  mark: string | null;
  tracks: number;
}

export type CardUnit =
  | { kind: 'device'; key: 'device'; label: string; tracks: number }
  | { kind: 'sd'; key: 'sd'; label: string; loaded: boolean }
  | { kind: 'virtual'; key: string; label: string; card: VirtualCard };

export interface CardUnitsInput {
  /** The virtual cards this browser remembers, oldest first. */
  cards: readonly VirtualCard[];
  device: DeviceCardStatus | null;
  /** The scope this session is working on: a card mark, a fingerprint, or `UNSCOPED`. */
  scope: string;
  /** True while the player's card is the card being viewed. */
  deviceView: boolean;
  /** True when this visit opened the SD read-write, so selecting it needs no picker. */
  hasCardHandle: boolean;
}

export interface CardUnits {
  units: CardUnit[];
  /** The key of the current unit, or null when no card is selected. */
  activeKey: string | null;
}

/** The units, in the order the shelf shows them, plus the one that is current. */
export function cardUnits(input: CardUnitsInput): CardUnits {
  const virtual = input.cards.find((card) => card.scope === input.scope);
  /* A real SD is current when the scope names something that is not a virtual card. A
   * dropped card (a fingerprint) is read-only but is still the card being worked on. */
  const activeSd = !input.deviceView && input.scope !== UNSCOPED && virtual === undefined;

  const units: CardUnit[] = [];
  if (input.device?.card === true && input.device.mark !== null) {
    units.push({
      kind: 'device',
      key: 'device',
      label: 'Device',
      tracks: input.device.tracks,
    });
  }
  units.push({ kind: 'sd', key: 'sd', label: 'SD card', loaded: input.hasCardHandle || activeSd });
  for (const card of input.cards) {
    units.push({
      kind: 'virtual',
      key: `virtual:${card.id}`,
      label: card.label.trim() || card.folder,
      card,
    });
  }

  let activeKey: string | null = null;
  if (input.deviceView) activeKey = 'device';
  else if (virtual !== undefined) activeKey = `virtual:${virtual.id}`;
  else if (activeSd) activeKey = 'sd';
  /* The device view outlives the card it viewed. A phantom current unit is worse than none:
   * the bar must never accuse a card it cannot offer. */
  if (activeKey !== null && !units.some((unit) => unit.key === activeKey)) activeKey = null;

  return { units, activeKey };
}
