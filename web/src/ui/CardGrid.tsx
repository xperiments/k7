import { useEffect, useState } from 'react';
import {
  CheckIcon,
  Cpu,
  EjectIcon,
  HardDriveIcon,
  PauseIcon,
  PencilIcon,
  PlayIcon,
  PlusIcon,
  RefreshCwIcon,
  Trash2Icon,
} from 'lucide-react';
import { Button } from '@/components/ui/button';
import { Input } from '@/components/ui/input';
import { Progress } from '@/components/ui/progress';
import { cn } from '@/lib/utils';
import { queryAccess } from '../card/handles';
import { findCardMark, fingerprintScope, parseCardMark, scopeFor } from '../card/mark';
import { cardShapeOf } from '../card/shape';
import { UNSCOPED, setCardScope, setDeviceView, useCardScope, useDeviceView } from '../card/session';
import { useDeviceStatus } from '../device/useDeviceStatus';
import {
  CARD_SIZES_GB,
  createVirtualCard,
  deleteVirtualCard,
  openVirtualCard,
  reconnectWorkingDirectory,
  renameVirtualCard,
  sizeBytes,
} from '../card/virtual';
import { workingDirectory } from '../card/vdir';
import type { VirtualCard } from '../storage/vcards';
import { scanDataTransfer } from './files';
import { fingerprintOf } from './intake';
import { plural } from './format';
import { navigate, routeHref } from './router';
import { WriteToSd } from './WriteToSd';
import { useAnalysis } from './state/analysis';
import { forgetVirtualCard, rememberVirtualCard, useVirtualCards } from './state/cards';

interface Note {
  error: boolean;
  text: string;
}

/** The tile a card grid is made of: one outlined cell, filled when it is the selected card. */
function tileClass(selected: boolean): string {
  return cn(
    'flex min-h-24 flex-col gap-1 rounded-panel border p-3 text-left transition-none',
    selected
      ? 'border-primary bg-transparent text-primary'
      // The tile reads as an outlined button, not a faint panel: it is a thing you press.
      : 'border-foreground/45 bg-transparent text-foreground hover:bg-foreground/10',
  );
}

/**
 * The card shelf: one tile per card — the real SD slot, every virtual card, and a "+" to make
 * another. Selecting a tile makes that card the one Prepare works on; the prepare area below
 * the grid is its surface. Removing is destructive (it deletes the music composed into the
 * card, never the originals), so it asks first.
 */
/** The progress a selected card tile shows while its music is being measured. */
function TileProgress({ percent }: { percent: number }) {
  return <Progress value={percent} aria-label="Run progress" className="mt-1" />;
}

/** One card's own status, inside its tile: the image in it and when k7.db last landed. */
function TileStatus(props: {
  writtenAt: string | null;
  saving: boolean;
  metadataPending: number;
  tracks: number | null;
}) {
  return (
    <span className="text-xs text-muted-foreground">
      {props.tracks !== null ? `${plural(props.tracks, 'track')}` : ''}
    </span>
  );
}

/** The one control a measuring tile needs at its top-right: pause, or resume when stopped. */
function PauseButton(props: { running: boolean; onPause: () => void; onResume: () => void }) {
  return (
    <Button
      size="icon-xs"
      variant="ghost"
      className="text-current"
      aria-label={props.running ? 'Pause measuring' : 'Resume measuring'}
      title={props.running ? 'Pause' : 'Resume'}
      onClick={(event) => {
        event.stopPropagation();
        if (props.running) props.onPause();
        else props.onResume();
      }}
    >
      {props.running ? <PauseIcon aria-hidden="true" /> : <PlayIcon aria-hidden="true" />}
    </Button>
  );
}

