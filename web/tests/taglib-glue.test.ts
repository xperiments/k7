import { mkdir, readFile, writeFile } from 'node:fs/promises';
import { join } from 'node:path';
import { pathToFileURL } from 'node:url';
import { parseBlob } from 'music-metadata';
import { describe, expect, it } from 'vitest';
import { patchTaglibGlue, TaglibGlueError } from '../src/audio/taglib-glue';

/**
 * The pinned glue the App bundles (`taglib-wasm@2.4.0`, imported by `index.browser.js` as a
 * separate module). Reading the real bytes is the point: a rewrite tested against a stand-in
 * would prove nothing about the module that ships.
 */
const DIST = join(process.cwd(), 'node_modules', 'taglib-wasm', 'dist');
const GLUE = join(DIST, 'taglib-wrapper.js');
const BUNDLE = join(DIST, 'index.browser.js');
const WASM = join(DIST, 'taglib-web.wasm');

const DIR = join(process.cwd(), 'node_modules', '.cache', 'k7-taglib-glue');
const FIXTURES = {
  mp3: new URL('./fixtures/tagged.mp3', import.meta.url),
  flac: new URL('./fixtures/plain.flac', import.meta.url),
};

/**
 * The bundle imports its glue by relative path, so the two files are staged together: the
 * patched glue replaces the shipped one and the bundle resolves to it.
 */
async function stage(name: string, glue: string, bundle: string): Promise<string> {
  const dir = join(DIR, name);
  await mkdir(dir, { recursive: true });
  await writeFile(join(dir, 'taglib-wrapper.js'), glue);
  await writeFile(join(dir, 'index.browser.js'), bundle);
  return pathToFileURL(join(dir, 'index.browser.js')).href;
}

describe('TagLib under a policy without unsafe-eval', () => {
  it('rewrites every dynamic-code site out of the pinned glue', async () => {
    const glue = await readFile(GLUE, 'utf8');
    // The problem, on the bytes that ship. If this line ever fails, the pinned build stopped
    // using dynamic code and this module can be deleted.
    expect(glue).toContain('new Function');

    const patched = patchTaglibGlue(glue);

    expect(patched).not.toContain('new Function');
    // Both closures, written out, and the module otherwise intact.
    expect(patched).toContain('invoker.apply(null,[fn].concat(wired))');
    expect(patched).toContain('Reflect.construct(holder,argv)');
    expect(patched.length).toBeGreaterThan(glue.length);
  });

  it('writes and reads the same tags with dynamic code forbidden', async () => {
    const [glue, bundle, wasm] = await Promise.all([
      readFile(GLUE, 'utf8'),
      readFile(BUNDLE, 'utf8'),
      readFile(WASM),
    ]);

    // A `Function` that only counts, so the assertion can say "none were built".
    const RealFunction = globalThis.Function;
    let built = 0;
    globalThis.Function = new Proxy(RealFunction, {
      apply(target, thisArg, args) {
        built += 1;
        return Reflect.apply(target, thisArg, args);
      },
      construct(target, args) {
        built += 1;
        return Reflect.construct(target, args);
      },
    });

    try {
      const href = await stage('patched', patchTaglibGlue(glue), bundle);
      const { TagLib } = (await import(/* @vite-ignore */ href)) as {
        TagLib: { initialize(options: { wasmBinary: Uint8Array }): Promise<TaglibApi> };
      };
      const taglib = await TagLib.initialize({ wasmBinary: new Uint8Array(wasm) });
      expect(built).toBe(0);

      for (const format of ['mp3', 'flac'] as const) {
        const input = new Uint8Array(await readFile(FIXTURES[format]));
        const output = await taglib.edit(input, (audio) => {
          audio.setProperty('TITLE', `rewritten ${format}`);
          audio.setProperty('ARTIST', 'K7');
        });
        expect(output.byteLength).toBeGreaterThan(0);

        const buffer = new ArrayBuffer(output.byteLength);
        new Uint8Array(buffer).set(output);
        const parsed = await parseBlob(
          new File([buffer], `probe.${format}`, {
            type: format === 'mp3' ? 'audio/mpeg' : 'audio/flac',
          }),
          { skipCovers: true },
        );
        expect(parsed.common.title).toBe(`rewritten ${format}`);
        expect(parsed.common.artist).toBe('K7');
      }

      // The callback into wasm is the other half: it must not have needed a function body.
      expect(built).toBe(0);
    } finally {
      globalThis.Function = RealFunction;
    }
  }, 120_000);

  it('refuses glue it does not know rather than shipping dynamic code', () => {
    expect(() => patchTaglibGlue('function somethingElse(){}')).toThrow(TaglibGlueError);
    expect(() => patchTaglibGlue('function somethingElse(){}')).toThrow(/createJsInvoker/);
  });
});

interface TaglibApi {
  edit(bytes: Uint8Array, run: (audio: { setProperty(key: string, value: string): void }) => void): Promise<Uint8Array>;
}
