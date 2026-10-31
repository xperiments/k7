import {
  ANALYSIS_SCHEMA,
  MOOD_TAGS,
  type Analysis,
  type ModelManifest,
  type MoodTag,
  type TempoEngine,
  type TrackFormat,
} from '../domain/analysis';
import { camelot } from '../domain/camelot';
import {
  bpmFromBeats,
  foldTempo,
  normaliseDeepConfidence,
  shouldEscalate,
  tempoConfidence,
} from '../domain/tempo';
import { ANALYZER_VERSION } from '../domain/version';
import type { Essentia, EssentiaModules } from '../weights/essentia';
import { logMelFrames, N_BANDS, toPatches } from './mel';
import { MODEL_FILES } from './models';
import type { TrackMetadata } from './metadata';
import { embeddingOutput, type Runtime, type Session, type TensorData } from './runtime';


export interface DecodedAudio {
  /** Null when both tempo and key came from tags, so 44.1 kHz was never decoded. */
  pcm44k: Float32Array | null;
  pcm16k: Float32Array;
  duration: number;
}

export interface TrackInput {
  name: string;
  path: string;
  size: number;
  format: TrackFormat;
  hash: string;
}

export type Stage = 'tempo' | 'deep' | 'key' | 'mood';

/** Which output index is the positive class, per each head's model card. */
const POSITIVE_INDEX: Record<MoodTag, number> = {
  happy: 0,
  sad: 1,
  aggressive: 0,
  relaxed: 1,
  party: 1,
  danceable: 0,
};

const EFFNET_PATCH = 128;
const EFFNET_HOP = 62;
const MUSICNN_PATCH = 187;
const MUSICNN_HOP = 93;

/** The 60 s window starting a quarter of the way in, clamped to the track. */
export function moodWindow(duration: number): { start: number; duration: number } {
  const length = Math.min(60, Math.max(0, duration));
  const start = duration > length ? Math.min(duration * 0.25, duration - length) : 0;
  return { start, duration: length };
}

/** Pads very short tracks by repeating their last frame, so one patch exists. */
function patchesFor(
  frames: Float32Array[],
  patchSize: number,
  hop: number,
): Float32Array {
  if (frames.length >= patchSize) return toPatches(frames, patchSize, hop);
  if (frames.length === 0) {
    return new Float32Array(patchSize * N_BANDS);
  }
  const padded = Array.from(
    { length: patchSize },
    (_, i) => frames[Math.min(i, frames.length - 1)],
  );
  return toPatches(padded, patchSize, patchSize);
}

export class Analyzer {
  private essentia!: Essentia;
  private readonly sessions = new Map<string, Promise<Session>>();

  constructor(
    private readonly runtime: Runtime,
    private readonly manifest: ModelManifest,
    /** Loads a model's bytes; the caller has verified them against the manifest. */
    private readonly loadModel: (file: string) => Promise<Uint8Array>,
    /** Loads Essentia's JS module and wasm; kept out of the shell bundle. */
    private readonly importEssentia: () => Promise<EssentiaModules>,
  ) {}

  async init(): Promise<void> {
    const { Essentia, EssentiaWASM } = await this.importEssentia();
    this.essentia = new Essentia(EssentiaWASM);
  }

  /** Sessions load on first use, so fully tagged files load no models at all. */
  private session(file: string): Promise<Session> {
    let session = this.sessions.get(file);
    if (!session) {
      session = this.loadModel(file)
        .then((bytes) => this.runtime.createSession(bytes))
        .catch((error: unknown) => {
          // A failed download is retried on the next track rather than cached as a failure.
          this.sessions.delete(file);
          throw error;
        });
      this.sessions.set(file, session);
    }
    return session;
  }

  private async embed(
    session: Session,
    patches: Float32Array,
    patchSize: number,
  ): Promise<TensorData> {
    const count = Math.floor(patches.length / (patchSize * N_BANDS));
    const output = await session.run({
      [session.inputNames[0]]: {
        data: patches,
        dims: [count, patchSize, N_BANDS],
      },
    });
    return output[embeddingOutput(session)];
  }

