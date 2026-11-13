import { useState } from 'react';
import {
  ChevronDownIcon,
  Cpu,
  HardDriveIcon,
  RefreshCwIcon,
  TriangleAlert,
} from 'lucide-react';

import { Button } from '@/components/ui/button';
import { Skeleton } from '@/components/ui/skeleton';
import {
  DropdownMenu,
  DropdownMenuContent,
  DropdownMenuGroup,
  DropdownMenuItem,
  DropdownMenuLabel,
  DropdownMenuRadioGroup,
  DropdownMenuRadioItem,
  DropdownMenuSeparator,
  DropdownMenuTrigger,
} from '@/components/ui/dropdown-menu';
import { Spinner } from '@/components/ui/spinner';
import { cn } from '@/lib/utils';
import { setDeviceView, useCardScope, useDeviceView } from '../card/session';
import {
  openVirtualCard,
  reconnectWorkingDirectory,
  type Reconnect,
} from '../card/virtual';
import { useDeviceStatus } from '../device/useDeviceStatus';
import { cardUnits, type CardUnit } from './card-units';
import { useAnalysis } from './state/analysis';
import { refreshVirtualCards, useVirtualCards } from './state/cards';

/**
 * The card switcher: the current card in the header's right, and the way to another one.
 *
 * The player's card and the SD in this computer are two buttons; the virtual cards are one
 * button when there is only one and a menu when there are several. That keeps the header a
 * header — three short controls at most, no strip — while the card in use is always named:
 * the current button takes the accent outline, and the virtual menu's trigger names the
 * current virtual card. The Prepare shelf is still where a card is created, renamed, removed
 * or written; this is the part that has to be reachable on every area.
 */
