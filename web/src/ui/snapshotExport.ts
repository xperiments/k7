import { ANALYSIS_SCHEMA, type Analysis, type ModelManifest } from '../domain/analysis';
import { ANALYZER_VERSION } from '../domain/version';
import {
  buildSnapshot,
  modelSetId,
  snapshotPath,
  type SnapshotTrack,
} from '../domain/snapshot';

export interface SnapshotTags {
  title: string;
  artist: string;
  album: string;
  genre: string;
}

/** One analysed Track, as the queue holds it: the File, for its tags, and the analysis. */
export interface SnapshotItem {
  file: File;
  analysis: Analysis | null;
  /** Tags the page already read into its own cache; the file is only read when absent. */
  tags?: SnapshotTags;
}

export interface SnapshotExport {
  bytes: Uint8Array<ArrayBuffer>;
  /** Rows written. */
  tracks: number;
  /** Analysed Tracks outside a `music` directory, so not part of the card's Library. */
  skipped: number;
}

/** The analyser's version as the format's number: major*10000 + minor*100 + patch. */
function versionNumber(version: string): number {
  const parts = version.split('.').map((part) => Number.parseInt(part, 10));
  const [major = 0, minor = 0, patch = 0] = parts.map((n) => (Number.isFinite(n) ? n : 0));
  return (major * 10000 + minor * 100 + patch) >>> 0;
}

/**
 * Builds `xtape.db` for the player from what the queue has measured.
 *
 * The snapshot's rows are keyed by the Track's path below the card's `music` directory,
 * so only Tracks that are inside one can be written: a folder dropped from somewhere else
 * is measured and shown, and simply not part of the card's Library. Titles come from the
 * files' own tags, because the player's text search is only as good as the names it has.
 */
export async function buildSnapshotExport(
  items: readonly SnapshotItem[],
  manifest: ModelManifest,
): Promise<SnapshotExport> {
  const { parseBlob } = await import('music-metadata');
  const tracks: SnapshotTrack[] = [];
  let skipped = 0;

  for (const item of items) {
    const { analysis } = item;
    if (!analysis) continue;
    if (snapshotPath(analysis.track.path) === null) {
      skipped += 1;
      continue;
    }

    /* Tags are a display and search convenience, never a reason to fail a run: a file
     * with none still gets its measurements. The page read them at analysis time and keeps
     * them beside the cover, so this only parses when the cache has nothing. */
    let title = item.tags?.title ?? '';
    let artist = item.tags?.artist ?? '';
    let album = item.tags?.album ?? '';
    let genre = item.tags?.genre ?? '';
    if (!item.tags) {
      try {
        const { common } = await parseBlob(item.file, { duration: false, skipCovers: true });
        title = common.title ?? '';
        artist = common.artist ?? '';
        album = common.album ?? '';
        genre = common.genre?.[0] ?? '';
      } catch {
        /* unreadable tags: the row keeps its measurements and empty strings */
      }
    }

    tracks.push({
      path: analysis.track.path,
      size: analysis.track.size,
      analysis,
      title,
      artist,
      album,
      genre,
    });
  }

  const bytes = buildSnapshot({
    tracks,
    generationSeconds: Math.floor(Date.now() / 1000),
    analyserVersion: versionNumber(ANALYZER_VERSION),
    appSchema: ANALYSIS_SCHEMA,
    modelSet: modelSetId(manifest.models),
  });
  return { bytes, tracks: tracks.length, skipped };
}
