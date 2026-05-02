import type { OutCmd } from '@/hooks/useAC2Socket';
import { STATE_COLORS, STATE_DESC, STATE_NAMES } from '@/lib/systemState';
import styles from '@/app/page.module.css';

interface Props {
  state: number;
  connected: boolean;
  send: (cmd: OutCmd) => void;
}

export default function StateStatusPanel({ state, connected, send }: Props) {
  const stateColor = STATE_COLORS[state] ?? STATE_COLORS[0];

  return (
    <div className={styles.panel}>
      <div className={styles.panelTitle}>STATE STATUS</div>
      <div className={styles.stateReadout} style={{ borderColor: `${stateColor}55` }}>
        <div className={styles.stateReadoutCurrent} style={{ color: stateColor }}>
          <span className={styles.stateReadoutDot} style={{ background: stateColor, boxShadow: `0 0 8px ${stateColor}` }} />
          {STATE_NAMES[state] ?? 'UNKNOWN'}
        </div>
        <div className={styles.stateReadoutText}>{STATE_DESC[state] ?? 'Telemetry state is outside the known state map.'}</div>
      </div>

      <div className={styles.stateFlow} aria-label="System state flow">
        {STATE_NAMES.map((name, index) => {
          const color = STATE_COLORS[index] ?? STATE_COLORS[0];
          const active = state === index;

          return (
            <div key={name} className={styles.stateFlowStep} data-active={active}>
              <span className={styles.stateFlowDot} style={{ background: active ? color : undefined }} />
              <span>{name}</span>
            </div>
          );
        })}
      </div>

      <div className={styles.divider} />

      <div className={styles.btnRow}>
        <button
          className={`${styles.btn} ${styles.btnSecondary}`}
          disabled={!connected}
          onClick={() => send({ type: 'cmd.manual_lock', lock: true })}
        >
          MANUAL LOCK
        </button>
        <button
          className={`${styles.btn} ${styles.btnSecondary}`}
          onClick={() => send({ type: 'cmd.get_version' })}
        >
          GET VERSION
        </button>
      </div>
    </div>
  );
}
