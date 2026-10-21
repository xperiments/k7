// Copies ONNX Runtime's CPU WASM loader into public/ort so it is served as a static
// asset with a stable name. The app references it by absolute URL, never as an import.
import { copyFile, mkdir } from 'node:fs/promises';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const root = join(here, '..');
const dist = join(root, 'node_modules', 'onnxruntime-web', 'dist');
const out = join(root, 'public', 'ort');

const files = ['ort-wasm-simd-threaded.mjs', 'ort-wasm-simd-threaded.wasm'];

await mkdir(out, { recursive: true });
for (const file of files) await copyFile(join(dist, file), join(out, file));
console.log(`ort assets -> public/ort (${files.join(', ')})`);
