export function parseEnergyTag(raw: unknown): number | null {
  let value: number;

  if (typeof raw === 'number') {
    value = raw;
  } else if (typeof raw === 'string') {
    let text = raw.trim();
    if (!text) return null;
    const percent = text.endsWith('%');
    if (percent) text = text.slice(0, -1).trim();
    value = Number(text);
    if (!Number.isFinite(value)) return null;
    if (percent) value /= 100;
  } else {
    return null;
  }

  if (!Number.isFinite(value) || value < 0) return null;
  if (value > 1) {
    // 0–100 scales are common; a stray 1.5 is ambiguous rather than 1.5%.
    return value >= 2 && value <= 100 ? value / 100 : null;
  }
  return value;
}
