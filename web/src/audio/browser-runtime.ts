import * as ort from 'onnxruntime-web/wasm';
import type { Runtime, Session, TensorData } from './runtime';

// ORT's loader is served from public/ort as a static asset. The absolute URL is
// deliberate: a bare "/ort/..." is resolved by Vite as a source import in dev and
// is rejected because public files cannot be imported from source.
ort.env.wasm.wasmPaths = new URL('/ort/', self.location.origin).href;
ort.env.wasm.numThreads = self.crossOriginIsolated
  ? Math.min(4, navigator.hardwareConcurrency || 1)
  : 1;

export const browserRuntime: Runtime = {
  async createSession(uri: string): Promise<Session> {
    const session = await ort.InferenceSession.create(uri, {
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
