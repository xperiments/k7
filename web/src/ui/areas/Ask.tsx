import { useEffect, useRef, useState, type FormEvent } from 'react';
import { ArrowUp, ListMusic, Sparkles, TriangleAlert } from 'lucide-react';
import { cn } from 'cn';

import { Alert, AlertDescription } from '@/components/reui/alert';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardFooter } from '@/components/ui/card';
import { Input } from '@/components/ui/input';
import { Progress } from '@/components/ui/progress';
import { Spinner } from '@/components/ui/spinner';
import {
  assistantFailure,
  assistantSentence,
  assistantState,
  destroyBases,
  downloadModel,
  type AssistantState,
} from '../../builtin-model';
import { useCardScope, useDeviceView } from '../../card/session';
import { McpError, type McpSearch, type SearchArgs } from '../../device/mcp';
import { resolveSource, type LibrarySource } from '../../library/source';
import { formatDuration } from '../format';
import { newId } from '../../lib/id';
import { purposeFilters, requestIntent, relaxRanges, spreadAnswer } from '../../playlists/ask';
import {
  answerText,
  CARD_CHANGED,
  filterWords,
  nameFor,
  OPENERS,
  READ_ONLY_SOURCE,
  SAVED_NOTE,
  saveQuestion,
  sourceText,
  type ChatMessage,
  type Retry,
} from '../../playlists/chat';
import { createDraft } from '../../playlists/drafts';
import { navigate, routeHref } from '../router';
import { AskFilters } from './AskFilters';

/**
 * **Ask**: a conversation with the library, in the person's words.
 *
 * The model is the **Built-in model** (ADR 0084): it turns one sentence into an **Intent** and
 * nothing else. What the page says back is the App's — the count, the reading, the source and
 * the offer to save are computed from the filters that ran and the search that answered, so a
 * sentence here can never describe a track that does not exist (ADR 0050). The Intent is not a
 * panel in the way; the answer says what was understood, and **Filters** opens the exact
 * arguments for the times it read the sentence wrong (ADR 0086).
 *
 * One session per ask, so each message stands alone: the transcript is a record of asks, not
 * context the model carries. It lives in this tab and is never stored.
 */

interface Found {
  intent: SearchArgs;
  search: McpSearch;
  relaxed: boolean;
}

function describeSearchError(error: unknown): string {
  if (error instanceof McpError) return error.message;
  return 'The library could not be read, so the search did not run.';
}

/**
 * The exact search, then one relaxed retry. An ask passes the sentence's purpose so a range-heavy
 * over-fill can retry with the pair the table prescribes; a hand-run Intent passes nothing, so the
 * filters a person set by hand are only relaxed when a mood is left to search on.
 */
async function find(
  source: LibrarySource,
  intent: SearchArgs,
  options?: { purpose?: Partial<SearchArgs> | null },
): Promise<Found> {
  const search = (args: SearchArgs) => source.search(args);
  const exact = await spreadAnswer(search, intent);
  if (exact.tracks.length > 0) return { intent, search: exact, relaxed: false };

  const relaxed = relaxRanges(intent, options);
  if (!relaxed) return { intent, search: exact, relaxed: false };
  const retry = await spreadAnswer(search, relaxed);
  if (retry.tracks.length === 0) return { intent, search: exact, relaxed: false };
  return { intent: relaxed, search: retry, relaxed: true };
}

function answerMessage(
  source: LibrarySource,
  found: Found,
  asked: string | null,
): ChatMessage {
  const manual = asked === null;
  return {
    id: newId(),
    role: 'assistant',
    answer: {
      intent: found.intent,
      search: found.search,
      source: { label: source.label, kind: source.kind, draftScope: source.draftScope },
      relaxed: found.relaxed,
      manual,
      name: manual
        ? nameFor(filterWords(found.intent).join(' '), 'Asked set')
        : nameFor(asked),
    },
  };
}

function errorMessage(text: string, retry?: Retry): ChatMessage {
  return { id: newId(), role: 'assistant', text, tone: 'error', retry };
}

