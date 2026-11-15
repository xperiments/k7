/**
 * The **Built-in model**'s edge in one place: whether Chrome's model is ready, the one call that
 * asks it, and the sentence a failure is worth.
 *
 * The Prompt API is Chrome's, desktop-only, and its state changes under the page: a machine below
 * the floor, a model not downloaded yet, a download in flight, a model Chrome removed because the
 * disk filled. Ask decides what to show; this module only says what is true.
 */

/** What Ask needs to know before it shows a prompt. `absent` is this App's own word. */
export type AssistantState =
  | 'absent'
  | 'unavailable'
  | 'downloadable'
  | 'downloading'
  | 'available';

/** Chrome's four answers, narrowed. An unknown answer is treated as "cannot run". */
export function stateFrom(availability: string): AssistantState {
  switch (availability) {
    case 'available':
    case 'downloadable':
    case 'downloading':
    case 'unavailable':
      return availability;
    default:
      return 'unavailable';
  }
}

/** The one sentence Ask shows when the model is not ready. `available` has none. */
export function assistantSentence(state: AssistantState): string {
  switch (state) {
    case 'absent':
      return 'Ask needs Chrome 148 or later on a computer, with its built-in model. This Chrome does not have it.';
    case 'unavailable':
      return 'This machine cannot run Chrome’s built-in model. It needs a 4 GB graphics card, or 16 GB of memory and four cores, plus 22 GB of free disk, and built-in AI turned on in Chrome.';
    case 'downloadable':
      return 'Chrome can download its built-in model now: a few gigabytes, with 22 GB of free disk needed. After that, Ask needs no key and no network.';
    case 'downloading':
      return 'Chrome is downloading its built-in model. This page can wait, or come back when it is done.';
    case 'available':
      return '';
  }
}

/**
 * One sentence for a model request that threw. A request that fails is not one failure: a
 * sentence the context cannot hold, a model that cannot run the ask, and a download that died
 * are different things the person can act on differently.
 */
export function assistantFailure(error: unknown): string {
  const name = error instanceof Error ? error.name : '';
  const message = error instanceof Error ? error.message : String(error);

  if (name === 'NotSupportedError') {
    return 'This Chrome cannot run the built-in model for that sentence.';
  }
  if (name === 'QuotaExceededError') {
    return 'The model ran out of room for that sentence. Try a shorter one.';
  }
  if (name === 'AbortError') {
    return 'The model stopped before it answered.';
  }
  if (/download|network|fetch/i.test(message)) {
    return 'Chrome could not finish downloading the model. Check the connection and try again.';
  }
  return 'The model did not answer. The Filters sheet still works on its own.';
}

function requireModel(): LanguageModelStatic {
  if (typeof LanguageModel === 'undefined') {
    throw new Error('The built-in model is not available in this browser.');
  }
  return LanguageModel;
}

/** What Chrome says about the model right now. */
export async function assistantState(): Promise<AssistantState> {
  if (typeof LanguageModel === 'undefined') return 'absent';
  return stateFrom(await LanguageModel.availability());
}

/**
 * Download the model, watching it. The bytes are Chrome's: this can start the download, and it
 * cannot cancel or remove it. A session has to exist to trigger the download, so one is created
 * and destroyed; the model stays.
 */
export async function downloadModel(onProgress?: (fraction: number) => void): Promise<void> {
  const model = requireModel();
  const session = await model.create({
    monitor(monitor) {
      monitor.addEventListener('downloadprogress', (event) => {
        onProgress?.(event.loaded);
      });
    },
  });
  session.destroy();
}

export interface AskModelOptions {
  /** The system prompt: the task and the shape, and nothing about the library. */
  readonly system: string;
  /** The JSON Schema the answer must satisfy. */
  readonly schema: object;
  /** The person's sentence. */
  readonly sentence: string;
  /**
   * Few-shot turns after the system prompt, as [user, assistant] pairs. They teach the shape
   * by example instead of describing it in the system message.
   */
  readonly examples?: readonly (readonly [string, string])[];
  /** Abort the prompt (a harness ceiling); the App passes none. */
  readonly signal?: AbortSignal;
}

type InitialPrompts = { role: 'system' | 'user' | 'assistant'; content: string }[];

/**
 * The base sessions, one per system prompt and example set. A base is created once, never
 * prompted, and cloned per ask; the clone carries the initial prompt and is destroyed after its
 * one answer, so no ask's context reaches another. The App closes them when the profile changes
 * or the Ask area closes; the evaluation harness closes them after a run.
 */
const bases = new Map<string, Promise<LanguageModelSession>>();

function baseFor(initialPrompts: InitialPrompts): Promise<LanguageModelSession> {
  const key = JSON.stringify(initialPrompts);
  const cached = bases.get(key);
  if (cached) return cached;
  const base = requireModel().create({ initialPrompts });
  bases.set(key, base);
  base.catch(() => bases.delete(key));
  return base;
}

/** Close every base session; call this when the prompt changes or the area holding it closes. */
export async function destroyBases(): Promise<void> {
  const held = [...bases.values()];
  bases.clear();
  await Promise.all(
    held.map(async (base) => {
      try {
        (await base).destroy();
      } catch {
        /* the model is gone; there is nothing left to close */
      }
    }),
  );
}

/**
 * Ask the built-in model once. The base session holds the system prompt and the examples; each
 * ask clones it, prompts the clone once and destroys the clone. The base is never prompted, so
 * no answer's context carries to the next sentence.
 *
 * The schema is passed as the constraint but not echoed into the input: the prompt's rules and
 * turns teach the shape, and echoing it made the model over-fill (APP-42's session screen).
 */
export async function askModel({
  system,
  schema,
  sentence,
  examples,
  signal,
}: AskModelOptions): Promise<string> {
  const initialPrompts: InitialPrompts = [{ role: 'system', content: system }];
  for (const [asked, answered] of examples ?? []) {
    initialPrompts.push(
      { role: 'user', content: asked },
      { role: 'assistant', content: answered },
    );
  }
  const base = await baseFor(initialPrompts);
  const session = await base.clone();
  try {
    return await session.prompt(sentence, {
      responseConstraint: schema,
      /* The output is still constrained; the schema is just not echoed into the model's input.
       * The prompt's rules and turns carry the shape. The APP-42 screen measured 19 → 37 rows
       * passing and 1850 → 1130 ms without the echo. */
      omitResponseConstraintInput: true,
      ...(signal ? { signal } : {}),
    });
  } finally {
    session.destroy();
  }
}
