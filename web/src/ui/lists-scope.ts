import { useEffect } from 'react';

import { scopeFor } from '../card/mark';
import { useCardScope, useDeviceView } from '../card/session';
import { useDeviceStatus } from '../device/useDeviceStatus';
import { setListScope } from '../playlists/drafts';

/**
 * Which card the playlists on screen belong to.
 *
 * The same selection the Playlists area follows: the **Device card**'s mark when that card is
 * the one selected, the card in the computer otherwise. The draft store works under it, so a
 * pending edit is filed under the card the list belongs to rather than the card this browser
 * last worked on (ADR 0095).
 */
export function useListScope(): string {
  const card = useCardScope();
  const deviceView = useDeviceView();
  const { status } = useDeviceStatus();
  const scope = deviceView && status?.mark ? scopeFor({ id: status.mark }) : card;

  useEffect(() => {
    setListScope(scope);
  }, [scope]);

  return scope;
}
