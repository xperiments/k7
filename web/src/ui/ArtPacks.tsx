import { useRef, useState } from 'react';
import { RefreshCw, Upload } from 'lucide-react';

import { Badge } from '@/components/reui/badge';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Skeleton } from '@/components/ui/skeleton';
import { Spinner } from '@/components/ui/spinner';
import { FACTORY_JCARDS, Jcard, useJcardPool } from '../jcard';
import {
  refreshArtPacks,
  resetArtPacks,
  uploadArtPack,
  useArtPackState,
} from '../jcard/active';
import { PACK_TOTAL_MAX, readArtPack, type ArtPack } from '../jcard/pack';
import { plural } from './format';

/**
 * Settings → Art packs (`art-packs.md` §4, front-door §4.5).
 *
 * The one place a `.k7artpack` is installed. A pack is checked in the browser first — the
 * header, the CRC, the sheet and the J-cards — and its designs are shown as the faces they
 * will become before anything is uploaded. The upload is the device's to refuse: the page
 * carries the player's own sentence back, and the pool changes only after the device says
 * the pack is on its flash.
 *
 * The page works without a player on purpose: the designs can be previewed and the factory
 * count is the App's own. What it will not do without a player is offer an upload — with the
 * card in the reader the K7 has nothing to install to — and the covers keep drawing from
 * the factory J-cards and whatever this browser cached.
 */

interface Notice {
  kind: 'ok' | 'error';
  text: string;
}

function NoticeLine({ notice }: { notice: Notice }) {
  return (
    <div role={notice.kind === 'ok' ? 'status' : 'alert'} className="flex items-start gap-2 text-sm">
      <Badge variant={notice.kind === 'ok' ? 'success-light' : 'destructive-light'}>
        {notice.kind === 'ok' ? 'ok' : 'failed'}
      </Badge>
      <span className="min-w-0 break-words">{notice.text}</span>
    </div>
  );
}

function reasonOf(error: unknown): string {
  return error instanceof Error ? error.message : String(error);
}

