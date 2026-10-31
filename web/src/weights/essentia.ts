import EssentiaCore from 'essentia.js/dist/essentia.js-core.es.js';
import { ESSENTIA_WASM } from './manifest';
import { loadWeightText } from './loader';

export type Essentia = InstanceType<typeof EssentiaCore>;

export interface EssentiaModules {
  Essentia: typeof EssentiaCore;
  EssentiaWASM: unknown;
}

let modules: Promise<EssentiaModules> | null = null;

/**
 * Essentia is fetched as text, checked, and imported from a blob URL. It cannot be a static
 * import: the 2.56 MB module would then be frozen into the device's 1 MB shell, and the
 * manifest check would not happen at all. The smaller core class stays in the bundle.
 */
export function loadEssentia(): Promise<EssentiaModules> {
  if (!modules) {
    modules = (async () => {
      const text = await loadWeightText(ESSENTIA_WASM);
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
