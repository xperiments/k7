import { useState } from 'react';
import {
  scanDataTransfer,
  scanDirectoryHandle,
  type CardDirectoryHandle,
  type FoundFile,
} from './files';
import { cardPicker, pickCardDirectory, type SnapshotDirectory } from './cardWriter';

interface DropZoneProps {
  disabled: boolean;
  /** A folder dropped in: measurable, but read-only, so the snapshot is downloaded. */
  onFiles: (found: FoundFile[], skipped: number) => void;
  /** The card, opened read-write: measurable and writable. */
  onCard: (found: FoundFile[], skipped: number, handle: SnapshotDirectory) => void;
}

/**
 * The way in: the card.
 *
 * The page exists to measure a card and hand the measurements back to it, so the card is
 * what it asks for — once, with write permission. Dropping still works, because a folder
 * from anywhere is measurable, but a dropped handle is read-only for good: that way in
 * ends at a download rather than at the card.
 */
export function DropZone({ disabled, onFiles, onCard }: DropZoneProps) {
  const [over, setOver] = useState(false);
  const [scanning, setScanning] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const inactive = disabled || scanning;
  const pickable = cardPicker() !== undefined;

  const chooseCard = async () => {
    if (inactive) return;
    setError(null);
    let picked;
    try {
      picked = await pickCardDirectory(cardPicker());
    } catch (failure) {
      setError(failure instanceof Error ? failure.message : String(failure));
      return;
    }
    if (picked.kind === 'unsupported') {
      setError('This browser cannot open a folder for writing; drop it and use Download .db.');
      return;
    }
    if (picked.kind === 'cancelled') return;

    setScanning(true);
    try {
      const result = await scanDirectoryHandle(picked.directory as unknown as CardDirectoryHandle);
      onCard(result.files, result.skipped, picked.directory);
    } catch (failure) {
      setError(failure instanceof Error ? failure.message : String(failure));
    } finally {
      setScanning(false);
    }
  };

  const drop = async (data: DataTransfer) => {
    if (inactive) return;
    setScanning(true);
    setError(null);
    try {
      const result = await scanDataTransfer(data);
      onFiles(result.files, result.skipped);
    } catch (failure) {
      setError(failure instanceof Error ? failure.message : String(failure));
    } finally {
      setScanning(false);
    }
  };

  return (
    <div
      className={`drop${over ? ' drop-over' : ''}${inactive ? ' drop-disabled' : ''}`}
      onDragOver={(event) => {
        event.preventDefault();
        if (!inactive) setOver(true);
      }}
      onDragLeave={() => setOver(false)}
      onDrop={(event) => {
        event.preventDefault();
        setOver(false);
        void drop(event.dataTransfer);
      }}
    >
      {scanning ? (
        <>
          <p className="drop-title">Scanning…</p>
          <p className="muted small">Looking for MP3 and FLAC files</p>
        </>
      ) : (
        <>
          <p className="drop-title">{pickable ? 'Choose the SD card' : 'Drop a folder'}</p>
          <p className="muted small">
            {pickable
              ? 'it opens read-write, so the snapshot is written back to it'
              : 'nothing leaves this machine'}
          </p>
          {pickable && (
            <>
              <div className="drop-actions">
                <button
                  type="button"
                  className="primary"
                  onClick={() => void chooseCard()}
                  disabled={inactive}
                >
                  Choose card
                </button>
              </div>
              <p className="muted small">or drop a folder — dropping is read-only</p>
            </>
          )}
        </>
      )}

      {error && <p className="error small">{error}</p>}
    </div>
  );
}
