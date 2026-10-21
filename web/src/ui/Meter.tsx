interface MeterProps {
  value: number;
  label: string;
  warn?: boolean;
}

export function Meter({ value, label, warn = false }: MeterProps) {
  const clamped = Math.max(0, Math.min(1, Number.isFinite(value) ? value : 0));
  return (
    <div
      className={`meter${warn ? ' meter-warn' : ''}`}
      role="meter"
      aria-label={label}
      aria-valuemin={0}
      aria-valuemax={1}
      aria-valuenow={clamped}
    >
      <div className="meter-fill" style={{ width: `${clamped * 100}%` }} />
    </div>
  );
}
