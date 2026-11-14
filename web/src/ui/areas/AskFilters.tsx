import { useId, useState } from 'react';

import { Button } from '@/components/ui/button';
import {
  DropdownMenu,
  DropdownMenuContent,
  DropdownMenuGroup,
  DropdownMenuItem,
  DropdownMenuLabel,
  DropdownMenuSeparator,
  DropdownMenuTrigger,
} from '@/components/ui/dropdown-menu';
import { Input } from '@/components/ui/input';
import {
  Sheet,
  SheetContent,
  SheetDescription,
  SheetFooter,
  SheetHeader,
  SheetTitle,
} from '@/components/ui/sheet';
import type { SearchArgs } from '../../device/mcp';
import { clampBpm, MOOD_VALUES, SORT_VALUES } from '../../playlists/ask';

/**
 * The `library_search` arguments behind an answer, for the times the words did not land.
 *
 * Ask does not show this panel: the answer already says what it understood, and the model
 * writes the filters. But a misparse still has to have a way out that is not another guess,
 * so the same fields stay one control away — and they are the form for a machine without
 * the built-in model (ADR 0086).
 */

function NumberField({
  label,
  value,
  min,
  max,
  onChange,
}: {
  label: string;
  value: number | undefined;
  min: number;
  max: number;
  onChange: (value: number | undefined) => void;
}) {
  const id = useId();
  return (
    <div className="flex min-w-0 flex-col gap-1">
      <label htmlFor={id} className="text-xs text-muted-foreground">
        {label}
      </label>
      <Input
        id={id}
        type="number"
        min={min}
        max={max}
        step="any"
        value={value ?? ''}
        onChange={(event) => {
          const raw = event.target.value;
          if (raw === '') {
            onChange(undefined);
            return;
          }
          const parsed = Number(raw);
          onChange(Number.isFinite(parsed) ? parsed : undefined);
        }}
      />
    </div>
  );
}

function TextField({
  label,
  value,
  placeholder,
  onChange,
}: {
  label: string;
  value: string | undefined;
  placeholder?: string;
  onChange: (value: string | undefined) => void;
}) {
  const id = useId();
  return (
    <div className="flex min-w-0 flex-col gap-1">
      <label htmlFor={id} className="text-xs text-muted-foreground">
        {label}
      </label>
      <Input
        id={id}
        value={value ?? ''}
        placeholder={placeholder}
        maxLength={120}
        autoComplete="off"
        onChange={(event) => onChange(event.target.value || undefined)}
      />
    </div>
  );
}

