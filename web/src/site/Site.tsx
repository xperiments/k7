import { Flash } from './Flash';

/**
 * The Site's first shape: the product's sentence, and the App's address.
 *
 * This document exists so that `/` is the product's page and `/app` is the App (ADR 0078).
 * The hero's photo slot, the install and the footer are the next ticket; until they land,
 * the Site says what K7 is, offers the flash and links to the App.
 */
export function Site() {
  return (
    <main className="flex min-h-dvh flex-col items-center gap-8 bg-background px-6 py-16 text-foreground">
      <img
        src="/icons/k7logo.svg"
        alt="K7"
        draggable={false}
        className="size-24 object-contain"
      />
      <h1 className="max-w-2xl text-balance text-center text-3xl font-medium tracking-tight text-foreground sm:text-4xl">
        Your music, on a cassette
      </h1>
      <p className="max-w-xl text-balance text-center text-sm leading-relaxed text-muted-foreground sm:text-base">
        For people who own their music and miss the object it came in, K7 is a cassette-shaped
        player that turns a card into a tape and a room into a radio station.
      </p>
      <a
        href="/app"
        className="inline-flex h-9 items-center justify-center rounded-md bg-primary px-4 text-sm font-medium text-primary-foreground transition-colors hover:bg-primary/90"
      >
        Open K7
      </a>

      <div id="flash" className="w-full max-w-2xl">
        <Flash />
      </div>
    </main>
  );
}
