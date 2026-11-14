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
}

/**
 * Ask the built-in model once. One session per ask: the system prompt and the examples go in,
 * one answer comes out, and the session is destroyed so no context carries to the next sentence.
 */
export async function askModel({
  system,
  schema,
  sentence,
  examples,
}: AskModelOptions): Promise<string> {
  const model = requireModel();
  const initialPrompts: { role: 'system' | 'user' | 'assistant'; content: string }[] = [
    { role: 'system', content: system },
  ];
  for (const [asked, answered] of examples ?? []) {
    initialPrompts.push(
      { role: 'user', content: asked },
      { role: 'assistant', content: answered },
    );
  }
  const session = await model.create({ initialPrompts });
  try {
    return await session.prompt(sentence, { responseConstraint: schema });
  } finally {
    session.destroy();
  }
}
