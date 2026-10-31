import * as ort from 'onnxruntime-web/wasm';
import { loadWeightBytes } from '../weights/loader';
import { ORT_WASM } from '../weights/manifest';
import type { Runtime, Session, TensorData } from './runtime';

// The loader's JavaScript glue is part of the shell; the 14 MB WebAssembly binary is a
// weight. ORT takes the buffer directly, so the verified bytes never become a URL that a
// second fetch could replace.
let prepared: Promise<void> | null = null;

function prepare(): Promise<void> {
  if (!prepared) {
    prepared = (async () => {
      const wasmBinary = await loadWeightBytes(ORT_WASM);
      ort.env.wasm.wasmPaths = {
        mjs: new URL('/ort/ort-wasm-simd-threaded.mjs', self.location.origin).href,
      };
      ort.env.wasm.wasmBinary = wasmBinary;
      ort.env.wasm.numThreads = self.crossOriginIsolated
        ? Math.min(4, navigator.hardwareConcurrency || 1)
        : 1;
    })().catch((error: unknown) => {
      prepared = null;
      throw error;
    });
  }
  return prepared;
}

export const browserRuntime: Runtime = {
  async createSession(model: Uint8Array): Promise<Session> {
    await prepare();
    const session = await ort.InferenceSession.create(model, {
      executionProviders: ['wasm'],
      graphOptimizationLevel: 'all',
    });

    return {
      inputNames: session.inputNames,
      outputNames: session.outputNames,
      async run(feeds: Record<string, TensorData>) {
        const inputs: Record<string, ort.Tensor> = {};
        for (const [name, tensor] of Object.entries(feeds)) {
          inputs[name] = new ort.Tensor('float32', tensor.data, [...tensor.dims]);
        }
        const outputs = await session.run(inputs);
        const result: Record<string, TensorData> = {};
        for (const [name, tensor] of Object.entries(outputs)) {
          result[name] = {
            data: tensor.data as Float32Array,
            dims: tensor.dims,
          };
        }
        return result;
      },
    };
  },
};