function TrackRows({ tracks }: { tracks: McpSearch['tracks'] }) {
  return (
    <ul aria-label="Tracks found" className="flex flex-col">
      {tracks.map((track) => (
        <li
          key={track.path}
          className="flex items-center gap-2 border-b py-2 last:border-b-0"
        >
          <ListMusic
            className="size-4 shrink-0 stroke-[1.25] text-muted-foreground"
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
            {track.analysis?.durationMs ? (
              <span>{formatDuration(track.analysis.durationMs / 1000)}</span>
            ) : null}
            {track.analysis?.bpm != null ? (
              <span>{track.analysis.bpm.toFixed(1)} BPM</span>
            ) : null}
            {track.analysis?.key ? <span>{track.analysis.key}</span> : null}
          </div>
        </li>
      ))}
    </ul>
  );
}

function AnswerBlock({
  message,
  scope,
  onFilters,
  onSave,
  onDismiss,
  onOpenDraft,
}: {
  message: ChatMessage;
  scope: string;
  onFilters: (intent: SearchArgs) => void;
  onSave: (message: ChatMessage) => void;
  onDismiss: (message: ChatMessage) => void;
  onOpenDraft: (id: string) => void;
}) {
  const [expanded, setExpanded] = useState(false);
  const answer = message.answer;
  if (!answer) return null;

  const tracks = answer.search.tracks;
  const shown = expanded ? tracks : tracks.slice(0, 4);
  const saveable = answer.source.draftScope !== null;
  const sameCard = answer.source.draftScope === scope;

  return (
    <div className="flex flex-col gap-2">
      <p className="text-sm break-words">
        <span className="sr-only">Ask said: </span>
        {answerText(answer)}
      </p>
      <div className="flex flex-wrap items-center gap-x-2 gap-y-0.5 text-xs text-muted-foreground">
        <span>{sourceText(answer.source)}</span>
        <span aria-hidden="true">·</span>
        <Button
          type="button"
          variant="link"
          size="xs"
          className="h-auto px-0 text-xs"
          onClick={() => onFilters(answer.intent)}
        >
          Filters
        </Button>
      </div>
      {tracks.length === 0 ? (
        <p className="text-xs text-muted-foreground">
          Every filter has to match at once, and a range only sees analysed tracks.
        </p>
      ) : null}

      {tracks.length > 0 ? (
        <Card size="sm">
          <CardContent>
            <TrackRows tracks={shown} />
            {tracks.length > 4 ? (
              <div className="flex justify-end pt-2">
                <Button
                  type="button"
                  variant="ghost"
                  size="xs"
                  aria-expanded={expanded}
                  onClick={() => setExpanded(!expanded)}
                >
                  {expanded ? 'Show fewer' : `Show all ${tracks.length} tracks`}
                </Button>
              </div>
            ) : null}
          </CardContent>
          {answer.draftId ? (
            <CardFooter className="flex flex-col items-start gap-2">
              <p className="text-xs text-muted-foreground">{SAVED_NOTE}</p>
              <Button
                type="button"
                variant="solid"
                size="sm"
                onClick={() => onOpenDraft(answer.draftId!)}
              >
                Open playlist
              </Button>
            </CardFooter>
          ) : answer.dismissed ? null : !saveable ? (
            <CardFooter>
              <p className="text-xs text-muted-foreground">{READ_ONLY_SOURCE}</p>
            </CardFooter>
          ) : !sameCard ? (
            <CardFooter>
              <p className="text-xs text-muted-foreground">{CARD_CHANGED}</p>
            </CardFooter>
          ) : (
            <CardFooter className="flex flex-col items-stretch gap-2">
              <p className="text-xs text-muted-foreground">
                {saveQuestion(tracks.length)}
              </p>
              <div className="flex flex-wrap gap-2">
                <Button
                  type="button"
                  variant="solid"
                  size="sm"
                  onClick={() => onSave(message)}
                >
                  Save playlist
                </Button>
                <Button
                  type="button"
                  variant="ghost"
                  size="sm"
                  onClick={() => onDismiss(message)}
                >
                  Not now
                </Button>
              </div>
            </CardFooter>
          )}
        </Card>
      ) : null}
    </div>
  );
}

