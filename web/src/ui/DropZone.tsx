import { useEffect, useRef, useState } from 'react';
import { filesFromInput, scanDataTransfer, type FoundFile } from './files';

interface DropZoneProps {
  disabled: boolean;
  onFiles: (found: FoundFile[], skipped: number) => void;
}

export function DropZone({ disabled, onFiles }: DropZoneProps) {
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
          <p className="drop-title">Drop MP3, FLAC, or a folder</p>
          <p className="muted small">nothing leaves this machine</p>
          <div className="drop-actions">
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
