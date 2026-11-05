declare module 'essentia.js/dist/essentia.js-core.es.js' {
  interface EssentiaVector {
    size(): number;
    /** Free the vector's copy in the WASM heap. JavaScript does not collect it. */
    delete(): void;
  }

  export default class Essentia {
    constructor(wasm: unknown);
    arrayToVector(array: Float32Array | number[]): EssentiaVector;
    vectorToArray(vector: EssentiaVector): Float32Array;
    BeatTrackerDegara(
      signal: EssentiaVector,
      maxTempo?: number,
      minTempo?: number,
    ): { ticks: EssentiaVector };
    RhythmExtractor2013(
      signal: EssentiaVector,
      maxTempo?: number,
      method?: string,
      minTempo?: number,
    ): {
      bpm: number;
      confidence: number;
      ticks: EssentiaVector;
      estimates: EssentiaVector;
      bpmIntervals: EssentiaVector;
    };
    KeyExtractor(
      audio: EssentiaVector,
      averageDetuningCorrection?: boolean,
      frameSize?: number,
      hopSize?: number,
      hpcpSize?: number,
      maxFrequency?: number,
      maximumSpectralPeaks?: number,
      minFrequency?: number,
      pcpThreshold?: number,
      profileType?: string,
      sampleRate?: number,
    ): { key: string; scale: string; strength: number };
  }
}

declare module 'essentia.js/dist/essentia-wasm.es.js' {
  export const EssentiaWASM: unknown;
}
