import type { DecodedAudio } from './analyzer';
import type { TrackFormat } from '../domain/analysis';

export function trackFormat(file: File): TrackFormat | null {
  const name = file.name.toLowerCase();
  if (name.endsWith('.mp3') || file.type === 'audio/mpeg') return 'mp3';
  if (name.endsWith('.flac') || file.type === 'audio/flac') return 'flac';
  return null;
}

export async function sha256Hex(buffer: ArrayBuffer): Promise<string> {
  const digest = await crypto.subtle.digest('SHA-256', buffer);
  return Array.from(new Uint8Array(digest))
    .map((byte) => byte.toString(16).padStart(2, '0'))
    .join('');
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

/**
 * Decodes to mono at both rates the pipeline needs. `duration` comes from the
 * decoded samples, so an MP3's encoder padding and Xing header are honoured.
 */
export async function decodeFile(
  buffer: ArrayBuffer,
  need44k = true,
): Promise<DecodedAudio> {
  const [pcm16k, pcm44k] = await Promise.all([
    decodeAt(buffer.slice(0), 16_000),
    need44k ? decodeAt(buffer.slice(0), 44_100) : Promise.resolve(null),
  ]);
  const rate = pcm44k ? 44_100 : 16_000;
  const samples = pcm44k ?? pcm16k;
  return { pcm44k, pcm16k, duration: samples.length / rate };
}
