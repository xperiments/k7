import { Badge } from '@/components/reui/badge';
import type { MeasurementSource } from '../domain/analysis';

/**
 * Where one value came from, on the row it belongs to.
 *
 * The distinction is the point of the tags-first rule: a value the file already carried was
 * never re-measured, and a reader who cannot see which is which has no way to tell a
 * preserved tag from a measurement that happened to agree. `none` is the third answer - the
 * tag was absent and no model ran, which is not the same as zero.
 *
 * The tone is the state: a tag is a caveat (it was preserved, not measured), a measurement
 * is the quiet default, and an absent value is the quietest of the three.
 */
export function SourceChip({ source }: { source: MeasurementSource | null }) {
  if (source === null) {
    return (
      <Badge variant="outline" radius="full" className="text-muted-foreground normal-case">
        not measured
      </Badge>
    );
  }
  if (source === 'metadata') {
    return (
      <Badge variant="warning-light" radius="full" className="normal-case" title="Taken from the file's tags, not re-measured">
        tag
      </Badge>
    );
  }
  return (
    <Badge variant="secondary" radius="full" className="normal-case">
      measured
    </Badge>
  );
}