function Empty({
  canAsk,
  onAsk,
}: {
  canAsk: boolean;
  onAsk: (sentence: string) => void;
}) {
  return (
    <div className="flex flex-col items-center gap-3 text-center">
      <span
        className="grid size-10 place-items-center rounded-full border border-foreground/30 text-primary"
        aria-hidden="true"
      >
        <Sparkles className="size-5 stroke-[1.25]" />
      </span>
      <h2 className="text-xl font-light tracking-tight">What should we listen to?</h2>
      <p className="max-w-md text-sm text-muted-foreground">
        {canAsk
          ? 'A mood, a tempo, a name or a key — in your own words. The model runs on this machine and never sees a track.'
          : 'The assistant is not ready on this machine. The Filters sheet still searches the library.'}
      </p>
      {canAsk ? (
        <div className="flex flex-wrap justify-center gap-2">
          {OPENERS.map((opener) => (
            <Button
              key={opener}
              type="button"
              variant="outline"
              size="sm"
              onClick={() => onAsk(opener)}
            >
              {opener}
            </Button>
          ))}
        </div>
      ) : null}
    </div>
  );
}

function Pending({ phase }: { phase: 'reading' | 'searching' }) {
  return (
    <p
      id="ask-pending"
      role="status"
      className="flex scroll-mt-4 items-center gap-2 text-sm text-muted-foreground"
    >
      <Spinner aria-hidden="true" className="size-3.5" />
      {phase === 'reading' ? 'Reading that…' : 'Searching…'}
    </p>
  );
}

