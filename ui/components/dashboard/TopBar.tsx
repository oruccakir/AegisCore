import type { SystemInfo } from '@/hooks/useAC2Socket';
import { STATE_COLORS, STATE_NAMES } from '@/lib/systemState';
import ConnectionDot from '@/components/ui/ConnectionDot';
import styles from '@/app/page.module.css';

interface Props {
  state: number;
  status: string;
  sysInfo: SystemInfo;
}

export default function TopBar({ state, status, sysInfo }: Props) {
  const stateColor = STATE_COLORS[state] ?? STATE_COLORS[0];

  return (
    <header className={styles.topbar}>
      <div className={styles.logo}>
        <span className={styles.logoAegis}>AEGIS</span>
        <span className={styles.logoCore}>CORE</span>
        <span className={styles.logoCci}>CCI v2</span>
      </div>

      <div
        className={styles.stateChip}
        style={{ borderColor: stateColor, color: stateColor, boxShadow: `0 0 10px ${stateColor}33` }}
      >
        <span className={styles.stateDot} style={{ background: stateColor, boxShadow: `0 0 6px ${stateColor}` }} />
        {STATE_NAMES[state]}
      </div>

      <div className={styles.connRow}>
        <ConnectionDot status={status} />
        <span
          style={{
            color: status === 'connected' ? 'var(--text-dim)' : '#ffaa00',
            marginLeft: 6,
            fontSize: 11,
            letterSpacing: '0.1em',
          }}
        >
          {status.toUpperCase()}
        </span>
        {sysInfo.version && (
          <span style={{ marginLeft: 12, color: 'var(--text-faint)', fontSize: 10 }}>
            fw {sysInfo.version}
          </span>
        )}
      </div>
    </header>
  );
}