  private async tags(embeddings: TensorData): Promise<Record<MoodTag, number>> {
    const count = embeddings.dims[0];
    const width = embeddings.dims[1];
    const result = {} as Record<MoodTag, number>;

    await Promise.all(
      MOOD_TAGS.map(async (tag) => {
        const session = await this.session(MODEL_FILES.heads[tag]);
        const output = await session.run({
          [session.inputNames[0]]: {
            data: embeddings.data,
            dims: [count, width],
          },
        });
        const activations = output[session.outputNames[0]];
        const positive = POSITIVE_INDEX[tag];
        let sum = 0;
        for (let i = 0; i < count; i++) {
          sum += activations.data[i * activations.dims[1] + positive];
        }
        result[tag] = count > 0 ? sum / count : 0.5;
      }),
    );

    return result;
  }

  private async valenceArousal(
    embeddings: TensorData,
  ): Promise<{ valence: number; arousal: number }> {
    const session = await this.session(MODEL_FILES.deam);
    const count = embeddings.dims[0];
    const output = await session.run({
      [session.inputNames[0]]: {
        data: embeddings.data,
        dims: [count, embeddings.dims[1]],
      },
    });
    const values = output[session.outputNames[0]];
    let valence = 0;
    let arousal = 0;
    for (let i = 0; i < count; i++) {
      valence += values.data[i * 2];
      arousal += values.data[i * 2 + 1];
    }
    // DEAM annotates valence and arousal on a 1–9 scale; present them as 0–1.
    const normalise = (value: number) =>
      Math.max(0, Math.min(1, count > 0 ? (value / count - 1) / 8 : 0.5));
    return { valence: normalise(valence), arousal: normalise(arousal) };
  }

  private deepTempo(vec44: unknown, fallbackBpm: number): {
    rawBpm: number;
    confidence: number;
  } {
    const result = this.essentia.RhythmExtractor2013(vec44 as never);
    if (!Number.isFinite(result.bpm) || result.bpm <= 0) {
      return { rawBpm: fallbackBpm, confidence: 0 };
    }
    return {
      rawBpm: result.bpm,
      confidence: normaliseDeepConfidence(result.confidence),
    };
  }

  /** Re-runs only the tempo measurement, for the manual deep button. */
  async deepTempoOnly(decoded: DecodedAudio): Promise<Analysis['tempo']> {
    if (!decoded.pcm44k) {
      throw new Error('44.1 kHz audio is required for deep tempo analysis');
    }
    const vec44 = this.essentia.arrayToVector(decoded.pcm44k);
    const deep = this.deepTempo(vec44, 0);
    return {
      bpm: foldTempo(deep.rawBpm),
      rawBpm: deep.rawBpm,
      confidence: deep.confidence,
      engine: 'deep',
      escalated: false,
      source: 'measured',
    };
  }

