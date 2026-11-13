/**
 * The slice of Chrome's Prompt API that K7 uses.
 *
 * The API is a global on desktop Chrome; a browser without it leaves `LanguageModel` undefined,
 * which is how Ask knows the model is absent. Everything else — the hardware floor, the download,
 * the availability — is Chrome's answer, not this page's.
 */
interface LanguageModelDownloadProgressEvent extends Event {
  /** 0 to 1. */
  readonly loaded: number;
}

interface LanguageModelMonitor {
  addEventListener(
    type: 'downloadprogress',
    listener: (event: LanguageModelDownloadProgressEvent) => void,
  ): void;
}

interface LanguageModelCreateOptions {
  initialPrompts?: { role: 'system' | 'user' | 'assistant'; content: string }[];
  monitor?: (monitor: LanguageModelMonitor) => void;
}

interface LanguageModelPromptOptions {
  /** A JSON Schema the answer must satisfy. */
  responseConstraint?: object;
  signal?: AbortSignal;
}

interface LanguageModelSession {
  prompt(input: string, options?: LanguageModelPromptOptions): Promise<string>;
  destroy(): void;
}

interface LanguageModelStatic {
  availability(): Promise<'unavailable' | 'downloadable' | 'downloading' | 'available'>;
  create(options?: LanguageModelCreateOptions): Promise<LanguageModelSession>;
}

declare const LanguageModel: LanguageModelStatic;
