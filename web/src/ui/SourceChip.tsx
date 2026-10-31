import type { MeasurementSource } from '../domain/analysis';

/**
 * Where one value came from, on the row it belongs to.
 *
 * The distinction is the point of the tags-first rule: a value the file already carried was
 * never re-measured, and a reader who cannot see which is which has no way to tell a
 * preserved tag from a measurement that happened to agree. `none` is the third answer - the
 * tag was absent and no model ran, which is not the same as zero.
 */
export function SourceChip({ source }: { source: MeasurementSource | null }) {
  if (source === null) return <em className="chip chip-none">not measured</em>;
  if (source === 'metadata') return <em className="chip chip-tag">tag</em>;
  return <em className="chip chip-measured">measured</em>;
}
