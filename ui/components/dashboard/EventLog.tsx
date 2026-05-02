'use client';
import type { LogEntry } from '@/hooks/useAC2Socket';
import styles from '@/app/page.module.css';

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
    <div className={styles.eventLog}>
      <div className={styles.eventLogHeader}>EVENT LOG</div>
      <div className={styles.eventLogList}>
        {entries.length === 0 && (
          <div className={styles.eventLogEmpty}>— awaiting events —</div>
        )}
        {entries.map((e, i) => (
          <div key={e.id} className={styles.eventLogEntry} style={{ animationDelay: i === 0 ? '0ms' : undefined }}>
            <span className={styles.eventLogTime}>{fmt(e.ts)}</span>
            <span className={styles.eventLogType} style={{ color: LEVEL_COLOR[e.level] }}>{e.type.padEnd(6)}</span>
            <span className={styles.eventLogText}>{e.text}</span>
          </div>
        ))}
      </div>
    </div>
  );
}
