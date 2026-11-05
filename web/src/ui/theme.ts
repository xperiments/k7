import { useSyncExternalStore } from 'react';

/**
 * Light or dark, remembered per browser.
 *
 * The stylesheet carries both palettes and the `dark` class on `<html>` chooses one, so the
 * whole choice is that class plus the browser's own chrome colour. It is a setting of this
 * browser, like the Provider: `localStorage`, never the player, and lost with site data.
 *
 * Light is the default. The App is a library on a page, not the device's OLED, and the
 * toggle is there for anyone who wants the dark palette anyway.
 */
export type Theme = 'light' | 'dark';

const STORAGE_KEY = 'k7.theme.v1';

/** The page's background per theme, for the browser's address bar and task switcher. */
const THEME_COLOR: Record<Theme, string> = {
  light: '#fbfaf8',
  dark: '#0e0f11',
};

function read(): Theme {
  try {
    return localStorage.getItem(STORAGE_KEY) === 'dark' ? 'dark' : 'light';
  } catch {
    return 'light';
  }
}

let cache: Theme = read();
const listeners = new Set<() => void>();

/**
 * Paint the document for a theme. The class is what every `dark:` utility and the `.dark`
 * palette key on; the metas are what the browser paints around the page and uses for native
 * controls, and they have to follow or a dark page gets a white address bar.
 */
function apply(theme: Theme): void {
  if (typeof document === 'undefined') return;
  document.documentElement.classList.toggle('dark', theme === 'dark');
  const color = document.querySelector<HTMLMetaElement>('meta[name="theme-color"]');
  if (color !== null) color.content = THEME_COLOR[theme];
  const scheme = document.querySelector<HTMLMetaElement>('meta[name="color-scheme"]');
  if (scheme !== null) scheme.content = theme;
}

/** Apply the remembered theme. Called once at boot, before the first React render. */
export function startTheme(): void {
  apply(cache);
}

export function getTheme(): Theme {
  return cache;
}

export function setTheme(theme: Theme): void {
  if (theme === cache) return;
  cache = theme;
  try {
    localStorage.setItem(STORAGE_KEY, theme);
  } catch {
    /* A browser that refuses storage keeps the theme for this page's life. */
  }
  apply(theme);
  for (const listener of listeners) listener();
}

export function toggleTheme(): void {
  setTheme(cache === 'dark' ? 'light' : 'dark');
}

function subscribe(listener: () => void): () => void {
  listeners.add(listener);
  return () => listeners.delete(listener);
}

/** The theme the page is wearing, re-rendering the toggle when it changes. */
export function useTheme(): Theme {
  return useSyncExternalStore(subscribe, getTheme, getTheme);
}