export function CardSwitcher() {
  const scope = useCardScope();
  const deviceView = useDeviceView();
  const { status } = useDeviceStatus();
  const { cards, loaded, error } = useVirtualCards();
  const { hasCardHandle, changeCard, selectRealCard } = useAnalysis();

  const { units, activeKey } = cardUnits({
    cards,
    device: status,
    scope,
    deviceView,
    hasCardHandle,
  });
  const device = units.find((unit) => unit.kind === 'device') ?? null;
  const sd = units.find((unit) => unit.kind === 'sd') ?? null;
  const virtuals = units.filter(
    (unit): unit is Extract<CardUnit, { kind: 'virtual' }> => unit.kind === 'virtual',
  );
  const activeVirtual = virtuals.find((unit) => unit.key === activeKey) ?? null;
  /* The SD is the only unit guaranteed to be there; the rest are optional. */
  const hasVirtualMenu = virtuals.length > 1;

  const [open, setOpen] = useState(false);
  const [opening, setOpening] = useState<string | null>(null);
  const [note, setNote] = useState<string | null>(null);
  const busy = opening !== null;

  const select = (unit: CardUnit) => {
    if (busy) return;
    if (unit.key === activeKey) {
      /* Re-picking the current card is not a change; it still puts the menu away. */
      setOpen(false);
      return;
    }
    setNote(null);
    if (unit.kind === 'device') {
      setDeviceView(true);
      return;
    }
    if (unit.kind === 'sd') {
      if (unit.loaded) selectRealCard();
      else void changeCard();
      return;
    }
    void openVirtual(unit);
  };

  const openVirtual = async (unit: Extract<CardUnit, { kind: 'virtual' }>) => {
    setOpening(unit.key);
    setNote(null);
    try {
      const directory = await reconnectWorkingDirectory();
      if (directory.kind === 'cancelled') return;
      if (directory.kind !== 'ready') {
        setNote(refusal(directory));
        if (hasVirtualMenu) setOpen(true);
        return;
      }
      const handle = await directory.directory.getDirectoryHandle(unit.card.folder);
      await openVirtualCard(unit.card, handle);
      setOpen(false);
    } catch (cause) {
      setNote(
        `Could not open ${unit.label}: ${cause instanceof Error ? cause.message : String(cause)}`,
      );
      if (hasVirtualMenu) setOpen(true);
    } finally {
      setOpening(null);
    }
  };

  /* One line for an error the menu is not there to carry: a single virtual card, or no
   * virtual cards at all. The menu itself shows these when it exists. */
  const inline = hasVirtualMenu
    ? null
    : note ?? (error !== null ? 'The cards could not be read.' : null);
  const retryInline = error !== null && note === null && !hasVirtualMenu;

  return (
    <div
      aria-busy={busy}
      className="flex min-w-0 items-center gap-1"
    >
      {device !== null ? (
        <UnitButton
          icon={<Cpu aria-hidden="true" />}
          label="Device"
          active={activeKey === 'device'}
          disabled={busy}
          onClick={() => select(device)}
        />
      ) : null}

      {sd !== null ? (
        <UnitButton
          icon={<HardDriveIcon aria-hidden="true" />}
          label="SD card"
          hint={sd.loaded ? undefined : 'SD card, choose one'}
          active={activeKey === 'sd'}
          disabled={busy}
          onClick={() => select(sd)}
        />
      ) : null}

      {virtuals.length === 1 ? (
        <VirtualButton
          unit={virtuals[0]}
          active={activeKey === virtuals[0].key}
          opening={opening === virtuals[0].key}
          disabled={busy}
          onClick={() => select(virtuals[0])}
        />
      ) : null}

      {!loaded && error === null ? (
        <Skeleton aria-hidden="true" className="h-6 w-16 rounded-full md:h-7 md:w-20" />
      ) : null}

      {hasVirtualMenu ? (
        <DropdownMenu
          open={open}
          onOpenChange={(next) => {
            setOpen(next);
            if (!next) setNote(null);
          }}
        >
          <DropdownMenuTrigger
            render={
              <Button
                type="button"
                variant="ghost"
                size="sm"
                aria-label={
                  activeVirtual === null
                    ? 'Virtual cards'
                    : `Virtual card: ${activeVirtual.label}`
                }
                className={cn(
                  'max-w-24 gap-1.5 md:max-w-56',
                  activeVirtual !== null &&
                    'border-primary text-primary hover:border-primary',
                )}
              />
            }
          >
            {busy ? (
              <Spinner aria-hidden="true" className="size-3.5 max-md:hidden" />
            ) : (
              <HardDriveIcon aria-hidden="true" className="max-md:hidden" />
            )}
            <span className="truncate">{activeVirtual?.label ?? 'Virtual cards'}</span>
            <ChevronDownIcon aria-hidden="true" className="size-3.5 text-muted-foreground" />
          </DropdownMenuTrigger>

          <DropdownMenuContent align="end" sideOffset={6} className="min-w-60">
            <DropdownMenuGroup>
              <DropdownMenuLabel>Virtual cards</DropdownMenuLabel>
              <DropdownMenuRadioGroup
                value={activeVirtual?.key ?? ''}
                aria-label="Virtual cards"
                onValueChange={(value) => {
                  const unit = virtuals.find((each) => each.key === String(value));
                  if (unit !== undefined) select(unit);
                }}
                disabled={busy}
              >
                {virtuals.map((unit) => (
                  <DropdownMenuRadioItem
                    key={unit.key}
                    value={unit.key}
                    className="gap-2.5 rounded-[4px] py-1.5 pr-8 pl-2.5 focus:bg-foreground focus:text-background focus:**:text-background data-[checked]:bg-foreground data-[checked]:text-background data-[checked]:**:text-background"
                  >
                    <HardDriveIcon aria-hidden="true" />
                    <span className="truncate">{unit.label}</span>
                  </DropdownMenuRadioItem>
                ))}
              </DropdownMenuRadioGroup>
              {busy ? (
                <div role="status" className="px-2.5 py-1 text-xs text-muted-foreground">
                  Opening the card…
                </div>
              ) : null}
            </DropdownMenuGroup>

            {error !== null ? (
              <>
                <DropdownMenuSeparator />
                <div role="alert" className="px-2.5 py-1 text-xs text-muted-foreground">
                  The cards could not be read.
                </div>
                <DropdownMenuItem onClick={() => void refreshVirtualCards()}>
                  Try again
                </DropdownMenuItem>
              </>
            ) : null}

            {note !== null ? (
              <>
                <DropdownMenuSeparator />
                <div role="alert" className="px-2.5 py-1 text-xs text-muted-foreground">
                  {note}
                </div>
              </>
            ) : null}
          </DropdownMenuContent>
        </DropdownMenu>
      ) : null}

      {inline !== null ? (
        <span
          role="alert"
          title={inline}
          className="flex min-w-0 items-center gap-1 text-xs text-muted-foreground"
        >
          <TriangleAlert aria-hidden="true" className="size-4 shrink-0" />
          <span className="max-w-40 truncate max-md:sr-only">{inline}</span>
        </span>
      ) : null}
      {retryInline ? (
        <Button
          type="button"
          size="icon-xs"
          variant="ghost"
          aria-label="Retry reading the cards"
          title="Retry reading the cards"
          onClick={() => void refreshVirtualCards()}
        >
          <RefreshCwIcon aria-hidden="true" />
        </Button>
      ) : null}
    </div>
  );
}

