import type { OutCmd } from '@/hooks/useAC2Socket';
import StateButton from '@/components/ui/StateButton';
import styles from '@/app/page.module.css';

interface Props {
  state: number;
  isSafe: boolean;
  send: (cmd: OutCmd) => void;
}

export default function StateControlPanel({ state, isSafe, send }: Props) {
  function setSystemState(targetState: 'idle' | 'search' | 'track') {
    send({ type: 'cmd.set_state', targetState });
  }

  return (
    <div className={styles.panel}>
      <div className={styles.panelTitle}>STATE CONTROL</div>
      <div className={styles.btnGroup}>
        <StateButton
          label="IDLE"
          active={state === 0}
          disabled={!isSafe || state === 0}
          color="#4a8a54"
          onClick={() => setSystemState('idle')}
        />
        <StateButton
          label="SEARCH"
          active={state === 1}
          disabled={!isSafe || state === 1}
          color="#00ff41"
          onClick={() => setSystemState('search')}
        />
        <StateButton
          label="TRACK"
          active={state === 2}
          disabled={!isSafe || state === 2 || state === 0}
          color="#ffaa00"
          onClick={() => setSystemState('track')}
        />
      </div>

      <div className={styles.divider} />

      <div className={styles.btnRow}>
        <button
          className={`${styles.btn} ${styles.btnSecondary}`}
          disabled={!isSafe}
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
