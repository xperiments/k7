import { ANALYSIS_SCHEMA, type Analysis, type ModelManifest } from '../domain/analysis';
import { ANALYZER_VERSION } from '../domain/version';
import {
  buildProfile,
  buildSnapshot,
  modelSetId,
  snapshotPath,
  type LibraryProfile,
  type SnapshotTrack,
} from '../domain/snapshot';
import type { TrackTags } from '../audio/metadata';

/** One analysed Track, as the queue holds it: the File, for its tags, and the analysis. */
export interface SnapshotItem {
  file: File;
  analysis: Analysis | null;
  /** Tags the queue read at analysis time; the file is only read when they are absent. */
  tags?: TrackTags;
}

export interface SnapshotExport {
  bytes: Uint8Array<ArrayBuffer>;
  /** Rows written. */
  tracks: number;
  /** Analysed Tracks outside a `music` directory, so not part of the card's Library. */
  skipped: number;
  /** The generation the header carries, to match the player's Snapshot. */
  generation: number;
  /** The Library's aggregates, stored beside the image. */
  profile: LibraryProfile;
}

/** The analyser's version as the format's number: major*10000 + minor*100 + patch. */
function versionNumber(version: string): number {
  const parts = version.split('.').map((part) => Number.parseInt(part, 10));
  const [major = 0, minor = 0, patch = 0] = parts.map((n) => (Number.isFinite(n) ? n : 0));
  return (major * 10000 + minor * 100 + patch) >>> 0;
}

/**
 * Builds `k7.db` for the player from what the queue has measured.
 *
 * The snapshot's rows are keyed by the Track's path below the card's `music` directory,
 * so only Tracks that are inside one can be written: a folder dropped from somewhere else
 * is measured and shown, and simply not part of the card's Library. Titles come from the
 * files' own tags, because the player's text search is only as good as the names it has.
 *
 * The image carries measurements only: a Playlist is a file on the card (ADR 0095), so nothing
 * about a list is written here.
 */
export async function buildSnapshotExport(
  items: readonly SnapshotItem[],
  manifest: ModelManifest,

): Promise<SnapshotExport> {
  const { parseBlob } = await import('music-metadata');
  const tracks: SnapshotTrack[] = [];
  let skipped = 0;

  for (const item of items) {
    let { analysis } = item;
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

    /* A length is the one row field an older cached analysis can lack: the tag-only path
     * used to write `duration: 0`, and the cache is keyed by content, so re-measuring the
     * card never replaces it. The image is the thing that has to carry the value, so a row
     * without one reads it from the file here — the same header read that tags use, not a
     * decode. A row that already has a length is never re-opened for it. */
    let seconds = analysis.track.duration;
    const needsDuration = !(Number.isFinite(seconds) && seconds > 0);
    if (!item.tags || needsDuration) {
      try {
        const parsed = await parseBlob(item.file, {
          duration: needsDuration,
          skipCovers: true,
        });
        if (!item.tags) {
          const { common } = parsed;
          title = common.title ?? '';
          artist = common.artist ?? '';
          album = common.album ?? '';
          genre = common.genre?.[0] ?? '';
        }
        const read = parsed.format.duration;
        if (needsDuration && typeof read === 'number' && Number.isFinite(read) && read > 0) {
          seconds = read;
        }
      } catch {
        /* unreadable tags or length: the row keeps its measurements and empty strings */
      }
    }

    if (seconds !== analysis.track.duration && Number.isFinite(seconds) && seconds > 0) {
      analysis = { ...analysis, track: { ...analysis.track, duration: seconds } };
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

  const generationSeconds = Math.floor(Date.now() / 1000);
  const bytes = buildSnapshot({
    tracks,
    generationSeconds,
    analyserVersion: versionNumber(ANALYZER_VERSION),
    appSchema: ANALYSIS_SCHEMA,
    modelSet: modelSetId(manifest.models),
  });
  return {
    bytes,
    tracks: tracks.length,
    skipped,
    generation: generationSeconds,
    profile: buildProfile(tracks),
  };
}
