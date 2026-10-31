import { useEffect, useRef, useState } from 'react';
import { useDeviceStatus } from '../device/useDeviceStatus';
import { PlayerView } from './PlayerView';
import { PrepareView } from './PrepareView';
import { initialView, selectView, type View } from './view';

/**
 * The shell of the page: it holds the two views and the rule that moves between them.
 *
 * The Prepare view stays mounted while the Player view is shown. A card that arrives in
 * the middle of an analysis must not throw the queue away (S3), and hiding the component
 * keeps its worker and its queue exactly where they were.
 */
export function App() {
  const { status, reachable } = useDeviceStatus();
  const [view, setView] = useState<View>('prepare');
  const [queueBusy, setQueueBusy] = useState(false);
  const [switchOffered, setSwitchOffered] = useState(false);
  const [dismissed, setDismissed] = useState(false);
  const initialized = useRef(false);

  const desired = status ? initialView(status.card) : null;

  // S2: the first answer from the device chooses the view, once. If the device only
  // appears after the user has already started a queue, that first answer is a change
  // like any other and S3 applies to it.
  useEffect(() => {
    if (!status || initialized.current) return;
    initialized.current = true;
    const selection = selectView(view, status.card, !queueBusy);
    setView(selection.view);
    setSwitchOffered(selection.switchOffered);
  }, [status, queueBusy, view]);

  // S3: every later change switches only while the queue is idle; otherwise it is offered.
  useEffect(() => {
    if (!status || !initialized.current) return;
    const selection = selectView(view, status.card, !queueBusy);
    if (selection.view !== view) setView(selection.view);
    setSwitchOffered(selection.switchOffered && !dismissed);
  }, [status, queueBusy, view, dismissed]);

  // A change back to the view already on screen is a fresh state, not a dismissed offer.
  useEffect(() => {
    if (desired !== null && desired === view) setDismissed(false);
  }, [desired, view]);

  const switchNow = () => {
    if (!status) return;
    setView(initialView(status.card));
    setSwitchOffered(false);
  };

  return (
    <div className="views">
      {switchOffered && status && (
        <div className="app switch-app">
          <div className="banner">
            <span className="small">
              {status.card ? 'The card is in the player.' : 'The card is out of the player.'}{' '}
              The queue is still running.
            </span>
            <div className="actions">
              <button type="button" className="primary" onClick={switchNow}>
                Switch now
              </button>
              <button type="button" className="ghost" onClick={() => setDismissed(true)}>
                Not now
              </button>
            </div>
          </div>
        </div>
      )}

      <div className="view" hidden={view !== 'prepare'}>
        <PrepareView onBusyChange={setQueueBusy} />
      </div>

      {view === 'player' && <PlayerView status={status} reachable={reachable} />}
    </div>
  );
}
