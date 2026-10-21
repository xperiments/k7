declare const __APP_VERSION__: string | undefined;

export const ANALYZER_VERSION =
  typeof __APP_VERSION__ === 'string' ? __APP_VERSION__ : 'dev';
