import { useEffect, useMemo, useRef, useState, type FormEvent } from 'react';
import { ListMusic, Search, SlidersHorizontal, Sparkles, TriangleAlert } from 'lucide-react';

import { Alert, AlertDescription } from '@/components/reui/alert';
import { Button } from '@/components/ui/button';
import { Card, CardContent } from '@/components/ui/card';
import { Input } from '@/components/ui/input';
import { Progress } from '@/components/ui/progress';
import { Spinner } from '@/components/ui/spinner';
import {
  Empty,
  EmptyContent,
  EmptyDescription,
  EmptyHeader,
  EmptyMedia,
  EmptyTitle,
} from '@/components/ui/empty';
import { Skeleton } from '@/components/ui/skeleton';
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
import { keepFolderTree, type Folder } from '../../media/folders';
import { resolveSource, type LibrarySource } from '../../library/source';
import { newId } from '../../lib/id';
import { purposeFilters, requestIntent, relaxRanges, spreadAnswer } from '../../playlists/ask';
import {
  addedNote,
  addQuestion,
  answerText,
  CARD_CHANGED,
  filterWords,
  nameFor,
  OPENERS,
  READ_ONLY_SOURCE,
  SAVED_NOTE,
  sourceText,
  type ChatMessage,
  type Retry,
} from '../../playlists/chat';
import { createDraft } from '../../playlists/drafts';
import { plural } from '../format';
import { FileTree } from '../file-tree';
import { navigate, routeHref } from '../router';
import { AskFilters } from './AskFilters';

/**
 * **The sources column**: the card's folders, and Ask with them.
 *
 * The tree is the column and stays one: a sentence types into the composer at its foot, and the
 * answer is drawn *in the tree* — the card's own folders pruned to the rows the answer found, so
 * a folder can still be dragged (k7-look.md K21) — rather than as a second list. **Files** puts
 * the whole card back, and the answer's offer stays under the tree either way.
 *
 * The model is the **Built-in model** (ADR 0084): it turns one sentence into an **Intent** and
 * nothing else. What the column says back is the App's — the count, the reading, the source and
 * the offer to save are computed from the filters that ran and the search that answered, so a
 * sentence here can never describe a track that does not exist (ADR 0050). **Filters** opens the
 * exact arguments for the times it read the sentence wrong (ADR 0086).
 *
 * One session per ask, so each ask stands alone: the transcript is this visit's, not context the
 * model carries, and the last turn is the one the column shows.
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

function NoSources({ reachable }: { reachable: boolean }) {
  return (
    <Empty className="border">
      <EmptyHeader>
        <EmptyMedia variant="icon">
          <ListMusic aria-hidden="true" />
        </EmptyMedia>
        <EmptyTitle>No card to browse</EmptyTitle>
        <EmptyDescription>
          {reachable
            ? 'Measure a card in this browser and its folders appear here, ready to drag into the list.'
            : 'The device is not answering, and this browser has not measured a card. Put the card in the computer and measure it once.'}
        </EmptyDescription>
      </EmptyHeader>
      <EmptyContent>
        <Button size="sm" onClick={() => navigate(routeHref({ name: 'prepare' }))}>
          Open Prepare
        </Button>
      </EmptyContent>
    </Empty>
  );
}

function SourceLoading() {
  return (
    <div className="flex flex-col gap-2">
      {Array.from({ length: 6 }, (_, index) => (
        <Skeleton key={index} className="h-7 w-full" />
      ))}
    </div>
  );
}

function Pending({ phase }: { phase: 'reading' | 'searching' }) {
  return (
    <p role="status" className="flex items-center gap-2 text-xs text-muted-foreground">
      <Spinner aria-hidden="true" className="size-3.5" />
      {phase === 'reading' ? 'Reading that…' : 'Searching…'}
    </p>
  );
}

/** The answer's own words and its offer, over the rows it put in the tree. */
function AnswerBlock({
  message,
  scope,
  listName,
  onFilters,
  onAdd,
  onSave,
  onDismiss,
  onOpenDraft,
}: {
  message: ChatMessage;
  scope: string;
  /** The list the editor has open beside the column. */
  listName: string;
  onFilters: (intent: SearchArgs) => void;
  onAdd: (message: ChatMessage) => void;
  onSave: (message: ChatMessage) => void;
  onDismiss: (message: ChatMessage) => void;
  onOpenDraft: (id: string) => void;
}) {
  const answer = message.answer;
  if (!answer) return null;

  const count = answer.search.tracks.length;
  const saveable = answer.source.draftScope !== null;
  const sameCard = answer.source.draftScope === scope;

  return (
    <Card size="sm">
      <CardContent className="flex flex-col gap-2">
        {/* The answer is announced as it lands: the thread it used to sit in is not a live
            region any more, and its rows are drawn in the tree without saying anything. */}
        <p role="status" className="text-sm break-words">
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

        {count === 0 ? (
          <p className="text-xs text-muted-foreground">
            Every filter has to match at once, and a range only sees analysed tracks.
          </p>
        ) : answer.draftId ? (
          <div className="flex flex-wrap items-center gap-2">
            <p className="text-xs text-muted-foreground">{SAVED_NOTE}</p>
            <Button
              type="button"
              variant="solid"
              size="sm"
              onClick={() => onOpenDraft(answer.draftId!)}
            >
              Open playlist
            </Button>
          </div>
        ) : answer.added ? (
          <p className="text-xs text-muted-foreground">{addedNote(count, listName)}</p>
        ) : answer.dismissed ? null : !saveable ? (
          <p className="text-xs text-muted-foreground">{READ_ONLY_SOURCE}</p>
        ) : !sameCard ? (
          <p className="text-xs text-muted-foreground">{CARD_CHANGED}</p>
        ) : (
          <>
            <p className="text-xs text-muted-foreground">
              {addQuestion(count, listName)}
            </p>
            <div className="flex flex-wrap gap-2">
              <Button
                type="button"
                variant="solid"
                size="sm"
                onClick={() => onAdd(message)}
              >
                Add {plural(count, 'track')}
              </Button>
              <Button
                type="button"
                variant="outline"
                size="sm"
                onClick={() => onSave(message)}
              >
                Save as a new list
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
          </>
        )}
      </CardContent>
    </Card>
  );
}

