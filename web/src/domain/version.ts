/**
 * The analysis pipeline's version — the one value that says "an Analysis stored by this
 * version is still the Analysis this build would produce".
 *
 * It is **not** the app's version. `k7-analysis` is keyed by the content hash, this version
 * and the model set, so a card measured once is not measured again; taking the value from
 * the app version threw every stored Analysis away on every release, including releases that
 * changed nothing about measuring a Track. The app version still names the bundle, the shell
 * cache and the update banner — it just has no business in a measurement's identity.
 *
 * Bump this, and only this, when the same bytes and the same models would now produce a
 * different result: the mel front-end, a detector or a threshold, a field that must be filled
 * from scratch. The model set is already part of the key, so new weights do not need it.
 * Bumping invalidates every stored Analysis — a cost paid once, deliberately.
 */
export const ANALYZER_VERSION = '1.0.0';
