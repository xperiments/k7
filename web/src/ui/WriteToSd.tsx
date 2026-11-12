import { useEffect, useState } from 'react';
import { Button } from '@/components/ui/button';
import { reconnectWorkingDirectory } from '../card/virtual';
import { listMedia } from '../storage/media';
import { plural } from './format';
import { useCardScope } from '../card/session';
import { writeCardToSd, type WriteDirectory } from '../card/writeToSd';
import { getVirtualCard, type VirtualCard } from '../storage/vcards';
import { getWrite, putWrite } from '../storage/vwrites';

/** The virtual card the current scope names, or null when a real SD (or nothing) is selected. */
export function useSelectedVirtualCard(): VirtualCard | null {
  const scope = useCardScope();
  const [card, setCard] = useState<VirtualCard | null>(null);

  useEffect(() => {
    if (!scope.startsWith('mark:')) {
      setCard(null);
      return;
    }
    let cancelled = false;
    void getVirtualCard(scope.slice('mark:'.length)).then((found) => {
      if (!cancelled) setCard(found ?? null);
    });
    return () => {
      cancelled = true;
    };
  }, [scope]);

  return card;
}

/**
 * The finish act for the selected virtual card: copy it onto a mounted SD, only the files that
 * changed since the last write, verify them, and say whether it is safe to eject.
 */
export function WriteToSd({ card }: { card: VirtualCard }) {
  const [busy, setBusy] = useState(false);
  const [progress, setProgress] = useState<{ done: number; total: number } | null>(null);
  const [note, setNote] = useState<string | null>(null);

  const write = async () => {
    setBusy(true);
    setNote(null);
    setProgress({ done: 0, total: 0 });
    try {
      const picker = (
        globalThis as {
          showDirectoryPicker?: (options?: { mode?: string; id?: string }) => Promise<unknown>;
        }
      ).showDirectoryPicker;
      if (typeof picker !== 'function') {
        setNote('This browser cannot open a folder for writing.');
        return;
      }

      let sd: WriteDirectory;
      try {
        sd = (await picker({ mode: 'readwrite', id: 'k7-sd' })) as WriteDirectory;
      } catch (error) {
        if (error instanceof DOMException && error.name === 'AbortError') return;
        throw error;
      }

      const dir = await reconnectWorkingDirectory();
      if (dir.kind !== 'ready') {
        setNote('The working directory is not reachable — choose it above, then try again.');
        return;
      }
      const source = (await dir.directory.getDirectoryHandle(card.folder)) as unknown as WriteDirectory;

      const [media, record] = await Promise.all([listMedia(card.scope), getWrite(card.scope)]);
      const prior = new Map((record?.files ?? []).map((file) => [file.path, file.hash]));

      const outcome = await writeCardToSd(source, sd, {
        media,
        prior,
        onProgress: (next) => setProgress({ done: next.done, total: next.total }),
      });

      const files = [...outcome.written, ...outcome.skipped].sort((left, right) =>
        left.path.localeCompare(right.path),
      );
      await putWrite({ scope: card.scope, files, at: new Date().toISOString() });

      if (outcome.failed.length === 0) {
        setNote(
          `Safe to eject. ${plural(outcome.written.length, 'file')} written, ` +
            `${outcome.skipped.length} already there.`,
        );
      } else {
        setNote(`Not written: ${outcome.failed.join(', ')}.`);
      }
    } catch (error) {
      setNote(`Could not write the card: ${error instanceof Error ? error.message : String(error)}`);
    } finally {
      setBusy(false);
      setProgress(null);
    }
  };

  return (
    <div className="flex flex-col gap-1">
      <Button
        variant="solid"
        size="xs"
        className="self-start whitespace-nowrap"
        onClick={() => void write()}
        disabled={busy}
      >
        {busy ? 'Writing…' : 'Write to SD'}
      </Button>
      {note !== null || (progress !== null && progress.total > 0) ? (
        <div className="flex flex-wrap items-center gap-x-2 gap-y-0.5 text-xs text-muted-foreground">
          {progress !== null && progress.total > 0 ? (
            <span>
              writing {progress.done} of {progress.total} files
            </span>
          ) : null}
          {note !== null ? <span>{note}</span> : null}
        </div>
      ) : null}
    </div>
  );
}
