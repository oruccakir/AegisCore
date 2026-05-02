import styles from '@/app/page.module.css';

interface Props {
  label: string;
  active: boolean;
  disabled: boolean;
  color: string;
  onClick: () => void;
}

export default function StateButton({ label, active, disabled, color, onClick }: Props) {
  return (
    <button
      className={`${styles.stateBtn} ${active ? styles.stateBtnActive : ''}`}
      style={active ? { borderColor: color, color, boxShadow: `0 0 12px ${color}44` } : undefined}
      disabled={disabled}
      onClick={onClick}
    >
      {active && <span className={styles.activeDot} style={{ background: color }} />}
      {label}
    </button>
  );
}
