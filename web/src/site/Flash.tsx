import { useEffect, useRef, useState } from 'react';
import { Cable, Check, Eraser, MonitorX, RefreshCw, Usb } from 'lucide-react';

import { Alert, AlertDescription, AlertTitle } from '@/components/reui/alert';
import { Button } from '@/components/ui/button';
import {
  Card,
  CardContent,
  CardDescription,
  CardHeader,
  CardTitle,
} from '@/components/ui/card';
import { Progress } from '@/components/ui/progress';
import { Spinner } from '@/components/ui/spinner';
import { canFlash } from './flash/capability';
import { openSerialDevice } from './flash/device';
import type { FailureCause } from './flash/errors';
import {
  BOARDS,
  hex,
  manualCommand,
  partUrl,
  type Board,
} from './flash/manifest';
import { FlashMachine, initialState, type MachineState } from './flash/machine';
import { downloadPart } from './flash/parts';

/**
 * The flash, as the page offers it (k7-site.md §3).
 *
 * The rules live in `flash/machine.ts`; this file is the surface: which board, what you need,
 * who is on the other end, the confirmation, the progress, and — after a failure — the
 * sentence that says what to do next. Two boards share the flow and nothing else: the chip,
 * the flash size, the erase's cost and the steps afterwards are each board's own. Without Web
 * Serial it offers the manual path instead of a dead control.
 */

const FAILURE: Record<FailureCause, { title: string; body: string }> = {
  cancelled: {
    title: 'No port chosen',
    body: 'The browser did not get a port. Press the control and choose the board in the dialog.',
  },
  'no-port': {
    title: 'This browser has no Web Serial',
    body: 'Flashing needs Chrome or Edge on a computer.',
  },
  'port-busy': {
    title: 'The port is busy',
    body: 'Another program is holding it — close any serial monitor or terminal, then try again.',
  },
  permission: {
    title: 'The browser blocked the port',
    body: 'Allow access to the serial port in this site’s settings, then try again.',
  },
  'wrong-chip': {
    title: 'That is not the board you chose',
    body: 'Nothing was written.',
  },
  'wrong-flash': {
    title: 'This board has the wrong flash',
    body: 'Nothing was written.',
  },
  'boot-mode': {
    title: 'The board did not answer',
    body: 'Hold BOOT, tap RESET, release BOOT — then try again. Some boards need that before they will listen.',
  },
  download: {
    title: 'A part did not download',
    body: 'The Site could not fetch the release. Reload the page and try again.',
  },
  hash: {
    title: 'A part did not match',
    body: 'The file the Site served is not the file the release hashed. Reload and try again.',
  },
  write: {
    title: 'The write stopped',
    body: 'Check that the cable carries data and that the board is still connected, then try again.',
  },
  unknown: {
    title: 'The flash stopped',
    body: 'Nothing else is known about what happened. Try again, and if it repeats, use the manual path below.',
  },
};

/** A failure, in the chosen board's words (F4–F5 name the chip and the flash). */
function failureCopy(cause: FailureCause, board: Board): { title: string; body: string } {
  const base = FAILURE[cause];
  if (cause === 'wrong-chip') {
    return { ...base, body: `${board.name} runs on ${board.chipProse}. Nothing was written.` };
  }
  if (cause === 'wrong-flash') {
    return {
      ...base,
      body: `${board.name} needs ${board.firmware.flash.size} of flash. Nothing was written.`,
    };
  }
  return base;
}

function ManualPath({ board }: { board: Board }) {
  return (
    <details className="min-w-0 rounded-panel border border-border px-4 py-3">
      <summary className="cursor-pointer text-sm font-light">Use another tool</summary>
      <p className="mt-3 text-xs text-muted-foreground">
        Download {board.name}’s parts and write them with esptool. The offsets and the sizes are
        the release’s own.
      </p>
      <ul className="mt-2 grid gap-1 text-xs">
        {board.firmware.parts.map((part) => (
          <li key={part.file} className="flex flex-wrap items-baseline gap-x-2">
            <a
              href={partUrl(board, part)}
              download
              className="underline underline-offset-4 transition-colors hover:text-foreground"
            >
              {part.file}
            </a>
            <span className="text-muted-foreground">
              {hex(part.offset)} · {part.size.toLocaleString()} B
            </span>
          </li>
        ))}
      </ul>
      <pre className="mt-3 w-full max-w-full overflow-x-auto rounded-panel bg-muted p-3 text-xs">
        <code>{manualCommand(board)}</code>
      </pre>
    </details>
  );
}

