import { describe, expect, it } from 'vitest';
import type { ModelManifest } from '../src/domain/analysis';
import { modelSetId } from '../src/domain/snapshot';
import { ANALYZER_VERSION } from '../src/domain/version';
import { buildSnapshotExport, type SnapshotItem } from '../src/ui/snapshotExport';
import { sampleAnalysis } from './sampleAnalysis';

const manifest: ModelManifest = {
  modelSet: 'sha256:models',
  licence: 'test',
  source: 'test',
  models: [
    { name: 'b.onnx', sha256: '22', bytes: 2 },
    { name: 'a.onnx', sha256: '11', bytes: 1 },
  ],
};

function item(path: string, name = 'x.mp3'): SnapshotItem {
  return {
    file: new File([new Uint8Array([0, 1, 2])], name, { type: 'audio/mpeg' }),
    analysis: { ...sampleAnalysis, track: { ...sampleAnalysis.track, name, path } },
  };
}

describe('modelSetId', () => {
  it('does not depend on the order the manifest lists the models in', () => {
    expect(modelSetId([...manifest.models].reverse())).toBe(modelSetId(manifest.models));
  });

  it('changes when a weight changes', () => {
    const other = [{ name: 'a.onnx', sha256: '11', bytes: 1 }, { name: 'b.onnx', sha256: '33' }];
    expect(modelSetId(other)).not.toBe(modelSetId(manifest.models));
  });
});

describe('building the snapshot for the player', () => {
  it('writes the tracks below a music directory and counts the rest', async () => {
    const { bytes, tracks, skipped } = await buildSnapshotExport(
      [item('CARD/music/Artist/x.mp3'), item('CARD/elsewhere/y.mp3', 'y.mp3')],
      manifest,
    );

    expect(tracks).toBe(1);
    expect(skipped).toBe(1);

    /* The header, read here rather than through the module's own writer. */
    expect(new TextDecoder().decode(bytes.subarray(0, 8))).toBe('K7SNAP\0\0');
    const view = new DataView(bytes.buffer);
    expect(view.getUint16(8, true)).toBe(1);
    expect(view.getUint16(10, true)).toBe(64);
    expect(view.getUint32(12, true)).toBe(bytes.length);
    expect(view.getUint32(16, true)).toBe(1);
    expect(view.getUint32(20, true)).toBe(32);
    // The analyser version travels in the image as major*10000 + minor*100 + patch, and it
    // is the pipeline's own version — not the app release (ADR 0066).
    const [major = 0, minor = 0, patch = 0] = ANALYZER_VERSION.split('.').map(Number);
    expect(view.getUint32(40, true)).toBe(major * 10_000 + minor * 100 + patch);
  });

  it('writes no rows when nothing is under a music directory', async () => {
    const { tracks, skipped, bytes } = await buildSnapshotExport(
      [item('CARD/elsewhere/y.mp3', 'y.mp3')],
      manifest,
    );
    expect(tracks).toBe(0);
    expect(skipped).toBe(1);
    expect(bytes.length).toBe(64); // a header and nothing else
  });
});
