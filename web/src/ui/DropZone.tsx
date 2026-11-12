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
import { cardPicker, type SnapshotDirectory } from './cardWriter';
import { findCardMark, parseCardMark, fingerprintScope, scopeFor } from '../card/mark';
import { cardShapeOf, cardShapeProblem } from '../card/shape';
import { setCardScope } from '../card/session';
import { fingerprintOf, readCardWithPicker } from './intake';
import { useAnalysis } from './state/analysis';

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
  /** The selected virtual card's name, so the intake says which card it adds to. */
  cardName?: string;
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
  cardName,
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
  /* A virtual card is open: dropping a folder of music then composes it into the card's
   * `music/`, instead of measuring it in place or refusing it as "not the card". */
  const { composing, compose } = useAnalysis();

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

  /**
   * Pick a folder of music and compose it into the open virtual card.
   *
   * The click path exists beside the drag: a person who does not know to drag needs one
   * button that says what it does. The picker is read-only — the source folder is the user's
   * music collection and is never written; the copy lands in the card's `music/`.
   */
  const chooseFolder = async () => {
    if (inactive) return;
    setError(null);
    const picker = (
      globalThis as {
        showDirectoryPicker?: (options?: { mode?: string; id?: string }) => Promise<unknown>;
      }
    ).showDirectoryPicker;
    if (typeof picker !== 'function') {
      setError({
        title: 'This browser cannot open a folder',
        text: 'Drag a folder of music onto this area instead.',
      });
      return;
    }
    setScanning(true);
    try {
      const handle = await picker({ mode: 'read', id: 'k7-music' });
      const result = await scanDirectoryHandle(handle as unknown as CardDirectoryHandle);
      const shape = cardShapeOf(result.files.map((found) => found.path));
      // The copy keeps the structure below the folder picked; a folder that already holds a
      // `music/` directory would nest one inside the other, so it is refused rather than
      // produced as `music/music/…`.
      if (shape.kind !== 'no-music' && shape.kind !== 'music-dir') {
        setError({
          title: 'Pick a folder of music',
          text: 'Choose an album or artist folder — not a folder that already contains a music folder.',
        });
        return;
      }
      await compose(result.files, result.skipped);
    } catch (failure) {
      if (!(failure instanceof DOMException && failure.name === 'AbortError')) {
        setError({
          title: 'Could not read the folder',
          text: failure instanceof Error ? failure.message : String(failure),
        });
      }
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
      const paths = result.files.map((found) => found.path);
      const shape = cardShapeOf(paths);
      // A virtual card is open: a dropped folder of music (or a card's `music/` itself) is
      // the compose act — copy it into the card, per-file — not a "wrong folder" error.
      if (composing && (shape.kind === 'no-music' || shape.kind === 'music-dir')) {
        void compose(result.files, result.skipped);
        return;
      }
      const problem = cardShapeProblem(shape);
      if (problem !== null) {
        setError({ title: 'That folder is not the card', text: problem });
        return;
      }
      // The card's own mark is read when it has one: a dropped card is read-only, so the
      // mark cannot be written here, but the identity it already carries is what keeps the
      // covers and the last image attached to it (card-mark.md G2).
      const markPath = findCardMark(paths);
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
        'border border-foreground/40 py-8 transition-colors',
        over && !inactive && 'border-foreground bg-foreground/10',
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
          {scanning
            ? 'Scanning…'
            : composing
              ? `Add music to ${cardName ?? 'this card'}`
              : pickable
                ? 'Choose the SD card'
                : 'Drop a folder'}
        </EmptyTitle>
        <EmptyDescription>
          {scanning
            ? 'Looking for MP3 and FLAC files'
            : disabled
              ? (disabledReason ?? 'The models are still loading.')
              : composing
                ? 'Copies MP3 and FLAC into the card’s music folder, measured as they land. Write it to an SD when it is ready.'
                : pickable
                  ? 'it opens read-write, so the snapshot is written back to it'
                  : 'nothing leaves this machine'}
        </EmptyDescription>
      </EmptyHeader>

      {!scanning && (composing || pickable || onRescan) && (
        <EmptyContent>
          <div className="flex flex-wrap items-center justify-center gap-2">
            {composing ? (
              <Button variant="solid" onClick={() => void chooseFolder()} disabled={inactive}>
                Choose folder
              </Button>
            ) : (
              pickable && (
                <Button variant="solid" onClick={() => void chooseCard()} disabled={inactive}>
                  Choose card
                </Button>
              )
            )}
            {!composing && onRescan && (
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
          <p className="text-xs text-muted-foreground">
            {composing ? 'or drag a folder of music here' : 'or drop a folder — dropping is read-only'}
          </p>
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
