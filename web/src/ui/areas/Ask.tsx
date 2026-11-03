import { useId, useState, useSyncExternalStore, type FormEvent } from 'react';
import { ListMusic, Sparkles } from 'lucide-react';

import { Alert, AlertAction, AlertDescription } from '@/components/reui/alert';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from '@/components/ui/card';
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
import { Spinner } from '@/components/ui/spinner';
import {
  McpError,
  searchTracks,
  type McpSearch,
  type SearchArgs,
} from '../../device/mcp';
import { MOOD_VALUES, requestIntent, SORT_VALUES } from '../../playlists/ask';
import { createDraft } from '../../playlists/drafts';
import { getSettings, hasProvider, subscribeSettings } from '../../settings';
import { navigate, routeHref } from '../router';

/**
 * Ask (front door §4.3): a sentence, an **Intent**, then a search.
 *
 * The model is asked for `library_search` filters and for nothing else — it never sees a
 * track, an artist or a measurement (ADR 0053, which adopted ADR 0050's shape rule). The
 * Intent is therefore shown and editable before anything runs: what the model got wrong is
 * visible as a number, not as a paragraph about music that does not exist.
 */

function SettingsLink() {
  return (
    <Button
      variant="outline"
      size="sm"
      nativeButton={false}
      render={<a href={routeHref({ name: 'settings' })} />}
    >
      Open Settings
    </Button>
  );
}

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

function describeSearchError(error: unknown): string {
  if (error instanceof McpError) return error.message;
  return 'The device did not answer, so the search did not run.';
}