export function SourcesColumn({
  tree,
  loading,
  reachable,
  listName,
  onAdd,
}: {
  /** The card's own tree, or null while it is read or when there is nothing to browse. */
  tree: Folder | null;
  loading: boolean;
  reachable: boolean;
  /** The list the editor has open: where an answer's rows go. */
  listName: string;
  onAdd: (rows: readonly string[]) => void;
}) {
  const scope = useCardScope();
  const deviceView = useDeviceView();
  const [assistant, setAssistant] = useState<AssistantState | null>(null);
  const [downloadPercent, setDownloadPercent] = useState<number | null>(null);
  const [downloadError, setDownloadError] = useState<string | null>(null);

  const [messages, setMessages] = useState<ChatMessage[]>([]);
  const [sentence, setSentence] = useState('');
  const [phase, setPhase] = useState<'reading' | 'searching' | null>(null);
  const [query, setQuery] = useState('');
  /* What the tree is showing: the whole card, or the answer's rows in it. An ask switches to
   * its own answer; **Files** gives the card back and keeps the answer's offer below. */
  const [showingAnswer, setShowingAnswer] = useState(false);
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

  /* The last turn is the one the column shows: the answer, or the error that replaced it. */
  const reply = useMemo(
    () => [...messages].reverse().find((message) => message.role === 'assistant') ?? null,
    [messages],
  );
  const answer = reply?.answer ?? null;

  /* An answer is what the tree shows, the moment it lands. A failure leaves the card's files
   * where they are: there is nothing to prune them to. The id is the whole dependency: adding,
   * saving or dismissing rewords the same answer, and must not pull the tree back to it after
   * the person pressed Files. */
  const answeredId = reply?.answer ? reply.id : null;
  const replyId = reply?.id;
  useEffect(() => {
    if (answeredId) setShowingAnswer(true);
  }, [answeredId]);

  /* The card's own folders, pruned to the answer's rows. It keeps the tree's order and its
   * folder rows, so dragging a folder out of an answer is the same act as dragging one out of
   * the card (k7-look.md K21). */
  const answerTree = useMemo(
    () =>
      answer && tree
        ? keepFolderTree(
            tree,
            answer.search.tracks.map((track) => track.path),
          )
        : null,
    [answer, tree],
  );
  const showing = showingAnswer && answerTree !== null;
  const hasSources = tree !== null && (tree.folders.length > 0 || tree.tracks.length > 0);

  const showFiles = () => {
    setShowingAnswer(false);
    setQuery('');
  };

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

  /** The primary action: the App applies the Intent to the list beside the column (ADR 0093). */
  function add(message: ChatMessage) {
    const found = message.answer;
    if (!found || found.added || found.draftId) return;
    if (found.source.draftScope === null || found.source.draftScope !== scope) return;
    onAdd(found.search.tracks.map((track) => track.path));
    setMessages((current) =>
      current.map((each) =>
        each.id === message.id && each.answer
          ? { ...each, answer: { ...each.answer, added: true } }
          : each,
      ),
    );
  }

  function save(message: ChatMessage) {
    const found = message.answer;
    if (!found || found.draftId || found.source.draftScope === null) return;
    if (found.source.draftScope !== scope) return;
    const draft = createDraft(
      found.name,
      found.search.tracks.map((track) => track.path),
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
    setQuery('');
    setShowingAnswer(false);
    setFilters({ open: false, initial: {} });
  }

  /** The magnifier: the same words, filtering the card's tree instead of asking the model. */
  function searchFiles(text: string) {
    setQuery(text.trim());
    setShowingAnswer(false);
  }

  function clearSearch() {
    setQuery('');
    setSentence('');
  }

  /* Enter does what the browser can do: Ask where there is a model, and the direct search
   * where there is not. */
  const submit = (event: FormEvent) => {
    event.preventDefault();
    if (assistant === 'available') void ask(sentence);
    else searchFiles(sentence);
  };

  const setupNote =
    downloadPercent !== null
      ? `Downloading Chrome’s built-in model… ${downloadPercent}%`
      : assistant !== null && assistant !== 'available'
        ? assistantSentence(assistant)
        : '';

  return (
    <div className="flex min-h-0 flex-1 flex-col gap-2">
      {/* What the tree is: the card's folders, or the answer's rows inside them. */}
      <div className="flex flex-wrap items-end justify-between gap-2">
        <div className="flex min-w-0 flex-col gap-0.5">
          <p className="text-sm font-light">{showing ? 'Ask found' : 'Files'}</p>
          {showing && answer ? (
            <p className="text-xs text-muted-foreground tabular-nums">
              {plural(answer.search.tracks.length, 'track')} · the card’s folders, pruned to
              them
            </p>
          ) : query ? (
            <p className="text-xs text-muted-foreground">
              Searching the card for “{query}”
            </p>
          ) : null}
        </div>
        {showing ? (
          <Button
            type="button"
            variant="ghost"
            size="sm"
            aria-label="Show the card's files"
            onClick={showFiles}
          >
            Files
          </Button>
        ) : query ? (
          <Button
            type="button"
            variant="ghost"
            size="sm"
            aria-label="Clear the search"
            onClick={clearSearch}
          >
            Clear
          </Button>
        ) : null}
      </div>


      <div className="flex min-h-0 flex-1 flex-col gap-3 lg:overflow-auto">
        {reply?.answer ? (
          <AnswerBlock
            message={reply}
            scope={scope}
            listName={listName}
            onFilters={openFilters}
            onAdd={add}
            onSave={save}
            onDismiss={dismiss}
            onOpenDraft={(id) => navigate(routeHref({ name: 'playlist', id }))}
          />
        ) : null}

        {loading ? (
          <SourceLoading />
        ) : !hasSources ? (
          <NoSources reachable={reachable} />
        ) : (
          <FileTree
            key={`${showing ? `answer\u0000${replyId ?? ''}` : `sources\u0000${query}`}`}
            tree={showing ? answerTree! : tree!}
            query={showing ? '' : query}
            path=""
            action="add"
            draggableSources
            expandAll={showing}
            showControls={!showing}
            onActivate={(track) => onAdd([track.path])}
          />
        )}
      </div>

      <div className="flex shrink-0 flex-col gap-2 border-t pt-3">
        {reply && !reply.answer ? (
          <Alert>
            <TriangleAlert aria-hidden="true" />
            <AlertDescription>
              <p>{reply.text}</p>
              {reply.retry ? (
                <div className="mt-2 flex flex-wrap gap-1.5">
                  <Button
                    type="button"
                    variant="outline"
                    size="sm"
                    onClick={() => retry(reply)}
                  >
                    Try again
                  </Button>
                  <Button
                    type="button"
                    variant="ghost"
                    size="sm"
                    onClick={() =>
                      openFilters(reply.retry?.kind === 'search' ? reply.retry.intent : {})
                    }
                  >
                    Filters
                  </Button>
                </div>
              ) : null}
            </AlertDescription>
          </Alert>
        ) : null}

        {phase ? <Pending phase={phase} /> : null}

        {/* One bar for both jobs: the same words either filter the card's tree or go to the
            model. Filters and the magnifier are the two searches and are always there; Ask is
            the primary and only present when this browser has the built-in model. */}
        <form onSubmit={submit} className="flex items-center gap-2">
          <label htmlFor="source-bar" className="sr-only">
            Search the sources, or ask
          </label>
          <Input
            id="source-bar"
            value={sentence}
            onChange={(event) => setSentence(event.target.value)}
            placeholder="Search or ask — a mood, a tempo, a name…"
            maxLength={280}
            autoComplete="off"
            disabled={pending}
          />
          <Button
            type="button"
            variant="outline"
            size="icon"
            aria-label="Filters"
            title="Filters"
            onClick={() => openFilters(seed)}
          >
            <SlidersHorizontal aria-hidden="true" />
          </Button>
          <Button
            type="button"
            variant="outline"
            size="icon"
            aria-label="Search the sources"
            title="Search the sources"
            onClick={() => searchFiles(sentence)}
            disabled={pending || !hasSources}
          >
            <Search aria-hidden="true" />
          </Button>
          {assistant === 'available' ? (
            <Button
              type="submit"
              variant="solid"
              size="icon"
              aria-label="Ask"
              title="Ask"
              disabled={pending || !sentence.trim()}
            >
              {pending ? <Spinner aria-hidden="true" /> : <Sparkles aria-hidden="true" />}
            </Button>
          ) : null}
        </form>

        {assistant === 'available' && messages.length === 0 ? (
          <div className="flex flex-wrap gap-1.5">
            {OPENERS.map((opener) => (
              <Button
                key={opener}
                type="button"
                variant="outline"
                size="xs"
                disabled={pending}
                onClick={() => void ask(opener)}
              >
                {opener}
              </Button>
            ))}
          </div>
        ) : null}

        {assistant === null ? (
          <p role="status" className="flex items-center gap-2 text-xs text-muted-foreground">
            <Spinner aria-hidden="true" className="size-3.5" />
            Checking Chrome’s built-in model…
          </p>
        ) : assistant === 'available' ? (
          <div className="flex flex-wrap items-center justify-between gap-2 text-xs text-muted-foreground">
            {reply ? (
              <Button
                type="button"
                variant="ghost"
                size="xs"
                onClick={clear}
                disabled={pending}
              >
                New ask
              </Button>
            ) : (
              <span />
            )}
          </div>
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
            <div className="flex flex-wrap items-center gap-2">
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
            </div>
          </div>
        )}
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