export function AskArea() {
  const scope = useCardScope();
  const deviceView = useDeviceView();
  const [assistant, setAssistant] = useState<AssistantState | null>(null);
  const [downloadPercent, setDownloadPercent] = useState<number | null>(null);
  const [downloadError, setDownloadError] = useState<string | null>(null);

  const [messages, setMessages] = useState<ChatMessage[]>([]);
  const [sentence, setSentence] = useState('');
  const [phase, setPhase] = useState<'reading' | 'searching' | null>(null);
  const [filters, setFilters] = useState<{ open: boolean; initial: SearchArgs }>({
    open: false,
    initial: {},
  });

  /* Two guards on one ask: React's state is a render behind, and a bounce on Enter must not
   * spend a second model call. The ref is the lock; `phase` is what the page shows. */
  const busy = useRef(false);
  const lastProfile = useRef<string | null>(null);

  const pending = phase !== null;

  const append = (message: ChatMessage) =>
    setMessages((current) => [...current, message]);

  const refresh = async () => {
    setDownloadError(null);
    try {
      setAssistant(await assistantState());
    } catch (error) {
      setAssistant('unavailable');
      setDownloadError(assistantFailure(error));
    }
  };

  useEffect(
    () => () => {
      void destroyBases();
    },
    [],
  );

  useEffect(() => {
    void refresh();
  }, []);

  /* A new turn comes into view at its own start, so the answer is read from the top even
   * when its list runs past the thread. While the model or the library works, the spinner
   * is what is brought into view. Instant, not animated: reading is the task. */
  const lastId = messages[messages.length - 1]?.id;
  useEffect(() => {
    const target = phase
      ? document.getElementById('ask-pending')
      : lastId
        ? document.getElementById(`ask-${lastId}`)
        : null;
    target?.scrollIntoView({ block: 'nearest' });
  }, [lastId, phase]);

  const setup = async () => {
    setDownloadError(null);
    setDownloadPercent(0);
    try {
      await downloadModel((fraction) => setDownloadPercent(Math.round(fraction * 100)));
    } catch (error) {
      setDownloadError(assistantFailure(error));
    } finally {
      setDownloadPercent(null);
    }
    await refresh();
  };

  const openFilters = (initial: SearchArgs) => setFilters({ open: true, initial });

  const lastAnswer = [...messages].reverse().find((message) => message.answer)?.answer;
  const seed: SearchArgs = lastAnswer?.intent ?? {};

  async function ask(asked: string, echo = true) {
    const text = asked.trim();
    if (!text || busy.current) return;
    busy.current = true;
    if (echo) append({ id: newId(), role: 'user', text });
    setSentence('');
    setPhase('reading');
    const retry: Retry = { kind: 'ask', sentence: text };
    try {
      const source = await resolveSource(scope, deviceView);
      if (!source) {
        append(errorMessage('No library to search: connect the player or open a card first.', retry));
        return;
      }
      /* The base session belongs to the prompt, which belongs to the source profile. A new
       * source closes the old base; the next requestIntent creates the new one. */
      const profileKey = JSON.stringify(source.profile);
      if (lastProfile.current !== null && lastProfile.current !== profileKey) {
        await destroyBases();
      }
      lastProfile.current = profileKey;
      /* ADR 0084's thirty-second ceiling: a prompt Chrome never settles is abandoned, not a
       * spinner that never ends. The retry below is the one the area already offers. */
      const parsed = await requestIntent(
        text,
        source.profile,
        undefined,
        AbortSignal.timeout(30_000),
      );
      if ('error' in parsed) {
        append(errorMessage(parsed.error, retry));
        return;
      }
      setPhase('searching');
      const found = await find(source, parsed, {
        purpose: purposeFilters(text, source.profile),
      });
      append(answerMessage(source, found, text));
    } catch (error) {
      append(errorMessage(describeSearchError(error), retry));
    } finally {
      busy.current = false;
      setPhase(null);
    }
  }

  async function runFilters(intent: SearchArgs) {
    if (busy.current) return;
    busy.current = true;
    setFilters({ open: false, initial: {} });
    setPhase('searching');
    const retry: Retry = { kind: 'search', intent };
    try {
      const source = await resolveSource(scope, deviceView);
      if (!source) {
        append(errorMessage('No library to search: connect the player or open a card first.', retry));
        return;
      }
      const found = await find(source, intent);
      append(answerMessage(source, found, null));
    } catch (error) {
      append(errorMessage(describeSearchError(error), retry));
    } finally {
      busy.current = false;
      setPhase(null);
    }
  }

  function retry(message: ChatMessage) {
    if (!message.retry) return;
    if (message.retry.kind === 'ask') void ask(message.retry.sentence, false);
    else void runFilters(message.retry.intent);
  }

  function save(message: ChatMessage) {
    const answer = message.answer;
    if (!answer || answer.draftId || answer.source.draftScope === null) return;
    if (answer.source.draftScope !== scope) return;
    const draft = createDraft(
      answer.name,
      answer.search.tracks.map((track) => track.path),
    );
    setMessages((current) =>
      current.map((each) =>
        each.id === message.id && each.answer
          ? { ...each, answer: { ...each.answer, draftId: draft.id } }
          : each,
      ),
    );
  }

  function dismiss(message: ChatMessage) {
    setMessages((current) =>
      current.map((each) =>
        each.id === message.id && each.answer
          ? { ...each, answer: { ...each.answer, dismissed: true } }
          : each,
      ),
    );
  }

  function clear() {
    if (busy.current) return;
    setMessages([]);
    setFilters({ open: false, initial: {} });
  }

  const submit = (event: FormEvent) => {
    event.preventDefault();
    void ask(sentence);
  };

  const setupNote =
    downloadPercent !== null
      ? `Downloading Chrome’s built-in model… ${downloadPercent}%`
      : assistant !== null && assistant !== 'available'
        ? assistantSentence(assistant)
        : '';

  return (
    <div className="flex h-full min-h-0 flex-col">
      <header className="flex shrink-0 items-center gap-2 border-b px-4 py-2.5 sm:px-6">
        <h1 className="min-w-0 flex-1 text-lg font-light tracking-tight">Ask</h1>
        {messages.length > 0 ? (
          <Button
            type="button"
            variant="ghost"
            size="sm"
            onClick={clear}
            disabled={pending}
          >
            New ask
          </Button>
        ) : null}
      </header>

      <div
        role="log"
        aria-live="polite"
        aria-relevant="additions"
        className="min-h-0 flex-1 overflow-y-auto"
      >
        <div
          className={cn(
            'mx-auto flex w-full max-w-3xl flex-col gap-6 px-4 py-6 sm:px-6',
            messages.length === 0 && 'min-h-full justify-center',
          )}
        >
          {messages.length === 0 && !pending ? (
            <Empty
              canAsk={assistant === 'available'}
              onAsk={(opener) => void ask(opener)}
            />
          ) : null}

          {messages.map((message) =>
            message.role === 'user' ? (
              <div
                key={message.id}
                id={`ask-${message.id}`}
                className="flex scroll-mt-4 justify-end"
              >
                <p className="max-w-[85%] rounded-panel border border-foreground/25 bg-foreground/10 px-3 py-2 text-sm break-words text-foreground">
                  <span className="sr-only">You said: </span>
                  {message.text}
                </p>
              </div>
            ) : message.answer ? (
              <div key={message.id} id={`ask-${message.id}`} className="scroll-mt-4">
                <AnswerBlock
                  message={message}
                  scope={scope}
                  onFilters={openFilters}
                  onSave={save}
                  onDismiss={dismiss}
                  onOpenDraft={(id) => navigate(routeHref({ name: 'playlist', id }))}
                />
              </div>
            ) : (
              <div key={message.id} id={`ask-${message.id}`} className="scroll-mt-4">
              <Alert>
                <TriangleAlert aria-hidden="true" />
                <AlertDescription>
                  <p>{message.text}</p>
                  {message.retry || message.tone === 'error' ? (
                    <div className="mt-2 flex flex-wrap gap-1.5">
                      {message.retry ? (
                        <Button
                          type="button"
                          variant="outline"
                          size="sm"
                          onClick={() => retry(message)}
                        >
                          Try again
                        </Button>
                      ) : null}
                      <Button
                        type="button"
                        variant="ghost"
                        size="sm"
                        onClick={() =>
                          openFilters(
                            message.retry?.kind === 'search' ? message.retry.intent : {},
                          )
                        }
                      >
                        Filters
                      </Button>
                    </div>
                  ) : null}
                </AlertDescription>
              </Alert>
              </div>
            ),
          )}

          {phase ? <Pending phase={phase} /> : null}
        </div>
      </div>

      <div className="shrink-0 border-t">
        <div className="mx-auto flex w-full max-w-3xl flex-col gap-2 px-4 py-3 sm:px-6">
          {assistant === null ? (
            <p
              role="status"
              className="flex items-center gap-2 text-sm text-muted-foreground"
            >
              <Spinner aria-hidden="true" className="size-3.5" />
              Checking Chrome’s built-in model…
            </p>
          ) : assistant === 'available' ? (
            <>
              <form onSubmit={submit} className="flex items-center gap-2">
                <label htmlFor="ask-sentence" className="sr-only">
                  Ask
                </label>
                <Input
                  id="ask-sentence"
                  value={sentence}
                  onChange={(event) => setSentence(event.target.value)}
                  placeholder="A mood, a tempo, a name, a key…"
                  maxLength={280}
                  autoComplete="off"
                  disabled={pending}
                />
                <Button
                  type="submit"
                  variant="solid"
                  size="icon"
                  aria-label="Ask"
                  disabled={pending || !sentence.trim()}
                >
                  {pending ? (
                    <Spinner aria-hidden="true" />
                  ) : (
                    <ArrowUp aria-hidden="true" />
                  )}
                </Button>
              </form>
              <div className="flex items-center justify-between gap-2">
                <Button
                  type="button"
                  variant="ghost"
                  size="xs"
                  onClick={() => openFilters(seed)}
                >
                  Filters
                </Button>
                <span className="text-xs text-muted-foreground">
                  The model never sees a track.
                </span>
              </div>
            </>
          ) : (
            <div className="flex flex-col gap-2">
              <p className="text-sm text-muted-foreground">{setupNote}</p>
              {downloadPercent !== null ? (
                <Progress value={downloadPercent} aria-label="Model download" />
              ) : null}
              {downloadError ? (
                <p className="text-sm text-destructive" role="alert">
                  {downloadError}
                </p>
              ) : null}
              <div className="flex flex-wrap gap-2">
                {assistant === 'downloadable' ? (
                  <Button
                    type="button"
                    variant="solid"
                    size="sm"
                    onClick={() => void setup()}
                    disabled={downloadPercent !== null}
                  >
                    Set up the assistant
                  </Button>
                ) : null}
                {assistant === 'downloading' ? (
                  <Button
                    type="button"
                    variant="outline"
                    size="sm"
                    onClick={() => void refresh()}
                    disabled={downloadPercent !== null}
                  >
                    Check again
                  </Button>
                ) : null}
                <Button
                  type="button"
                  variant="outline"
                  size="sm"
                  onClick={() => openFilters(seed)}
                >
                  Search by filters
                </Button>
              </div>
            </div>
          )}
        </div>
      </div>

      {filters.open ? (
        <AskFilters
          initial={filters.initial}
          onClose={() => setFilters({ open: false, initial: {} })}
          onRun={(intent) => void runFilters(intent)}
        />
      ) : null}
    </div>
  );
}