  async analyze(
    decoded: DecodedAudio,
    track: TrackInput,
    metadata: TrackMetadata = {},
    onStage?: (stage: Stage) => void,
  ): Promise<Analysis> {
    if (metadata.stored) {
      const stored = metadata.stored;
      return {
        schema: ANALYSIS_SCHEMA,
        track: {
          name: track.name,
          path: track.path,
          size: track.size,
          format: track.format,
          duration: decoded.duration,
          hash: track.hash,
        },
        tempo: {
          bpm: foldTempo(stored.bpm),
          rawBpm: stored.bpm,
          confidence: null,
          engine: null,
          escalated: false,
          source: 'metadata',
        },
        key: {
          tonic: stored.key.tonic,
          mode: stored.key.mode,
          camelot: stored.key.camelot,
          strength: null,
          source: 'metadata',
        },
        mood: {
          tags: stored.moodTags,
          label: stored.moodLabel,
          source: 'metadata',
          valence: stored.valence,
          arousal: stored.arousal,
        },
        energy: stored.energy,
        energySource: stored.energy !== null ? 'metadata' : null,
        provenance: {
          analyzer: ANALYZER_VERSION,
          modelSet: this.manifest.modelSet,
          window: null,
          analysedAt: new Date().toISOString(),
        },
      };
    }

    let vector44: ReturnType<Essentia['arrayToVector']> | null = null;
    const vec44 = () => {
      if (!decoded.pcm44k) {
        throw new Error('44.1 kHz audio is required to measure tempo or key');
      }
      vector44 ??= this.essentia.arrayToVector(decoded.pcm44k);
      return vector44;
    };

    let tempo: Analysis['tempo'];
    if (metadata.bpm !== undefined) {
      tempo = {
        bpm: foldTempo(metadata.bpm),
        rawBpm: metadata.bpm,
        confidence: null,
        engine: null,
        escalated: false,
        source: 'metadata',
      };
    } else {
      onStage?.('tempo');
      const beats = this.essentia.vectorToArray(
        this.essentia.BeatTrackerDegara(vec44()).ticks,
      );
      let rawBpm = bpmFromBeats(beats);
      let confidence = tempoConfidence(beats);
      let engine: TempoEngine = 'fast';
      let escalated = false;

      if (shouldEscalate(confidence, beats.length)) {
        onStage?.('deep');
        const deep = this.deepTempo(vec44(), rawBpm);
        rawBpm = deep.rawBpm;
        confidence = deep.confidence;
        engine = 'deep';
        escalated = true;
      }

      tempo = {
        bpm: foldTempo(rawBpm),
        rawBpm,
        confidence,
        engine,
        escalated,
        source: 'measured',
      };
    }

    let key: Analysis['key'];
    if (metadata.key) {
      key = { ...metadata.key, strength: null, source: 'metadata' };
    } else {
      onStage?.('key');
      const measured = this.essentia.KeyExtractor(
        vec44(),
        true,
        4096,
        4096,
        12,
        3500,
        60,
        25,
        0.2,
        'bgate',
        44100,
      );
      const mode = measured.scale === 'minor' ? 'minor' : 'major';
      key = {
        tonic: measured.key,
        mode,
        camelot: camelot(measured.key, mode),
        strength: measured.strength,
        source: 'measured',
      };
    }

    const needTags = metadata.mood === undefined;
    const needAxes = metadata.energy === undefined;
    let tags: Record<MoodTag, number> | null = null;
    let valence: number | null = null;
    let arousal: number | null = null;
    let window: { start: number; duration: number } | null = null;

    if (needTags || needAxes) {
      onStage?.('mood');
      window = moodWindow(decoded.duration);
      const from = Math.floor(window.start * 16_000);
      const to = Math.min(
        decoded.pcm16k.length,
        Math.floor((window.start + window.duration) * 16_000),
      );
      const frames = logMelFrames(decoded.pcm16k.subarray(from, to));

      const effnetPatches = needTags
        ? patchesFor(frames, EFFNET_PATCH, EFFNET_HOP)
        : null;
      const musicnnPatches = needAxes
        ? patchesFor(frames, MUSICNN_PATCH, MUSICNN_HOP)
        : null;
      const [effnet, musicnn] = await Promise.all([
        effnetPatches
          ? this.session(MODEL_FILES.effnet).then((session) =>
              this.embed(session, effnetPatches, EFFNET_PATCH),
            )
          : Promise.resolve(null),
        musicnnPatches
          ? this.session(MODEL_FILES.musicnn).then((session) =>
              this.embed(session, musicnnPatches, MUSICNN_PATCH),
            )
          : Promise.resolve(null),
      ]);

      const [measuredTags, axes] = await Promise.all([
        effnet ? this.tags(effnet) : Promise.resolve(null),
        musicnn ? this.valenceArousal(musicnn) : Promise.resolve(null),
      ]);
      tags = measuredTags;
      valence = axes?.valence ?? null;
      arousal = axes?.arousal ?? null;
    }

    const energy = metadata.energy ?? arousal;
    const energySource: Analysis['energySource'] =
      metadata.energy !== undefined
        ? 'metadata'
        : arousal !== null
          ? 'measured'
          : null;

    return {
      schema: ANALYSIS_SCHEMA,
      track: {
        name: track.name,
        path: track.path,
        size: track.size,
        format: track.format,
        duration: decoded.duration,
        hash: track.hash,
      },
      tempo,
      key,
      mood: {
        tags,
        label: metadata.mood ?? null,
        source: metadata.mood !== undefined ? 'metadata' : 'measured',
        valence,
        arousal,
      },
      energy,
      energySource,
      provenance: {
        analyzer: ANALYZER_VERSION,
        modelSet: this.manifest.modelSet,
        window,
        analysedAt: new Date().toISOString(),
      },
    };
  }
}
