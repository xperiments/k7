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

/**
 * The Sampling Parameters origin trial's enum. The stable web IDL exposes no `temperature` or
 * `topK`: a web page's numeric values are ignored (Chrome's legacy extension-only parameters),
 * so this enum is the only sampling control a page can get.
 */
type LanguageModelSamplingMode = 'balanced' | 'slightly-creative' | 'creative' | 'most-creative';

interface LanguageModelCreateOptions {
  initialPrompts?: { role: 'system' | 'user' | 'assistant'; content: string }[];
  monitor?: (monitor: LanguageModelMonitor) => void;
  /** The origin trial's sampling mode; a browser without the trial ignores it. */
  samplingMode?: LanguageModelSamplingMode;
}

interface LanguageModelPromptOptions {
  /** A JSON Schema the answer must satisfy. */
  responseConstraint?: object;
  /** Leave the constraint out of the model's input; the output is still constrained. */
  omitResponseConstraintInput?: boolean;
  signal?: AbortSignal;
}

interface LanguageModelSession {
  /** The mode this session was created with, when the origin trial provides it. */
  readonly samplingMode?: LanguageModelSamplingMode;
  prompt(input: string, options?: LanguageModelPromptOptions): Promise<string>;
  /** Fork this session, preserving its initial prompt and the conversation context. */
  clone(options?: { signal?: AbortSignal }): Promise<LanguageModelSession>;
  destroy(): void;
}

interface LanguageModelStatic {
  availability(): Promise<'unavailable' | 'downloadable' | 'downloading' | 'available'>;
  create(options?: LanguageModelCreateOptions): Promise<LanguageModelSession>;
}

declare const LanguageModel: LanguageModelStatic;
