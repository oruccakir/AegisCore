export const STATE_NAMES = ['IDLE', 'SEARCH', 'TRACK', 'FAIL_SAFE'];

export const STATE_COLORS = ['#4a8a54', '#00ff41', '#ffaa00', '#ff2222'];

export const STATE_DESC = [
  'System standing by. No active scan.',
  'Scanning sector. Awaiting target acquisition.',
  'Target acquired. Tracking in progress.',
  'FAIL-SAFE ENGAGED. System locked.',
];

export function fmtUptime(ms: number) {
  const s = Math.floor(ms / 1000);
  const m = Math.floor(s / 60);
  const h = Math.floor(m / 60);
  return `${String(h).padStart(2, '0')}:${String(m % 60).padStart(2, '0')}:${String(s % 60).padStart(2, '0')}`;
}
