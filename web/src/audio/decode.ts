import type { DecodedAudio } from './analyzer';
import { measureableName, type TrackFormat } from '../domain/analysis';
import { sha256Hex } from '../lib/sha256';

export { sha256Hex };

export function trackFormat(file: File): TrackFormat | null {
  const byName = measureableName(file.name);
  if (byName !== null) return byName;
  // A file whose type the browser knows but whose name hides its extension still decodes.
  if (file.type === 'audio/mpeg') return 'mp3';
  if (file.type === 'audio/flac') return 'flac';
  return null;
}


function mono(buffer: AudioBuffer): Float32Array {
  const channels = buffer.numberOfChannels;
  const out = new Float32Array(buffer.length);
  for (let channel = 0; channel < channels; channel++) {
    const data = buffer.getChannelData(channel);
    for (let i = 0; i < data.length; i++) out[i] += data[i];
  }
  if (channels > 1) {
    for (let i = 0; i < out.length; i++) out[i] /= channels;
  }
  return out;
}

async function decodeAt(bytes: ArrayBuffer, sampleRate: number): Promise<Float32Array> {
  // The context's rate is the decode target; its length is irrelevant.
  const context = new OfflineAudioContext(1, 1, sampleRate);
  const buffer = await context.decodeAudioData(bytes);
  return mono(buffer);
}

/** Resamples in memory: a rendered pass, not a second decode of the compressed file. */
async function resample(
  pcm: Float32Array,
  from: number,
  to: number,
): Promise<Float32Array> {
  const context = new OfflineAudioContext(1, Math.ceil((pcm.length * to) / from), to);
  const buffer = context.createBuffer(1, pcm.length, from);
  buffer.copyToChannel(new Float32Array(pcm), 0);
  const source = context.createBufferSource();
  source.buffer = buffer;
  source.connect(context.destination);
  source.start();
  const rendered = await context.startRendering();
  return rendered.getChannelData(0).slice();
}

/**
 * Decodes to mono at both rates the pipeline needs. `duration` comes from the decoded
 * samples, so an MP3's encoder padding and Xing header are honoured.
 *
 * The file is decoded **once**. It used to be decoded twice - once per rate - because each
 * `OfflineAudioContext.decodeAudioData` decodes and resamples the compressed bytes from
 * scratch, so the 16 kHz copy cost as much as the 44.1 kHz one. Now the 16 kHz signal is a
 * rendered resample of the 44.1 kHz decode: one decode of the file, one cheap pass.
 */
export async function decodeFile(
  buffer: ArrayBuffer,
  need44k = true,
): Promise<DecodedAudio> {
  if (!need44k) {
    const pcm16k = await decodeAt(buffer, 16_000);
    return { pcm44k: null, pcm16k, duration: pcm16k.length / 16_000 };
  }
  const pcm44k = await decodeAt(buffer, 44_100);
  const pcm16k = await resample(pcm44k, 44_100, 16_000);
  return { pcm44k, pcm16k, duration: pcm44k.length / 44_100 };
}