export function ArtPacksCard() {
  const state = useArtPackState();
  const pool = useJcardPool();
  const fileInput = useRef<HTMLInputElement>(null);

  const [imported, setImported] = useState<ArtPack | null>(null);
  const [importError, setImportError] = useState<string | null>(null);
  const [notice, setNotice] = useState<Notice | null>(null);
  const [pending, setPending] = useState<'upload' | 'reset' | null>(null);
  const [confirmReset, setConfirmReset] = useState(false);

  const factoryCount = state.status?.factoryCount ?? FACTORY_JCARDS.length;
  const active =
    state.status?.installed && state.status.valid
      ? {
          name: state.status.name || 'An unnamed pack',
          author: state.status.author,
          count: state.status.designCount,
          cached: !state.reachable,
        }
      : !state.status && state.cached
        ? {
            name: state.cached.name || 'An unnamed pack',
            author: state.cached.author,
            count: state.cached.designCount,
            cached: true,
          }
        : null;

  const refused = state.status?.installed === true && state.status.valid === false;
  const canWrite = state.reachable && !state.busy && !state.loading;

  const choose = async (file: File | undefined) => {
    if (!file) return;
    setImported(null);
    setImportError(null);
    setNotice(null);
    setConfirmReset(false);
    if (file.size > PACK_TOTAL_MAX) {
      setImportError(
        `This file is ${Math.round(file.size / 1024)} KB; a pack is at most ${PACK_TOTAL_MAX / (1024 * 1024)} MB.`,
      );
      return;
    }
    try {
      const bytes = new Uint8Array(await file.arrayBuffer());
      setImported(readArtPack(bytes));
    } catch (error) {
      setImportError(reasonOf(error));
    }
  };

  const install = async () => {
    if (!imported || !canWrite) return;
    setNotice(null);
    setPending('upload');
    try {
      const status = await uploadArtPack(imported);
      setNotice({
        kind: 'ok',
        text: `${imported.name || 'The pack'} is on the player: ${plural(status.designCount, 'design')}, and ${plural(status.factoryCount + status.designCount, 'design')} in the pool with the factory.`,
      });
      setImported(null);
    } catch (error) {
      setNotice({ kind: 'error', text: reasonOf(error) });
    } finally {
      setPending(null);
    }
  };

  const reset = async () => {
    setNotice(null);
    setPending('reset');
    try {
      const status = await resetArtPacks();
      setConfirmReset(false);
      setNotice({
        kind: 'ok',
        text: `Back to the factory's ${plural(status.factoryCount, 'design')}. The player's embedded sheet was not touched.`,
      });
    } catch (error) {
      setNotice({ kind: 'error', text: reasonOf(error) });
    } finally {
      setPending(null);
    }
  };

  return (
    <Card>
      <CardHeader>
        <CardTitle>Art packs</CardTitle>
        <p className="text-sm text-muted-foreground">
          The K7 prints a K7Art&apos;s label at the two-finger pose; this App draws its J-card on
          an album with no cover picture. A user pack joins the factory designs — it never
          replaces them.
        </p>
      </CardHeader>
      <CardContent className="flex flex-col gap-4">
        {state.loading && !state.status && !state.cached ? (
          <div className="flex flex-col gap-2">
            <Skeleton className="h-5 w-2/3" />
            <p className="text-xs text-muted-foreground" role="status">
              Asking the player what pack is active…
            </p>
          </div>
        ) : (
          <div className="flex flex-col gap-2">
            <div className="flex flex-wrap items-center gap-2">
              <Badge variant="outline">Factory · {plural(factoryCount, 'design')}</Badge>
              {active ? (
                <Badge variant="success-light">
                  {active.cached ? 'Last installed' : 'Active'} · {active.name}
                </Badge>
              ) : refused ? (
                <Badge variant="warning-light">Refused by the player</Badge>
              ) : (
                <Badge variant="secondary">No user pack</Badge>
              )}
              <Badge variant="secondary">Pool · {plural(pool.length, 'design')}</Badge>
              {state.loading ? <Spinner className="size-3.5" aria-label="Loading the art status" /> : null}
            </div>

            <p className="text-xs text-muted-foreground">
              {active
                ? `${active.name}${active.author ? ` by ${active.author}` : ''} · ${plural(active.count, 'design')} ${active.cached ? 'last seen on the player' : 'on the player'}.`
                : 'The factory designs are the whole pool. Import a pack to add designs.'}
            </p>

            {refused ? (
              <p className="text-xs text-muted-foreground">
                The player refused the pack in its flash — {state.status?.why || 'no reason was given'}.
                The factory sheet alone is drawing. Reset to factory clears it.
              </p>
            ) : null}

            {!state.reachable && !state.error ? (
              <p className="text-xs text-muted-foreground" role="status">
                The player is not answering. With the card in the reader this page offers no upload;
                the covers draw from the factory J-cards and any pack this browser cached.
              </p>
            ) : null}

            {state.error ? (
              <div role="alert" className="flex flex-wrap items-start gap-2 text-sm">
                <Badge variant="destructive-light">failed</Badge>
                <span className="min-w-0 flex-1 break-words">{state.error}</span>
                <Button
                  type="button"
                  variant="outline"
                  size="sm"
                  onClick={() => void refreshArtPacks()}
                  disabled={state.busy}
                >
                  <RefreshCw aria-hidden="true" />
                  Retry
                </Button>
              </div>
            ) : !state.loading ? (
              <div>
                <Button
                  type="button"
                  variant="ghost"
                  size="sm"
                  onClick={() => void refreshArtPacks()}
                  disabled={state.busy}
                >
                  <RefreshCw aria-hidden="true" />
                  Ask the player again
                </Button>
              </div>
            ) : null}
          </div>
        )}

        <div className="flex flex-col gap-3 border-t pt-3">
          <div className="flex flex-wrap items-center gap-2">
            <Button
              type="button"
              variant="outline"
              size="sm"
              onClick={() => fileInput.current?.click()}
              disabled={state.busy}
            >
              Choose a .k7artpack
            </Button>
            <span className="text-xs text-muted-foreground">
              A pack from the K7Art editor: at most 1 MB and 32 designs.
            </span>
            <input
              ref={fileInput}
              type="file"
              accept=".k7artpack,application/x-k7artpack,application/octet-stream"
              className="sr-only"
              aria-label="Choose a K7Art pack"
              onChange={(event) => {
                const file = event.target.files?.[0];
                event.target.value = '';
                void choose(file);
              }}
            />
          </div>

          {importError ? (
            <div role="alert" className="flex items-start gap-2 text-sm">
              <Badge variant="destructive-light">refused</Badge>
              <span className="min-w-0 break-words">{importError}</span>
            </div>
          ) : null}

          {imported ? (
            <div className="flex flex-col gap-3">
              <div className="flex flex-wrap items-baseline gap-x-2 gap-y-1">
                <span className="text-sm font-medium">{imported.name || imported.packId}</span>
                <span className="text-xs text-muted-foreground">
                  {imported.author ? `by ${imported.author}` : 'no author'} · {imported.packId}
                </span>
                <Badge variant="outline">{plural(imported.designCount, 'design')}</Badge>
                <Badge variant="secondary">
                  {Math.max(1, Math.round(imported.size / 1024))} KB
                </Badge>
              </div>

              <div className="grid grid-cols-2 gap-3 sm:grid-cols-3">
                {imported.designs.map((design) => (
                  <figure key={design.id} className="flex min-w-0 flex-col gap-1">
                    <div className="k7-cover-tile relative aspect-square overflow-clip border border-border">
                      <Jcard
                        design={design}
                        fields={{
                          title: design.name,
                          artist: imported.author || imported.name,
                          meta: imported.name || imported.packId,
                        }}
                        fit="cover"
                      />
                    </div>
                    <figcaption className="truncate text-xs text-muted-foreground">
                      {design.name}
                    </figcaption>
                  </figure>
                ))}
              </div>

              <div className="flex flex-wrap items-center gap-2">
                <Button
                  type="button"
                  variant="solid"
                  size="sm"
                  onClick={() => void install()}
                  disabled={!canWrite}
                >
                  <Upload aria-hidden="true" />
                  Install on the K7
                </Button>
                <Button
                  type="button"
                  variant="ghost"
                  size="sm"
                  onClick={() => setImported(null)}
                  disabled={state.busy}
                >
                  Discard
                </Button>
                {!state.reachable ? (
                  <span className="text-xs text-muted-foreground">
                    A pack needs the player&apos;s link.
                  </span>
                ) : null}
              </div>

              {pending === 'upload' ? (
                <div role="status" className="flex items-start gap-2 text-xs text-muted-foreground">
                  <Spinner className="mt-0.5 size-3.5" aria-hidden="true" />
                  <span>
                    Sending the pack to the player. It checks the whole file before it writes.
                  </span>
                </div>
              ) : null}
            </div>
          ) : null}

          {notice ? <NoticeLine notice={notice} /> : null}
        </div>

        <div className="flex flex-wrap items-center gap-2 border-t pt-3">
          <div className="flex min-w-0 flex-1 flex-col gap-0.5">
            <span className="text-sm">Reset to factory</span>
            <span className="text-xs text-muted-foreground">
              Erases the user pack from the player&apos;s flash. The factory sheet is not
              touched, and the file you published is your copy.
            </span>
          </div>
          {pending === 'reset' ? (
            <div role="status" className="flex items-start gap-2 text-xs text-muted-foreground">
              <Spinner className="mt-0.5 size-3.5" aria-hidden="true" />
              <span>Erasing the user pack and returning the player to its factory sheet.</span>
            </div>
          ) : null}
          {canWrite && (state.status?.installed || state.cached) ? (
            confirmReset ? (
              <>
                <Button type="button" variant="solid" size="sm" onClick={() => void reset()}>
                  Erase the user pack
                </Button>
                <Button
                  type="button"
                  variant="ghost"
                  size="sm"
                  onClick={() => setConfirmReset(false)}
                >
                  Keep
                </Button>
              </>
            ) : (
              <Button
                type="button"
                variant="outline"
                size="sm"
                onClick={() => setConfirmReset(true)}
              >
                Reset to factory
              </Button>
            )
          ) : null}
        </div>

        <p className="text-xs text-muted-foreground">
          Want your own designs? Make a pack in the{' '}
          <a
            href="/k7art/"
            className="underline underline-offset-4 hover:text-foreground"
            target="_blank"
            rel="noopener noreferrer"
          >
            K7Art editor
          </a>
          , then import it here.
        </p>
      </CardContent>
    </Card>
  );
}
