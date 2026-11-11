/**
 * The flash's failures, named.
 *
 * A flash that says only "error" is a flash that leaves a person holding a board and no next
 * step. Every cause here is something the page can say in a sentence and something the
 * person can act on, and each one is what the tests pin.
 */
export type FailureCause =
  | 'cancelled'
  | 'no-port'
  | 'port-busy'
  | 'permission'
  | 'wrong-chip'
  | 'wrong-flash'
  | 'boot-mode'
  | 'download'
  | 'hash'
  | 'write'
  | 'unknown';

export class FlashError extends Error {
  readonly cause: FailureCause;

  constructor(cause: FailureCause, message: string) {
    super(message);
    this.name = 'FlashError';
    this.cause = cause;
  }
}

/** The cause and the sentence, for anything that can be thrown at the machine. */
export function failureOf(error: unknown): { cause: FailureCause; message: string } {
  if (error instanceof FlashError) return { cause: error.cause, message: error.message };
  return {
    cause: 'unknown',
    message: error instanceof Error ? error.message : String(error),
  };
}
