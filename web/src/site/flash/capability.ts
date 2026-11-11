/**
 * Whether this browser can flash at all (k7-site.md F1).
 *
 * Web Serial is the only route, and it exists in desktop Chromium: not Safari, not Firefox,
 * not any phone. The page asks this and says so, rather than offering a control that cannot
 * open a port.
 */
export interface SerialCapable {
  readonly serial?: { requestPort?: unknown };
}

export function canFlash(navigatorLike: SerialCapable = navigator): boolean {
  return typeof navigatorLike.serial?.requestPort === 'function';
}