/** The two known units: a button whose label is a word on a desktop and a glyph on a phone. */
function UnitButton({
  icon,
  label,
  hint,
  active,
  disabled,
  onClick,
}: {
  icon: React.ReactNode;
  label: string;
  hint?: string;
  active: boolean;
  disabled: boolean;
  onClick: () => void;
}) {
  return (
    <>
      <Button
        type="button"
        variant="ghost"
        size="icon-xs"
        aria-pressed={active}
        aria-label={hint ?? label}
        title={hint ?? label}
        disabled={disabled}
        onClick={onClick}
        className="md:hidden aria-pressed:hover:border-primary"
      >
        {icon}
      </Button>
      <Button
        type="button"
        variant="ghost"
        size="sm"
        aria-pressed={active}
        aria-label={hint ?? label}
        title={hint ?? label}
        disabled={disabled}
        onClick={onClick}
        className="hidden gap-1.5 aria-pressed:hover:border-primary md:inline-flex"
      >
        {icon}
        {label}
      </Button>
    </>
  );
}

/** One virtual card, when it is the only one: a direct button that names it. */
function VirtualButton({
  unit,
  active,
  opening,
  disabled,
  onClick,
}: {
  unit: Extract<CardUnit, { kind: 'virtual' }>;
  active: boolean;
  opening: boolean;
  disabled: boolean;
  onClick: () => void;
}) {
  return (
    <>
      <Button
        type="button"
        variant="ghost"
        size="xs"
        aria-pressed={active}
        aria-label={unit.label}
        title={unit.label}
        disabled={disabled}
        onClick={onClick}
        className="max-w-24 gap-1 aria-pressed:hover:border-primary md:hidden"
      >
        {opening ? <Spinner aria-hidden="true" className="size-3.5" /> : null}
        <span className="truncate">{unit.label}</span>
      </Button>
      <Button
        type="button"
        variant="ghost"
        size="sm"
        aria-pressed={active}
        aria-label={unit.label}
        title={unit.label}
        disabled={disabled}
        onClick={onClick}
        className="hidden max-w-56 gap-1.5 aria-pressed:hover:border-primary md:inline-flex"
      >
        {opening ? (
          <Spinner aria-hidden="true" className="size-3.5" />
        ) : (
          <HardDriveIcon aria-hidden="true" />
        )}
        <span className="truncate">{unit.label}</span>
      </Button>
    </>
  );
}

/** The sentence a refused, cancelled or unsupported working directory gets. */
function refusal(result: Reconnect): string {
  if (result.kind === 'cancelled') return 'Opening the card was cancelled.';
  if (result.kind === 'denied') return 'Permission to the working directory was refused.';
  if (result.kind === 'unsupported') return 'This browser cannot open a folder for writing.';
  if (result.kind === 'failed') return `Could not open the working directory: ${result.message}`;
  return '';
}
