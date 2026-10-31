import { useEffect, useRef, useState } from 'react';
import {
  filesFromInput,
  scanDataTransfer,
  scanDirectoryHandle,
  type CardDirectoryHandle,
  type FoundFile,
} from './files';
import { cardPicker, pickCardDirectory, type SnapshotDirectory } from './cardWriter';

interface DropZoneProps {
  disabled: boolean;
  onFiles: (found: FoundFile[], skipped: number) => void;
  /** A folder the browser let us open read-write: the card, ready to be written back. */
  onCard: (found: FoundFile[], skipped: number, handle: SnapshotDirectory) => void;
}

export function DropZone({ disabled, onFiles, onCard }: DropZoneProps) {
  const filesInput = useRef<HTMLInputElement>(null);
  const folderInput = useRef<HTMLInputElement>(null);
  const [over, setOver] = useState(false);
  const [scanning, setScanning] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const inactive = disabled || scanning;

  useEffect(() => {
    // Non-standard attributes, so TypeScript's input type is left alone.
    folderInput.current?.setAttribute('webkitdirectory', '');
    folderInput.current?.setAttribute('directory', '');
  }, []);

  const choose = () => {
    if (!inactive) filesInput.current?.click();
  };

  /* The card is chosen, not dropped. A dropped folder is read-only for good, so the
   * only way to write the snapshot back where it came from is to ask for the root with
   * write permission - and to keep the handle, which is the card for the rest of the
   * session. */
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

  const scan = async (data: DataTransfer) => {
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
      role="button"
      tabIndex={0}
      aria-disabled={inactive}
      onClick={choose}
      onKeyDown={(event) => {
        if (event.key === 'Enter' || event.key === ' ') {
          event.preventDefault();
          choose();
        }
      }}
      onDragOver={(event) => {
        event.preventDefault();
        if (!inactive) setOver(true);
      }}
      onDragLeave={() => setOver(false)}
      onDrop={(event) => {
        event.preventDefault();
        setOver(false);
        void scan(event.dataTransfer);
      }}
    >
      <input
        ref={filesInput}
        type="file"
        multiple
        accept=".mp3,.flac,audio/mpeg,audio/flac"
        hidden
        onChange={(event) => {
          if (event.target.files) onFiles(filesFromInput(event.target.files), 0);
          event.target.value = '';
        }}
      />
      <input
        ref={folderInput}
        type="file"
        multiple
        hidden
        onChange={(event) => {
          if (event.target.files) onFiles(filesFromInput(event.target.files), 0);
          event.target.value = '';
        }}
      />

      {scanning ? (
        <>
          <p className="drop-title">Scanning folder…</p>
          <p className="muted small">Looking for MP3 and FLAC files</p>
        </>
      ) : (
        <>
          <p className="drop-title">
            {cardPicker() ? 'Choose the SD card' : 'Drop MP3, FLAC, or a folder'}
          </p>
          <p className="muted small">
            {cardPicker()
              ? 'it opens read-write, so the snapshot can be written back to it'
              : 'nothing leaves this machine'}
          </p>
          <div className="drop-actions">
            {cardPicker() && (
              <button
                type="button"
                className="primary"
                onClick={(event) => {
                  event.stopPropagation();
                  void chooseCard();
                }}
                disabled={inactive}
              >
                Choose card
              </button>
            )}
            <button
              type="button"
              className="ghost"
              onClick={(event) => {
                event.stopPropagation();
                filesInput.current?.click();
              }}
              disabled={inactive}
            >
              Choose files
            </button>
            <button
              type="button"
              className="ghost"
              onClick={(event) => {
                event.stopPropagation();
                folderInput.current?.click();
              }}
              disabled={inactive}
            >
              Choose folder
            </button>
          </div>
        </>
      )}

      {error && <p className="error small">{error}</p>}
    </div>
  );
}
