import { parseEnergyTag } from '../domain/energyTag';
import { parseKeyTag, type ParsedKey } from '../domain/keyTag';
import { parseStoredAnalysis, type StoredAnalysis } from '../domain/storedAnalysis';

export interface TrackMetadata {
  /** BPM from an embedded tag (TBPM / Vorbis BPM). */
  bpm?: number;
  /** Key parsed from an embedded tag (TKEY / initialKey / key). */
  key?: ParsedKey;
  /** Free-text mood tag (TMOO / MOOD). */
  mood?: string;
  /** Normalised energy tag (ENERGY / TXXX:ENERGY), 0–1. */
  energy?: number;
  /** The lossless manifest written by this app, if present. */
  stored?: StoredAnalysis;
}

interface NativeTag {
  id: string;
  value: unknown;
}

const ENERGY_TAGS = new Set([
  'energy',
  'energylevel',
  'energy_level',
  'txxx:energy',
  'txxx:energylevel',
  'txxx:energy_level',
]);

const XTAPE_TAGS = new Set(['xtape', 'txxx:xtape']);
const KEY_TAGS = new Set(['initialkey', 'key', 'tkey', 'txxx:initialkey', 'txxx:key']);
const BPM_TAGS = new Set(['tbpm', 'bpm', 'txxx:bpm']);

function nativeValue(
  native: Record<string, NativeTag[]> | undefined,
  ids: Set<string>,
): unknown {
  if (!native) return undefined;
  for (const tags of Object.values(native)) {
    for (const tag of tags) {
      if (ids.has(tag.id.toLowerCase())) return tag.value;
    }
  }
  return undefined;
}

/**
 * Reads BPM and key from the file's own tags. Only metadata is parsed, not
 * audio frames, so this stays cheap even for long tracks. A failure to read
 * tags is not an analysis failure: the field is simply measured instead.
 */
export async function readTrackMetadata(file: File): Promise<TrackMetadata> {
  try {
    const { parseBlob } = await import('music-metadata');
    const parsed = await parseBlob(file, {
      duration: false,
      skipCovers: true,
    });
    const { common } = parsed;

    const native = parsed.native as Record<string, NativeTag[]>;
    const rawBpm = nativeValue(native, BPM_TAGS);
    const nativeBpm =
      typeof rawBpm === 'string' ? Number.parseFloat(rawBpm) : rawBpm;
    const bpm =
      typeof nativeBpm === 'number' && Number.isFinite(nativeBpm) && nativeBpm > 0
        ? nativeBpm
        : typeof common.bpm === 'number' && Number.isFinite(common.bpm) && common.bpm > 0
          ? common.bpm
          : undefined;
    const stored =
      parseStoredAnalysis(
        String(nativeValue(native, XTAPE_TAGS) ?? ''),
      ) ?? undefined;

    const rawKey = common.key ?? nativeValue(native, KEY_TAGS);
    const key = rawKey ? parseKeyTag(String(rawKey)) : null;
    const mood =
      typeof common.mood === 'string' && common.mood.trim()
        ? common.mood.trim()
        : undefined;
    const energy =
      parseEnergyTag(nativeValue(native, ENERGY_TAGS)) ?? undefined;

    if (stored) {
      return {
        bpm: stored.bpm,
        key: stored.key,
        mood: stored.moodLabel ?? undefined,
        energy: stored.energy ?? undefined,
        stored,
      };
    }

    return { bpm, key: key ?? undefined, mood, energy };
  } catch {
    return {};
  }
}
