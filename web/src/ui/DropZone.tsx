import { useState } from 'react';
import { FolderOpenIcon } from 'lucide-react';
import { Alert, AlertDescription, AlertTitle } from '@/components/reui/alert';
import { Button } from '@/components/ui/button';
import {
  Empty,
  EmptyContent,
  EmptyDescription,
  EmptyHeader,
  EmptyMedia,
  EmptyTitle,
} from '@/components/ui/empty';
import { Spinner } from '@/components/ui/spinner';
import { cn } from '@/lib/utils';
import {
  scanDataTransfer,
  scanDirectoryHandle,
  type CardDirectoryHandle,
  type FoundFile,
} from './files';
import { cardPicker, pickCardDirectory, type SnapshotDirectory } from './cardWriter';

interface DropZoneProps {
  disabled: boolean;
  /** Why the intake is closed, when the reason is known (missing models, not loading). */
  disabledReason?: string;
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
 *
 * The intake is also the empty state of the queue below it, which is why it carries the
 * one action ("Choose card") and the queue adds no second empty of its own.
 */
export function DropZone({ disabled, disabledReason, onFiles, onCard }: DropZoneProps) {
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
    <Empty
      aria-busy={scanning}
      className={cn(
        'border border-dashed py-8 transition-colors',
        over && !inactive && 'border-primary bg-primary/5',
        inactive && 'opacity-60',
      )}
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
      <EmptyHeader>
        <EmptyMedia variant="icon">
          {scanning ? <Spinner aria-hidden="true" /> : <FolderOpenIcon aria-hidden="true" />}
        </EmptyMedia>
        <EmptyTitle>
          {scanning ? 'Scanning…' : pickable ? 'Choose the SD card' : 'Drop a folder'}
        </EmptyTitle>
        <EmptyDescription>
          {scanning
            ? 'Looking for MP3 and FLAC files'
            : disabled
              ? (disabledReason ?? 'The models are still loading.')
              : pickable
                ? 'it opens read-write, so the snapshot is written back to it'
                : 'nothing leaves this machine'}
        </EmptyDescription>
      </EmptyHeader>

      {pickable && !scanning && (
        <EmptyContent>
          <Button onClick={() => void chooseCard()} disabled={inactive}>
            Choose card
          </Button>
          <p className="text-xs text-muted-foreground">or drop a folder — dropping is read-only</p>
        </EmptyContent>
      )}

      {error && (
        <Alert variant="destructive" className="w-full max-w-sm text-left">
          <AlertTitle>Could not open the folder</AlertTitle>
          <AlertDescription>{error}</AlertDescription>
        </Alert>
      )}
    </Empty>
  );
}
