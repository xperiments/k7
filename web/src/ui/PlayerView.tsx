import type { DeviceStatus } from '../device/status';

function clock(seconds: number | undefined): string | null {
  if (seconds === undefined) return null;
  const whole = Math.max(0, Math.floor(seconds));
  const minutes = Math.floor(whole / 60);
  return `${minutes}:${String(whole % 60).padStart(2, '0')}`;
}

/**
 * The Player view is a placeholder until the device has a player screen. It exists to make
 * the Card state visible and to name what is playing; it is deliberately not the analyser.
 */
export function PlayerView({
  status,
  reachable,
}: {
  status: DeviceStatus | null;
  reachable: boolean;
}) {
  const playing = status?.playing ?? null;
  const title = playing?.title?.trim() || playing?.path?.trim() || null;
  const elapsed = clock(playing?.elapsed);
  const duration = clock(playing?.duration);
  const time = elapsed && duration ? `${elapsed} / ${duration}` : elapsed;

  return (
    <div className="app">
      <header className="top">
        <div>
          <h1>
            xtape <span>player</span>
          </h1>
          <p className="muted small">The card is in the player.</p>
        </div>
        <div className="model-status small">
          <span className={reachable ? 'ok' : 'muted'}>
            {reachable ? 'device connected' : 'device unreachable'}
          </span>
        </div>
      </header>

      {!reachable && (
        <div className="banner">
          <span className="small">
            {status
              ? 'The device is off or unreachable. What you last saw is below.'
              : 'The device is off or unreachable. The Player view needs the device.'}
          </span>
        </div>
      )}

      <section className="result-card">
        <div className="result-head">
          <div className="result-title">
            <h2>{title ?? 'Nothing is playing'}</h2>
            {playing?.path && playing.path !== title && (
              <p className="muted small">{playing.path}</p>
            )}
            {time && <p className="muted small">{time}</p>}
          </div>
        </div>

        <p className="muted small">
          This is a placeholder. The player controls are still to be designed; the analyser
          is the other view, shown when the card is out.
        </p>

        <div className="player-actions">
          <a className="player-link" href="/stream">
            Open the live stream
          </a>
        </div>
      </section>

      <footer className="foot muted small">
        <span>Analysis runs locally. Models CC BY-NC 4.0 (MTG/UPF). Code AGPL-3.0.</span>
      </footer>
    </div>
  );
}