export function AskFilters({
  initial,
  onClose,
  onRun,
}: {
  initial: SearchArgs;
  onClose: () => void;
  onRun: (intent: SearchArgs) => void;
}) {
  const [intent, setIntent] = useState<SearchArgs>(() => ({
    ...initial,
    key: initial.key ? [...initial.key] : undefined,
  }));
  const [keyText, setKeyText] = useState((initial.key ?? []).join(', '));
  const keysField = useId();
  const moods = intent.mood ?? [];

  function setField<K extends keyof SearchArgs>(field: K, value: SearchArgs[K] | undefined) {
    setIntent((current) => {
      const next = { ...current };
      if (value === undefined) delete next[field];
      else next[field] = value;
      return next;
    });
  }

  const toggleMood = (mood: string) => {
    const next = moods.includes(mood) ? moods.filter((each) => each !== mood) : [...moods, mood];
    setField('mood', next.length > 0 ? next : undefined);
  };

  const setKeys = (text: string) => {
    setKeyText(text);
    const keys = text
      .split(',')
      .map((each) => each.trim())
      .filter((each) => each.length > 0)
      .slice(0, 24);
    setField('key', keys.length > 0 ? keys : undefined);
  };

  const run = () => {
    /* The library stores folded tempo only, so a hand-typed bound outside 70–180 could never
     * match. Clamp before the search, the same way a model answer is clamped. */
    const args: SearchArgs = { ...intent };
    if (args.bpmMin !== undefined) args.bpmMin = clampBpm(args.bpmMin);
    if (args.bpmMax !== undefined) args.bpmMax = clampBpm(args.bpmMax);
    onRun(args);
  };

  return (
    <Sheet
      open
      onOpenChange={(open) => {
        if (!open) onClose();
      }}
    >
      <SheetContent
        side="right"
        className="w-full! sm:max-w-md!"
      >
        <SheetHeader>
          <SheetTitle>Filters</SheetTitle>
          <SheetDescription>
            The arguments the search runs. Ask writes these for you; change anything when it
            read you wrong.
          </SheetDescription>
        </SheetHeader>

        <div className="min-h-0 flex-1 overflow-y-auto px-4">
          <div className="grid grid-cols-1 gap-3 sm:grid-cols-2">
            <TextField
              label="Words to find"
              value={intent.query}
              placeholder="title, artist, album or genre"
              onChange={(value) => setField('query', value)}
            />
            <TextField
              label="Genre"
              value={intent.genre}
              placeholder="ambient"
              onChange={(value) => setField('genre', value)}
            />
            <div className="flex min-w-0 flex-col gap-1">
              <span className="text-xs text-muted-foreground">Sort</span>
              <DropdownMenu>
                <DropdownMenuTrigger render={<Button variant="outline" size="sm" />}>
                  {intent.sort ? `Sort: ${intent.sort}` : 'Sort: none'}
                </DropdownMenuTrigger>
                <DropdownMenuContent align="start">
                  {/* Base UI's label needs a group; outside one it throws and takes the
                   * popup down (error #31). Radix does not require this. */}
                  <DropdownMenuGroup>
                    <DropdownMenuLabel>Sort</DropdownMenuLabel>
                    <DropdownMenuItem onClick={() => setField('sort', undefined)}>
                      None
                    </DropdownMenuItem>
                    <DropdownMenuSeparator />
                    {SORT_VALUES.map((sort) => (
                      <DropdownMenuItem key={sort} onClick={() => setField('sort', sort)}>
                        {sort}
                      </DropdownMenuItem>
                    ))}
                  </DropdownMenuGroup>
                </DropdownMenuContent>
              </DropdownMenu>
            </div>

            <NumberField
              label="BPM min"
              value={intent.bpmMin}
              min={70}
              max={180}
              onChange={(value) => setField('bpmMin', value)}
            />
            <NumberField
              label="BPM max"
              value={intent.bpmMax}
              min={70}
              max={180}
              onChange={(value) => setField('bpmMax', value)}
            />
            <div className="flex min-w-0 flex-col gap-1">
              <label htmlFor={keysField} className="text-xs text-muted-foreground">
                Keys (Camelot or note names)
              </label>
              <Input
                id={keysField}
                value={keyText}
                placeholder="8A, 9A"
                autoComplete="off"
                onChange={(event) => setKeys(event.target.value)}
              />
            </div>

            <NumberField
              label="Energy min"
              value={intent.energyMin}
              min={0}
              max={100}
              onChange={(value) => setField('energyMin', value)}
            />
            <NumberField
              label="Energy max"
              value={intent.energyMax}
              min={0}
              max={100}
              onChange={(value) => setField('energyMax', value)}
            />
            <NumberField
              label="Danceability min"
              value={intent.danceabilityMin}
              min={0}
              max={100}
              onChange={(value) => setField('danceabilityMin', value)}
            />
            <NumberField
              label="Danceability max"
              value={intent.danceabilityMax}
              min={0}
              max={100}
              onChange={(value) => setField('danceabilityMax', value)}
            />
            <NumberField
              label="Valence min"
              value={intent.valenceMin}
              min={0}
              max={100}
              onChange={(value) => setField('valenceMin', value)}
            />
            <NumberField
              label="Valence max"
              value={intent.valenceMax}
              min={0}
              max={100}
              onChange={(value) => setField('valenceMax', value)}
            />
            <NumberField
              label="Arousal min"
              value={intent.arousalMin}
              min={0}
              max={100}
              onChange={(value) => setField('arousalMin', value)}
            />
            <NumberField
              label="Arousal max"
              value={intent.arousalMax}
              min={0}
              max={100}
              onChange={(value) => setField('arousalMax', value)}
            />
            <NumberField
              label="Rows"
              value={intent.limit}
              min={1}
              max={100}
              onChange={(value) => setField('limit', value)}
            />
            <NumberField
              label="Skip"
              value={intent.offset}
              min={0}
              max={199}
              onChange={(value) => setField('offset', value)}
            />
          </div>

          <div className="flex flex-col gap-1.5 pt-4">
            <span className="text-xs text-muted-foreground">Mood</span>
            <div className="flex flex-wrap gap-1.5">
              {MOOD_VALUES.map((mood) => {
                const on = moods.includes(mood);
                return (
                  <Button
                    key={mood}
                    type="button"
                    size="sm"
                    variant={on ? 'solid' : 'outline'}
                    aria-pressed={on}
                    onClick={() => toggleMood(mood)}
                  >
                    {mood}
                  </Button>
                );
              })}
            </div>
          </div>
        </div>

        <SheetFooter className="flex-row justify-end border-t">
          <Button type="button" variant="outline" size="sm" onClick={onClose}>
            Cancel
          </Button>
          <Button type="button" variant="solid" size="sm" onClick={run}>
            Run search
          </Button>
        </SheetFooter>
      </SheetContent>
    </Sheet>
  );
}
