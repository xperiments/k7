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
import {
  cardPicker,
  identifyCard,
  pickCardDirectory,
  UNMARKED_CARD,
  type CardDirectory,
  type SnapshotDirectory,
} from './cardWriter';
import { cardFingerprint } from '../card/fingerprint';
import { findCardMark, parseCardMark, fingerprintScope, scopeFor } from '../card/mark';
import { cardShapeOf, cardShapeProblem } from '../card/shape';
import { setCardScope } from '../card/session';

/** The two things a refusal can be: the folder is wrong, or the browser could not open it. */
interface Problem {
  title: string;
  text: string;
}

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
function fingerprintOf(files: readonly FoundFile[]): Promise<string> {
  return cardFingerprint(
    files.map((found) => ({
      path: found.path,
      size: found.file.size,
      mtime: found.file.lastModified,
    })),
  );
}

export function DropZone({ disabled, disabledReason, onFiles, onCard }: DropZoneProps) {
  const [over, setOver] = useState(false);
  const [scanning, setScanning] = useState(false);
  const [error, setError] = useState<Problem | null>(null);
  /* Said, not refused: a dropped card cannot carry a mark written by this page, and the
   * fingerprint scope it gets instead changes the moment a Track is added. */
  const [note, setNote] = useState<string | null>(null);
  const inactive = disabled || scanning;
  const pickable = cardPicker() !== undefined;

  const chooseCard = async () => {
    if (inactive) return;
    setError(null);
    let picked;
    try {
      picked = await pickCardDirectory(cardPicker());
    } catch (failure) {
      setError({ title: 'Could not open the folder', text: failure instanceof Error ? failure.message : String(failure) });
      return;
    }
    if (picked.kind === 'unsupported') {
      setError({
        title: 'This browser cannot open a folder for writing',
        text: 'The card has to be chosen read-write so k7.db can be written to it. A browser without the folder picker can only measure a dropped folder, read-only.',
      });
      return;
    }
    if (picked.kind === 'cancelled') return;

    setScanning(true);
    try {
      const result = await scanDirectoryHandle(picked.directory as unknown as CardDirectoryHandle);
      // The card root is the only choice that can be written back: one level down is the
      // music directory, one up is the card's parent, and neither can hold k7.db (K1).
      const problem = cardShapeProblem(cardShapeOf(result.files.map((found) => found.path)));
      if (problem !== null) {
        setError({ title: 'That folder is not the card', text: problem });
        return;
      }
      // Identify the card before the run: the scope has to exist before the first cover is
      // written, and a mark this App did not write is never overwritten (card-mark.md §4).
      const identified = await identifyCard(picked.directory as unknown as CardDirectory);
      setCardScope(
        identified === UNMARKED_CARD
          ? fingerprintScope(await fingerprintOf(result.files))
          : identified,
      );
      setNote(null);
      onCard(result.files, result.skipped, picked.directory);
    } catch (failure) {
      setError({
        title: 'Could not read the card',
        text: failure instanceof Error ? failure.message : String(failure),
      });
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
      const problem = cardShapeProblem(cardShapeOf(result.files.map((found) => found.path)));
      if (problem !== null) {
        setError({ title: 'That folder is not the card', text: problem });
        return;
      }
      // The card's own mark is read when it has one: a dropped card is read-only, so the
      // mark cannot be written here, but the identity it already carries is what keeps the
      // covers and the last image attached to it (card-mark.md G2).
      const markPath = findCardMark(result.files.map((found) => found.path));
      const markFile = markPath === null ? undefined : result.files.find((found) => found.path === markPath);
      const mark = markFile === undefined ? null : parseCardMark(await markFile.file.text());
      const usable = typeof mark === 'object' && mark !== null ? mark : null;
      setCardScope(
        usable !== null ? scopeFor(usable) : fingerprintScope(await fingerprintOf(result.files)),
      );
      setNote(
        usable === null
          ? 'That card has no mark this page can read, so it is identified by its contents for now. Open it once with “Choose card” to give it one.'
          : null,
      );
      onFiles(result.files, result.skipped);
    } catch (failure) {
      setError({
        title: 'Could not read the folder',
        text: failure instanceof Error ? failure.message : String(failure),
      });
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

      {note && (
        <Alert className="w-full max-w-sm text-left">
          <AlertTitle>Unmarked card</AlertTitle>
          <AlertDescription>{note}</AlertDescription>
        </Alert>
      )}

      {error && (
        <Alert variant="destructive" className="w-full max-w-sm text-left">
          <AlertTitle>{error.title}</AlertTitle>
          <AlertDescription>{error.text}</AlertDescription>
        </Alert>
      )}
    </Empty>
  );
}
