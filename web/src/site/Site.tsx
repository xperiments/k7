import { Button } from '@/components/ui/button';
import { Flash } from './Flash';
import { Install } from './Install';

/**
 * The Site: what K7 is, the flash and the install (k7-site.md §2), in that order.
 *
 * The origin's root is a page of the website, not the App; the App is at `/app`
 * (ADR 0078). The hero's media is a slot, not a composition: a photograph of the device can
 * replace the panel's own screen without the page moving around it.
 */

const SOURCE = 'https://github.com/xperiments/k7';
const NOTICES = `${SOURCE}/blob/master/THIRD_PARTY_NOTICES`;

export function Site() {
  return (
    <div className="min-h-dvh bg-background text-foreground">
      <div className="mx-auto flex w-full max-w-5xl flex-col gap-16 px-6 py-10 sm:py-14">
        <header className="flex items-center justify-between gap-4">
          <a
            href="#top"
            aria-label="K7"
            className="flex items-center gap-2 hover:text-foreground"
          >
            <span aria-hidden="true" className="k7-logo size-7" />
          </a>
          <a
            href="/app"
            className="text-sm text-muted-foreground underline-offset-4 transition-colors hover:text-foreground hover:underline"
          >
            Open K7
          </a>
        </header>

        <section id="top" className="grid items-center gap-10 md:grid-cols-[1.05fr_1fr]">
          <div className="grid gap-6">
            <h1 className="text-balance text-4xl font-light tracking-tight sm:text-5xl">
              Your music, on a cassette
            </h1>
            <p className="max-w-prose text-pretty text-base leading-relaxed text-muted-foreground">
              For people who own their music and miss the object it came in, K7 is a
              cassette-shaped player that turns a card into a tape and a room into a radio
              station.
            </p>
            <div className="flex flex-wrap items-center gap-3">
              <Button variant="solid" render={<a href="#flash" />}>Flash a board</Button>
              <Button variant="outline" render={<a href="#install" />}>
                Install the app
              </Button>
            </div>
          </div>

          {/* The hero's media slot. One file, one frame: the panel's own drawing today, a
            * photograph of the device tomorrow (k7-site.md S11). */}
          <figure className="overflow-hidden rounded-panel border border-border">
            <img
              src="/site/panel.png"
              width={800}
              height={480}
              alt="The K7 panel's cassette drawer: an orange line drawing of a tape on a dark screen"
              className="block h-auto w-full"
            />
          </figure>
        </section>

        <section id="flash" aria-label="Flash K7" className="scroll-mt-6">
          <Flash />
        </section>

        <section id="install" aria-label="Install K7" className="scroll-mt-6">
          <Install />
        </section>

        <footer className="flex flex-col gap-4 border-t border-border pt-6 text-xs text-muted-foreground sm:flex-row sm:items-center sm:justify-between">
          <p>K7 is free software, AGPL-3.0-only. The player runs K7’s firmware; the App runs in your browser.</p>
          <nav aria-label="Source and notices" className="flex flex-wrap gap-4">
            <a
              href={SOURCE}
              className="underline-offset-4 transition-colors hover:text-foreground hover:underline"
            >
              Source
            </a>
            <a
              href={NOTICES}
              className="underline-offset-4 transition-colors hover:text-foreground hover:underline"
            >
              Third-party notices
            </a>
            <a
              href="/app"
              className="underline-offset-4 transition-colors hover:text-foreground hover:underline"
            >
              Open K7
            </a>
          </nav>
        </footer>
      </div>
    </div>
  );
}
