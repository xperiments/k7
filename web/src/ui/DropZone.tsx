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
import { scanDataTransfer, type FoundFile } from './files';
import { cardPicker, type SnapshotDirectory } from './cardWriter';
import { findCardMark, parseCardMark, fingerprintScope, scopeFor } from '../card/mark';
import { cardShapeOf, cardShapeProblem } from '../card/shape';
import { setCardScope } from '../card/session';
import { fingerprintOf, readCardWithPicker } from './intake';

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
  /**
   * Read the card this browser remembers again. Present only when there is one to read: a
   * reload does not scan on its own, so this is the empty state's way back to the Queue.
   */
  onRescan?: () => void;
  /** A run is still writing into the card, so the rescan would race it. */
  rescanDisabled?: boolean;
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
 * actions: "Choose card" for a pick, and "Rescan card" when this browser still remembers the
 * one it worked on. A reload does not read that card by itself; the run starts on the click.
 */

export function DropZone({
  disabled,
  disabledReason,
  onFiles,
  onCard,
  onRescan,
  rescanDisabled = false,
}: DropZoneProps) {
  const [over, setOver] = useState(false);
  const [scanning, setScanning] = useState(false);
  const [picking, setPicking] = useState(false);
  const [error, setError] = useState<Problem | null>(null);
  /* Said, not refused: a dropped card cannot carry a mark written by this page, and the
   * fingerprint scope it gets instead changes the moment a Track is added. */
  const [note, setNote] = useState<string | null>(null);
  const inactive = disabled || scanning || picking;
  const pickable = cardPicker() !== undefined;

  const chooseCard = async () => {
    if (inactive) return;
    setError(null);
    setPicking(true);
    try {
      const picked = await readCardWithPicker();
      switch (picked.kind) {
        case 'read':
          setNote(null);
          onCard(picked.files, picked.skipped, picked.handle);
          return;
        case 'wrong':
          setError({ title: 'That folder is not the card', text: picked.problem });
          return;
        case 'busy':
          setError({
            title: 'A folder picker is already open',
            text: 'Finish or cancel it, then choose the card again.',
          });
          return;
        case 'unsupported':
          setError({
            title: 'This browser cannot open a folder for writing',
            text: 'The card has to be chosen read-write so k7.db can be written to it. A browser without the folder picker can only measure a dropped folder, read-only.',
          });
          return;
        case 'failed':
          setError({ title: 'Could not read the card', text: picked.message });
          return;
        case 'cancelled':
          return;
      }
    } finally {
      setPicking(false);
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

      {!scanning && (pickable || onRescan) && (
        <EmptyContent>
          <div className="flex flex-wrap items-center justify-center gap-2">
            {pickable && (
              <Button onClick={() => void chooseCard()} disabled={inactive}>
                Choose card
              </Button>
            )}
            {onRescan && (
              <Button
                variant="outline"
                onClick={onRescan}
                disabled={inactive || rescanDisabled}
                title="Read the card this browser remembers again; a reload does not scan it, and nothing already measured is measured twice"
              >
                Rescan card
              </Button>
            )}
          </div>
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
