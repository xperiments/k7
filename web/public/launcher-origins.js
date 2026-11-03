/*
 * The App origins this Launcher trusts (L1, X2).
 *
 * Compiled in, never read from a URL parameter, a query string or the referrer. The shell
 * build replaces this whole file with the origins that build was told to trust; the hosted
 * build keeps the production default. Keeping it in its own file — rather than an inline
 * script — is what lets the Launcher keep `default-src 'self'` and no `'unsafe-inline'`.
 */
window.K7_APP_ORIGINS = ['https://k7.xperiments.dev'];
