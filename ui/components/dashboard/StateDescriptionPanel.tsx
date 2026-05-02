import { STATE_COLORS, STATE_DESC, STATE_NAMES } from '@/lib/systemState';
import styles from '@/app/page.module.css';

export default function StateDescriptionPanel({ state }: { state: number }) {
  const stateColor = STATE_COLORS[state] ?? STATE_COLORS[0];

  return (
    <div className={styles.stateDesc} style={{ borderColor: `${stateColor}44` }}>
      <div className={styles.stateDescTitle} style={{ color: stateColor }}>
        {STATE_NAMES[state]}
      </div>
      <div className={styles.stateDescText}>{STATE_DESC[state]}</div>
    </div>
  );
}
