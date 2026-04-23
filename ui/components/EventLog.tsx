'use client';
import type { LogEntry } from '@/hooks/useAC2Socket';
import styles from './EventLog.module.css';

interface Props { entries: LogEntry[]; }

const LEVEL_COLOR: Record<string, string> = {
  ok:    '#00ff41',
  info:  '#4a8a54',
  warn:  '#ffaa00',
  error: '#ff2222',
};

function fmt(ts: number) {
  const d = new Date(ts);
  return `${String(d.getHours()).padStart(2,'0')}:${String(d.getMinutes()).padStart(2,'0')}:${String(d.getSeconds()).padStart(2,'0')}.${String(d.getMilliseconds()).padStart(3,'0')}`;
}

export default function EventLog({ entries }: Props) {
  return (
    <div className={styles.wrap}>
      <div className={styles.header}>EVENT LOG</div>
      <div className={styles.list}>
        {entries.length === 0 && (
          <div className={styles.empty}>— awaiting events —</div>
        )}
        {entries.map((e, i) => (
          <div key={e.id} className={styles.entry} style={{ animationDelay: i === 0 ? '0ms' : undefined }}>
            <span className={styles.time}>{fmt(e.ts)}</span>
            <span className={styles.type} style={{ color: LEVEL_COLOR[e.level] }}>{e.type.padEnd(6)}</span>
            <span className={styles.text}>{e.text}</span>
          </div>
        ))}
      </div>
    </div>
  );
}