/** What to do once the board has the firmware: the K7's setup, or the player's handover (F11). */
function AfterFlash({ board }: { board: Board }) {
  if (board.id === 'player') {
    return (
      <ol className="grid list-decimal gap-2 pl-5 text-sm">
        <li>Unplug it and put it within a few metres of the K7. It needs no cable and no computer.</li>
        <li>
          On the K7, open <strong>Menu → Wi-Fi → Share</strong> and start the window.
        </li>
        <li>
          The K7 hands it the network and it starts playing. There is no Wi-Fi to type into the
          player.
        </li>
      </ol>
    );
  }
  return (
    <ol className="grid list-decimal gap-2 pl-5 text-sm">
      <li>
        Join <code>K7.local</code> from a phone or a laptop — the password is{' '}
        <code>12345678</code> unless it was changed.
      </li>
      <li>
        Open{' '}
        <a
          href="http://192.168.4.1/setup"
          className="underline underline-offset-4 transition-colors hover:text-foreground"
        >
          192.168.4.1/setup
        </a>{' '}
        and choose your network.
      </li>
      <li>
        Then{' '}
        <a
          href="/app"
          className="underline underline-offset-4 transition-colors hover:text-foreground"
        >
          install K7
        </a>{' '}
        from this Site.
      </li>
    </ol>
  );
}

