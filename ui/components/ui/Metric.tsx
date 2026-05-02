import styles from '@/app/page.module.css';

export default function Metric({ label, value, warn }: { label: string; value: string; warn?: boolean }) {
  return (
    <div className={styles.metric}>
      <span className={styles.metricLabel}>{label}</span>
      <span className={styles.metricValue} style={{ color: warn ? 'var(--amber)' : undefined }}>
        {value}
      </span>
    </div>
  );
}
