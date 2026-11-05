import EssentiaCore from 'essentia.js/dist/essentia.js-core.es.js';
import { EssentiaGlueError, patchEssentiaGlue } from './essentia-glue';
import { WeightIncompatibleError } from './errors';
import { ESSENTIA_WASM } from './manifest';
import { loadWeightText } from './loader';

export type Essentia = InstanceType<typeof EssentiaCore>;

export interface EssentiaModules {
  Essentia: typeof EssentiaCore;
  EssentiaWASM: unknown;
}

let modules: Promise<EssentiaModules> | null = null;

/**
 * The glue, verified and then made loadable. A build this module cannot rewrite is a weight
 * failure — the analyser cannot start — not a per-track analysis error.
 */
async function loadGlue(): Promise<string> {
  const text = await loadWeightText(ESSENTIA_WASM);
  try {
    return patchEssentiaGlue(text);
  } catch (error) {
    if (error instanceof EssentiaGlueError) {
      throw new WeightIncompatibleError(ESSENTIA_WASM, error.message);
    }
    throw error;
  }
}

/**
 * Essentia is fetched as text, checked, and imported from a blob URL. It cannot be a static
 * import: the 2.56 MB module would then be frozen into the bundle, and the manifest check
 * would not happen at all. The smaller core class stays in the bundle.
 *
 * The text is patched before it becomes a blob: the pinned build's embind glue builds its
 * invokers with `new Function`, which this origin's policy refuses, so `new Essentia(...)`
 * fails before the first Track unless the dynamic code is written out (`essentia-glue.ts`
 * says which three sites and why).
 */
export function loadEssentia(): Promise<EssentiaModules> {
  if (!modules) {
    modules = (async () => {
      const text = await loadGlue();
      const url = URL.createObjectURL(new Blob([text], { type: 'text/javascript' }));
      try {
        const loaded = (await import(/* @vite-ignore */ url)) as { EssentiaWASM?: unknown };
        if (!loaded.EssentiaWASM) {
          throw new Error('The Essentia module did not export its WebAssembly binary.');
        }
        return { Essentia: EssentiaCore, EssentiaWASM: loaded.EssentiaWASM };
      } finally {
        URL.revokeObjectURL(url);
      }
    })().catch((error: unknown) => {
      // A failed load is retried on the next message rather than kept as a rejected promise.
      modules = null;
      throw error;
    });
  }
  return modules;
}
