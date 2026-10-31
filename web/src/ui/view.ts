/** The page has two views, and the Card state is the one fact that chooses between them. */
export type View = 'prepare' | 'player';

export function initialView(card: boolean): View {
  return card ? 'player' : 'prepare';
}

export interface ViewSelection {
  view: View;
  /** A later Card-state change arrived while the queue was working. */
  switchOffered: boolean;
}

/**
 * S2 chooses the view at load; S3 applies to every change after it. A change switches only
 * while the queue is idle. Mid-analysis the page stays, and the caller may offer the
 * switch — switching is the user's decision, not a side effect of a poll.
 */
export function selectView(current: View, card: boolean, idle: boolean): ViewSelection {
  const desired = initialView(card);
  if (desired === current) return { view: current, switchOffered: false };
  if (idle) return { view: desired, switchOffered: false };
  return { view: current, switchOffered: true };
}