export function Flash() {
  const [capable] = useState(() => canFlash());
  const [board, setBoard] = useState<Board>(BOARDS[0]!);
  const [state, setState] = useState<MachineState>(() => initialState(board));
  const [erasePending, setErasePending] = useState(false);
  const machine = useRef<FlashMachine | null>(null);

  useEffect(() => {
    let live = true;
    const created = new FlashMachine(
      board,
      { openDevice: openSerialDevice, download: downloadPart },
      (next) => {
        if (live) setState(next);
      },
    );
    machine.current = created;
    return () => {
      live = false;
      machine.current = null;
      void created.reset();
    };
  }, [board]);

  /* A board is a different release, a different chip and a different set of steps: choosing
   * another one starts the section over rather than carrying a port across. */
  const choose = (next: Board): void => {
    if (next.id === board.id) return;
    setBoard(next);
    setState(initialState(next));
    setErasePending(false);
  };

  const prepare = (): void => {
    setErasePending(false);
    void machine.current?.prepare();
  };

  const flash = (erase: boolean): void => {
    setErasePending(false);
    void machine.current?.flash({ erase });
  };

  const reset = (): void => {
    setErasePending(false);
    void machine.current?.reset();
  };

  const percent = Math.min(100, Math.round((state.written / Math.max(state.total, 1)) * 100));
  const failure = state.cause ? failureCopy(state.cause, board) : null;

  return (
    <Card>
      <CardHeader>
        <CardTitle>Flash a board</CardTitle>
        <CardDescription>
          {board.name} {board.firmware.version} for {board.hardware}.
        </CardDescription>
      </CardHeader>

      <CardContent className="grid gap-4">
        {!capable ? (
          <Alert variant="warning">
            <MonitorX aria-hidden="true" />
            <AlertTitle>This browser cannot flash</AlertTitle>
            <AlertDescription>
              Flashing needs Web Serial: Chrome or Edge on a computer, with a cable that
              carries data. A phone or tablet cannot do it.
            </AlertDescription>
          </Alert>
        ) : null}

        {state.step === 'idle' || state.step === 'failed' ? (
          <div className="grid gap-2">
            <div className="flex flex-wrap gap-2">
              {BOARDS.map((option) => (
                <Button
                  key={option.id}
                  variant={option.id === board.id ? 'solid' : 'outline'}
                  aria-pressed={option.id === board.id}
                  onClick={() => choose(option)}
                >
                  {option.name}
                </Button>
              ))}
            </div>
            <p className="text-xs text-muted-foreground">{board.summary}</p>
          </div>
        ) : null}

        {capable && state.step === 'idle' ? (
          <>
            <ul className="grid gap-1 text-xs text-muted-foreground">
              <li className="flex items-center gap-2">
                <Cable aria-hidden="true" className="size-3.5" /> A USB cable that carries
                data, not just power.
              </li>
              <li className="flex items-center gap-2">
                <Usb aria-hidden="true" className="size-3.5" /> The board, plugged into this
                computer.
              </li>
              <li className="flex items-center gap-2">
                <Check aria-hidden="true" className="size-3.5" /> That is all. Nothing is
                written until you confirm.
              </li>
            </ul>
            <Button variant="solid" onClick={prepare}>Connect {board.name}</Button>
          </>
        ) : null}

        {state.step === 'opening' ? (
          <p className="flex items-center gap-2 text-sm text-muted-foreground">
            <Spinner className="size-4" /> Waiting for a port…
          </p>
        ) : null}

        {state.step === 'ready' && state.identity ? (
          <>
            <Alert variant="info">
              <Usb aria-hidden="true" />
              <AlertTitle>Board found</AlertTitle>
              <AlertDescription>
                {state.identity.port} · {state.identity.chip} · {state.identity.flashSize} of
                flash
              </AlertDescription>
            </Alert>
            <p className="text-xs text-muted-foreground">
              The Site will write the bootloader, the partition table and the app for{' '}
              {board.name} {state.version}.{' '}
              {board.id === 'k7'
                ? 'The card is not touched, and neither is the board’s stored network.'
                : 'The network record the K7 handed it is not touched.'}
            </p>
            {erasePending ? (
              <>
                <Alert variant="destructive">
                  <Eraser aria-hidden="true" />
                  <AlertTitle>Erase the whole chip?</AlertTitle>
                  <AlertDescription>
                    {board.id === 'k7'
                      ? 'This erases the stored WiFi network, the setup password and the device’s Snapshot, including its playlists. The card’s music is not touched.'
                      : 'This erases the network record the K7 handed it, so the Share window has to hand it over again after the flash.'}
                  </AlertDescription>
                </Alert>
                <div className="flex flex-wrap gap-2">
                  <Button variant="solid" onClick={() => flash(true)}>
                    Erase and flash
                  </Button>
                  <Button variant="ghost" onClick={() => setErasePending(false)}>
                    Cancel
                  </Button>
                </div>
              </>
            ) : (
              <div className="flex flex-wrap gap-2">
                <Button variant="solid" onClick={() => flash(false)}>
                  Flash {board.name} {state.version}
                </Button>
                <Button variant="ghost" onClick={() => setErasePending(true)}>
                  Erase and install fresh…
                </Button>
              </div>
            )}
            <Button variant="ghost" size="sm" onClick={reset}>
              Cancel
            </Button>
          </>
        ) : null}

        {state.step === 'downloading' ? (
          <>
            <p className="text-sm text-muted-foreground">Checking {board.name}’s parts…</p>
            <Progress value={0} />
          </>
        ) : null}

        {state.step === 'writing' ? (
          <>
            <p className="text-sm">
              Writing {percent}% — do not unplug the board.
            </p>
            <Progress value={percent} />
            <p className="text-xs text-muted-foreground">
              {state.written.toLocaleString()} of {state.total.toLocaleString()} bytes
            </p>
          </>
        ) : null}

        {state.step === 'done' ? (
          <>
            <Alert variant="success">
              <Check aria-hidden="true" />
              <AlertTitle>
                {board.name} {state.version} is on the board
              </AlertTitle>
              <AlertDescription>
                {board.id === 'k7'
                  ? 'It restarts with no network and raises its own setup network.'
                  : 'It restarts and looks for a K7 to take its network from.'}
              </AlertDescription>
            </Alert>
            <AfterFlash board={board} />
            <Button variant="outline" onClick={reset}>
              Flash another board
            </Button>
          </>
        ) : null}

        {state.step === 'failed' && failure ? (
          <>
            <Alert variant="destructive">
              <MonitorX aria-hidden="true" />
              <AlertTitle>{failure.title}</AlertTitle>
              <AlertDescription>{failure.body}</AlertDescription>
            </Alert>
            {state.message ? (
              <p className="text-xs text-muted-foreground">{state.message}</p>
            ) : null}
            <div className="flex flex-wrap gap-2">
              <Button variant="solid" onClick={prepare}>
                <RefreshCw aria-hidden="true" /> Try again
              </Button>
              <Button variant="ghost" onClick={reset}>
                Start over
              </Button>
            </div>
          </>
        ) : null}

        {!capable || state.step === 'failed' ? <ManualPath board={board} /> : null}
      </CardContent>
    </Card>
  );
}