export function AskArea() {
  const settings = useSyncExternalStore(subscribeSettings, getSettings, getSettings);
  const provider = hasProvider(settings);

  const [sentence, setSentence] = useState('');
  const [asking, setAsking] = useState(false);
  const [askError, setAskError] = useState<string | null>(null);
  const [intentNote, setIntentNote] = useState<string | null>(null);

  const [intent, setIntent] = useState<SearchArgs>({});
  const [keyText, setKeyText] = useState('');
  const keysField = useId();

  const [searching, setSearching] = useState(false);
  const [search, setSearch] = useState<McpSearch | null>(null);
  const [searchError, setSearchError] = useState<string | null>(null);

  function setField<K extends keyof SearchArgs>(field: K, value: SearchArgs[K] | undefined) {
    setIntent((current) => {
      const next = { ...current };
      if (value === undefined) delete next[field];
      else next[field] = value;
      return next;
    });
  }

  const moods = intent.mood ?? [];

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

  const ask = async (event: FormEvent) => {
    event.preventDefault();
    const asked = sentence.trim();
    if (!asked || asking) return;

    setAsking(true);
    setAskError(null);
    setIntentNote(null);
    const result = await requestIntent(asked, settings);
    setAsking(false);

    if ('error' in result) {
      setAskError(result.error);
      return;
    }

    setIntent(result);
    setKeyText((result.key ?? []).join(', '));
    setSearch(null);
    setSearchError(null);
    setIntentNote('The model proposed these filters. Change anything before running the search.');
  };

  const run = async () => {
    if (searching) return;
    setSearching(true);
    setSearchError(null);
    setSearch(null);
    try {
      setSearch(await searchTracks(intent));
    } catch (error) {
      setSearchError(describeSearchError(error));
    } finally {
      setSearching(false);
    }
  };

  const create = () => {
    if (!search || search.tracks.length === 0) return;
    const name = sentence.trim().slice(0, 60) || 'Asked set';
    const draft = createDraft(
      name,
      search.tracks.map((track) => track.path),
    );
    navigate(routeHref({ name: 'playlist', id: draft.id }));
  };

  return (
    <div className="mx-auto flex w-full max-w-4xl flex-col gap-6 p-4 sm:p-6">
      <header className="flex flex-col gap-0.5">
        <h1 className="text-xl font-semibold tracking-tight">Ask</h1>
        <p className="text-sm text-muted-foreground">
          A sentence in, filters out. The model never sees your library, and nothing is added
          until you make it a playlist.
        </p>
      </header>

      {!provider ? (
        <Alert variant="info">
          <AlertDescription>
            A Provider is needed to ask the model. Set its base URL, its model and its key in
            Settings; the filters below work on their own without one.
          </AlertDescription>
          <AlertAction>
            <SettingsLink />
          </AlertAction>
        </Alert>
      ) : null}

      {askError ? (
        <Alert variant="destructive">
          <AlertDescription>{askError}</AlertDescription>
          <AlertAction>
            <Button variant="outline" size="sm" onClick={() => setAskError(null)}>
              Dismiss
            </Button>
          </AlertAction>
        </Alert>
      ) : null}

      <Card>
        <CardHeader>
          <CardTitle>What are you after?</CardTitle>
          <CardDescription>
            For example: a late-night 8A set, 120–126 BPM, twenty tracks.
          </CardDescription>
        </CardHeader>
        <CardContent>
          <form onSubmit={ask} className="flex flex-col gap-2 sm:flex-row sm:items-end">
            <div className="flex min-w-0 flex-1 flex-col gap-1">
              <label htmlFor="ask-sentence" className="text-xs text-muted-foreground">
                Sentence
              </label>
              <Input
                id="ask-sentence"
                value={sentence}
                onChange={(event) => setSentence(event.target.value)}
                placeholder="A late-night 8A set, 120–126 BPM, twenty tracks"
                maxLength={280}
                autoComplete="off"
              />
            </div>
            <Button type="submit" size="sm" disabled={!provider || asking || !sentence.trim()}>
              {asking ? (
                <Spinner aria-hidden="true" className="size-3.5" />
              ) : (
                <Sparkles aria-hidden="true" />
              )}
              Ask the model
            </Button>
          </form>
        </CardContent>
      </Card>

      <Card>
        <CardHeader>
          <CardTitle>Intent</CardTitle>
          <CardDescription>
            {intentNote ??
              'The filters that will be run. Edit any of them, or write them yourself.'}
          </CardDescription>
        </CardHeader>
        <CardContent className="flex flex-col gap-3">
          <div className="grid gap-3 sm:grid-cols-2 lg:grid-cols-3">
            <TextField
              label="Words to find"
              value={intent.query}
              placeholder="path, title, artist, album or genre"
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
              min={0}
              max={400}
              onChange={(value) => setField('bpmMin', value)}
            />
            <NumberField
              label="BPM max"
              value={intent.bpmMax}
              min={0}
              max={400}
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

          <div className="flex flex-col gap-1">
            <span className="text-xs text-muted-foreground">Mood</span>
            <div className="flex flex-wrap gap-1.5">
              {MOOD_VALUES.map((mood) => {
                const on = moods.includes(mood);
                return (
                  <Button
                    key={mood}
                    type="button"
                    size="sm"
                    variant={on ? 'secondary' : 'outline'}
                    aria-pressed={on}
                    onClick={() => toggleMood(mood)}
                  >
                    {mood}
                  </Button>
                );
              })}
            </div>
          </div>

          <div className="flex flex-wrap items-center gap-2 border-t pt-3">
            <Button size="sm" onClick={() => void run()} disabled={searching}>
              {searching ? <Spinner aria-hidden="true" className="size-3.5" /> : null}
              Run search
            </Button>
            <span className="text-xs text-muted-foreground">
              Runs against the device's own MCP endpoint.
            </span>
          </div>

          {searchError ? (
            <p className="text-sm text-destructive" role="alert">
              {searchError}
            </p>
          ) : null}
        </CardContent>
      </Card>

      {search ? (
        <Card>
          <CardHeader>
            <CardTitle>Results</CardTitle>
            <CardDescription className="tabular-nums">
              {search.tracks.length} of {search.matched} matched
              {search.truncated ? ', the first page' : ''}
            </CardDescription>
          </CardHeader>
          <CardContent className="flex flex-col gap-3">
            {search.tracks.length === 0 ? (
              <p className="text-sm text-muted-foreground">Nothing matched those filters.</p>
            ) : (
              <ul className="flex flex-col">
                {search.tracks.map((track) => (
                  <li
                    key={track.path}
                    className="flex items-center gap-2 border-b py-2 last:border-b-0"
                  >
                    <ListMusic
                      className="size-4 shrink-0 text-muted-foreground"
                      aria-hidden="true"
                    />
                    <div className="flex min-w-0 flex-1 flex-col gap-0.5">
                      <span className="truncate text-sm" title={track.path}>
                        {track.analysis?.title || track.name}
                      </span>
                      <span className="truncate text-xs text-muted-foreground">
                        {track.analysis?.artist
                          ? `${track.analysis.artist} · ${track.path}`
                          : track.path}
                      </span>
                    </div>
                    <div className="flex shrink-0 items-center gap-2 text-xs text-muted-foreground tabular-nums">
                      {track.analysis?.bpm != null ? (
                        <span>{track.analysis.bpm.toFixed(1)} BPM</span>
                      ) : null}
                      {track.analysis?.key ? <span>{track.analysis.key}</span> : null}
                    </div>
                  </li>
                ))}
              </ul>
            )}
            <div className="flex flex-wrap items-center gap-2 border-t pt-3">
              <Button size="sm" onClick={create} disabled={search.tracks.length === 0}>
                Create playlist from these
              </Button>
              <span className="text-xs text-muted-foreground">
                Becomes a draft here; Process sends it to the player.
              </span>
            </div>
          </CardContent>
        </Card>
      ) : null}
    </div>
  );
}