export function CardGrid() {
  /* The bar above the shell reads the same list; both views stay in step through the store. */
  const { cards } = useVirtualCards();
  const [dirReady, setDirReady] = useState(false);
  const [busy, setBusy] = useState(false);
  const [note, setNote] = useState<Note | null>(null);

  const [creating, setCreating] = useState(false);
  const [newName, setNewName] = useState('');
  const [newGb, setNewGb] = useState<number>(32);

  const [editing, setEditing] = useState<string | null>(null);
  const [editName, setEditName] = useState('');
  const [confirmRemove, setConfirmRemove] = useState<string | null>(null);

  const scope = useCardScope();
  const { status: device } = useDeviceStatus();
  const deviceView = useDeviceView();
  const {
    composing,
    changeCard,
    addFound,
    items,
    total,
    done,
    running,
    pause,
    resume,
    hasCardHandle,
    ejectRealCard,
    selectRealCard,
    metadataPending,
    writtenAt,
    mediaCount,
    saving,
    rescanCard,
    canRescan,
  } = useAnalysis();
  const percent = total > 0 ? (done / total) * 100 : 0;
  /* The run is unfinished while anything is queued or in flight, or the label pass is still
   * writing. Only then does a card show progress and a pause; once it completes the tile goes
   * back to its idle controls (eject on the SD, rename/remove on a virtual card). */
  const pending = items.filter(
    (item) =>
      item.status === 'queued' ||
      item.status === 'hashing' ||
      item.status === 'decoding' ||
      item.status === 'analysing',
  ).length;
  const inProgress = pending > 0 || metadataPending > 0;
  /* A run this session has been measured and is no longer moving: the tile says so with a
   * check, so a finished card does not just look empty. */
  const finished = total > 0 && done === total && !inProgress;

  useEffect(() => {
    void (async () => {
      const dir = await workingDirectory();
      if (dir !== null) setDirReady(await queryAccess(dir, 'readwrite'));
    })();
  }, []);

  const fail = (result: Awaited<ReturnType<typeof reconnectWorkingDirectory>>): boolean => {
    if (result.kind === 'ready') return false;
    if (result.kind === 'denied') setNote({ error: true, text: 'Permission to the working directory was refused.' });
    else if (result.kind === 'unsupported') setNote({ error: true, text: 'This browser cannot open a folder for writing.' });
    else if (result.kind === 'failed') setNote({ error: true, text: result.message });
    return true;
  };

  const connect = async () => {
    setBusy(true);
    setNote(null);
    try {
      const result = await reconnectWorkingDirectory();
      if (!fail(result)) setDirReady(true);
    } finally {
      setBusy(false);
    }
  };

  const create = async () => {
    const label = newName.trim();
    if (label === '') {
      setNote({ error: true, text: 'Give the card a name.' });
      return;
    }
    setBusy(true);
    setNote(null);
    try {
      const dir = await reconnectWorkingDirectory();
      if (fail(dir) || dir.kind !== 'ready') return;
      setDirReady(true);
      const created = await createVirtualCard(dir.directory, label, sizeBytes(newGb));
      rememberVirtualCard(created.card);
      await openVirtualCard(created.card, created.directory);
      setCreating(false);
      setNewName('');
    } catch (error) {
      setNote({ error: true, text: error instanceof Error ? error.message : String(error) });
    } finally {
      setBusy(false);
    }
  };

  const select = async (card: VirtualCard) => {
    setBusy(true);
    setNote(null);
    try {
      const dir = await reconnectWorkingDirectory();
      if (fail(dir) || dir.kind !== 'ready') return;
      setDirReady(true);
      const cardDirectory = await dir.directory.getDirectoryHandle(card.folder);
      await openVirtualCard(card, cardDirectory);
    } catch (error) {
      setNote({ error: true, text: `Could not open the card: ${error instanceof Error ? error.message : String(error)}` });
    } finally {
      setBusy(false);
    }
  };

  const rename = async (card: VirtualCard) => {
    const label = editName.trim();
    if (label === '') {
      setEditing(null);
      return;
    }
    setBusy(true);
    setNote(null);
    try {
      const dir = await reconnectWorkingDirectory();
      if (fail(dir) || dir.kind !== 'ready') return;
      const next = await renameVirtualCard(dir.directory, card, label);
      rememberVirtualCard(next);
      setEditing(null);
    } catch (error) {
      setNote({ error: true, text: error instanceof Error ? error.message : String(error) });
    } finally {
      setBusy(false);
    }
  };

  const remove = async (card: VirtualCard) => {
    setBusy(true);
    setNote(null);
    try {
      const dir = await reconnectWorkingDirectory();
      if (fail(dir) || dir.kind !== 'ready') return;
      await deleteVirtualCard(dir.directory, card);
      forgetVirtualCard(card.id);
      setConfirmRemove(null);
    } catch (error) {
      setNote({ error: true, text: error instanceof Error ? error.message : String(error) });
    } finally {
      setBusy(false);
    }
  };

  /** A folder dropped on the SD tile: measured in place, read-only, exactly as a drop behaves. */
  const dropSd = async (data: DataTransfer) => {
    setBusy(true);
    setNote(null);
    try {
      const result = await scanDataTransfer(data);
      const paths = result.files.map((found) => found.path);
      if (cardShapeOf(paths).kind !== 'card-root') {
        setNote({ error: true, text: 'That is not an SD card — drop the folder that holds a music folder.' });
        return;
      }
      const markPath = findCardMark(paths);
      const markFile = markPath === null ? undefined : result.files.find((found) => found.path === markPath);
      const parsed = markFile === undefined ? null : parseCardMark(await markFile.file.text());
      const usable = typeof parsed === 'object' && parsed !== null ? parsed : null;
      setCardScope(usable !== null ? scopeFor(usable) : fingerprintScope(await fingerprintOf(result.files)));
      addFound(result.files, result.skipped, true);
    } catch (error) {
      setNote({ error: true, text: `Could not read the folder: ${error instanceof Error ? error.message : String(error)}` });
    } finally {
      setBusy(false);
    }
  };

  const realSelected = !composing && !deviceView && scope !== UNSCOPED;
  // The SD is "loaded" when it was picked this session, or when it is the card being worked
  // on (a dropped card is read-only but still the current card). Either way, eject must be
  // offered, because that is the only explicit way to put it down.
  const sdLoaded = hasCardHandle || realSelected;

  const browseSd = () => {
    if (!sdLoaded) return;
    selectRealCard();
    navigate(routeHref({ name: 'media' }));
  };

  return (
    <div className="flex flex-col gap-2">
      <div className="flex items-center justify-between gap-2">
        <h2 className="text-sm font-light tracking-tight">Cards</h2>
        <div className="flex items-center gap-2">
          {!dirReady && (
            <Button size="xs" variant="outline" onClick={() => void connect()} disabled={busy}>
              Choose working directory
            </Button>
          )}
          <Button size="xs" variant="solid" onClick={() => setCreating((open) => !open)} disabled={busy}>
            <PlusIcon aria-hidden="true" /> New virtual card
          </Button>
        </div>
      </div>

      {creating && (
        <div className="flex flex-wrap items-center gap-2 rounded-panel border border-border p-3">
          <Input
            value={newName}
            onChange={(event) => setNewName(event.target.value)}
            placeholder="Card name"
            aria-label="Card name"
            className="w-40"
            autoFocus
          />
          <div className="flex flex-wrap items-center gap-1">
            {CARD_SIZES_GB.map((gb) => (
              <Button
                key={gb}
                size="xs"
                variant={newGb === gb ? 'solid' : 'outline'}
                aria-pressed={newGb === gb}
                onClick={() => setNewGb(gb)}
              >
                {gb} GB
              </Button>
            ))}
          </div>
          <Button size="sm" variant="solid" onClick={() => void create()} disabled={busy}>
            Create
          </Button>
          <Button size="sm" variant="ghost" onClick={() => setCreating(false)} disabled={busy}>
            Cancel
          </Button>
        </div>
      )}

      <div className="grid grid-cols-2 gap-2 sm:grid-cols-3 lg:grid-cols-4">
        {/* The card in the player: the ESP32-S3's own SD. The browser never has its
            directory, so this tile only views it — its albums are the Albums page's. It
            appears only while the device reports a card, and only a marked one can be named. */}
        {device?.card === true && device.mark !== null ? (
          <div
            className={cn(tileClass(deviceView), 'cursor-pointer')}
            onClick={() => setDeviceView(true)}
            role="button"
            tabIndex={0}
            onKeyDown={(event) => {
              if (event.key === 'Enter' || event.key === ' ') {
                event.preventDefault();
                setDeviceView(true);
              }
            }}
            aria-label="Show the albums on the card in the player"
          >
            <div className="flex items-center gap-1">
              <Cpu aria-hidden="true" />
              <span className="min-w-0 flex-1 truncate text-sm">Device</span>
              {deviceView ? <CheckIcon aria-hidden="true" className="size-4 shrink-0" /> : null}
            </div>
            <span className={cn('text-xs tabular-nums', deviceView ? '' : 'text-muted-foreground')}>
              {deviceView ? 'card in the player · ' : ''}
              {plural(device.tracks, 'track')}
            </span>
          </div>
        ) : null}

        {/* The real SD slot. Drop a card folder on it or click to pick one read-write; the
            read-write pick is what lets Prepare write k7.db back. */}
        <div
          className={cn(tileClass(realSelected), 'cursor-pointer')}
          onClick={() => {
            if (busy) return;
            // A loaded SD is selected again, not re-picked: the picker is for a slot that is
            // empty, and ejecting is what empties it.
            if (sdLoaded) selectRealCard();
            else void changeCard();
          }}
          onDragOver={(event) => event.preventDefault()}
          onDrop={(event) => {
            event.preventDefault();
            void dropSd(event.dataTransfer);
          }}
          role="button"
          tabIndex={0}
          onKeyDown={(event) => {
            if ((event.key === 'Enter' || event.key === ' ') && !busy) {
              event.preventDefault();
              if (sdLoaded) selectRealCard();
              else void changeCard();
            }
          }}
          aria-label="Choose the mounted SD card read-write, or drop its folder here"
        >
          <div className="flex items-center gap-1">
            <HardDriveIcon aria-hidden="true" />
            <span className="min-w-0 flex-1 truncate text-sm">SD card</span>
            {realSelected && finished ? (
              <CheckIcon aria-hidden="true" className="size-4 shrink-0" />
            ) : null}
            {realSelected && inProgress ? (
              <PauseButton running={running} onPause={pause} onResume={resume} />
            ) : sdLoaded ? (
              <>
                <Button
                  size="icon-xs"
                  variant="ghost"
                  className="text-current"
                  aria-label="Re-process the card"
                  title="Read the card again — tracks already measured are recognised"
                  onClick={(event) => {
                    event.stopPropagation();
                    void rescanCard();
                  }}
                  disabled={busy || !canRescan}
                >
                  <RefreshCwIcon aria-hidden="true" />
                </Button>
                <Button
                  size="icon-xs"
                  variant="ghost"
                  className="text-current"
                  aria-label="Eject SD card"
                  title="Eject SD card"
                  onClick={(event) => {
                    event.stopPropagation();
                    ejectRealCard();
                  }}
                >
                  <EjectIcon aria-hidden="true" />
                </Button>
              </>
            ) : null}
          </div>
          {!realSelected ? (
            <span className={cn('text-xs', sdLoaded ? '' : 'text-muted-foreground')}>
              {sdLoaded ? 'loaded' : 'choose or drop'}
            </span>
          ) : null}
          {realSelected ? (
            <TileStatus
              writtenAt={writtenAt}
              saving={saving}
              metadataPending={metadataPending}
              tracks={mediaCount > 0 ? mediaCount : null}
            />
          ) : null}
          {realSelected && inProgress ? <TileProgress percent={percent} /> : null}
          {sdLoaded ? (
            <Button
              size="xs"
              variant="outline"
              className="mt-1 self-start text-current"
              onClick={(event) => {
                event.stopPropagation();
                browseSd();
              }}
            >
              Browse albums
            </Button>
          ) : null}
        </div>

        {cards.map((card) => {
          const selected = !deviceView && card.scope === scope;
          const removing = confirmRemove === card.id;
          return (
            <div key={card.id} className="flex flex-col gap-1">
              <div
                className={cn(tileClass(selected), 'cursor-pointer')}
                onClick={() => void select(card)}
                role="button"
                tabIndex={0}
                onKeyDown={(event) => {
                  if (event.key === 'Enter' || event.key === ' ') {
                    event.preventDefault();
                    void select(card);
                  }
                }}
                aria-label={`Prepare ${card.label || card.folder}`}
              >
                <div className="flex items-center gap-1">
                  <HardDriveIcon aria-hidden="true" />
                  <span className="min-w-0 flex-1 truncate text-sm">{card.label || card.folder} :: {card.sizeBytes / 1_000_000_000} GB</span>
                  
                  {selected && finished ? (
                    <CheckIcon aria-hidden="true" className="size-4 shrink-0" />
                  ) : null}
                  {/* `text-current` so the icons follow the tile's own ink: accent on the
                      selected tile, neutral on the others. */}
                  <div className="flex shrink-0 gap-1" onClick={(event) => event.stopPropagation()}>
                    {selected && inProgress ? (
                      <PauseButton running={running} onPause={pause} onResume={resume} />
                    ) : (
                      <>
                        <Button
                          size="icon-xs"
                          variant="ghost"
                          className="text-current"
                          aria-label="Rename card"
                          title="Rename card"
                          onClick={() => {
                            setEditing(card.id);
                            setEditName(card.label);
                          }}
                          disabled={busy}
                        >
                          <PencilIcon aria-hidden="true" />
                        </Button>
                        <Button
                          size="icon-xs"
                          variant="ghost"
                          className="text-current"
                          aria-label="Remove card"
                          title="Remove card"
                          onClick={() => setConfirmRemove(card.id)}
                          disabled={busy}
                        >
                          <Trash2Icon aria-hidden="true" />
                        </Button>
                      </>
                    )}
                  </div>
                </div>

                {selected && inProgress ? <TileProgress percent={percent} /> : null}
                <div
                  className="mt-1 flex flex-nowrap items-start gap-1"
                  onClick={(event) => event.stopPropagation()}
                >
                  <WriteToSd card={card} />
                </div>
              </div>

              {editing === card.id && (
                <div className="flex items-center gap-1">
                  <Input
                    value={editName}
                    onChange={(event) => setEditName(event.target.value)}
                    aria-label="New card name"
                    className="w-full"
                    autoFocus
                  />
                  <Button size="xs" variant="solid" onClick={() => void rename(card)} disabled={busy}>
                    Save
                  </Button>
                  <Button size="xs" variant="ghost" onClick={() => setEditing(null)} disabled={busy}>
                    Cancel
                  </Button>
                </div>
              )}

              {removing && (
                <div className="flex flex-wrap items-center gap-1">
                  <span className="text-xs">Remove this card and its music?</span>
                  <Button size="xs" variant="solid" onClick={() => void remove(card)} disabled={busy}>
                    Remove
                  </Button>
                  <Button size="xs" variant="ghost" onClick={() => setConfirmRemove(null)} disabled={busy}>
                    Keep
                  </Button>
                </div>
              )}
            </div>
          );
        })}

      </div>

      {/* K22: a state is words, never a second hue, so error and hint share the neutral. */}
      {note ? <p className={cn('text-xs', note.error ? 'text-muted-foreground' : 'text-muted-foreground')}>{note.text}</p> : null}
    </div>
  );
}
