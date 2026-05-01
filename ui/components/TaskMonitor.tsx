'use client';
import { useState } from 'react';
import type { TaskInfo, OutCmd } from '@/hooks/useAC2Socket';
import styles from './TaskMonitor.module.css';

interface Props {
  tasks: TaskInfo[];
  send:  (cmd: OutCmd) => void;
}

const TASK_STATE: Record<number, string> = {
  0: 'RUNNING', 1: 'READY', 2: 'BLOCKED', 3: 'SUSP', 4: 'DELETED',
};

const TASK_TYPE_LABELS = ['BLINK', 'COUNTER', 'LOAD'];

export default function TaskMonitor({ tasks, send }: Props) {
  const [newType,  setNewType]  = useState<0 | 1 | 2>(0);
  const [newParam, setNewParam] = useState(5);

  function createTask() {
    send({ type: 'cmd.create_task', task_type: newType, param: newParam });
  }

  function deleteTask(slotIndex: number) {
    send({ type: 'cmd.delete_task', slot_index: slotIndex });
  }

  return (
    <div className={styles.panel}>
      <div className={styles.title}>TASK MONITOR</div>

      {tasks.length === 0 ? (
        <div className={styles.empty}>Waiting for task list from STM32…</div>
      ) : (
        <div className={styles.rows}>
          {tasks.map((t) => {
            const isUser  = (t.task_id & 0x80) !== 0;
            const slotIdx = t.task_id & 0x7f;
            const pct     = Math.min(100, t.cpu_load);
            const warn    = (t.stack_watermark < 32);
            const barColor = warn ? 'var(--red)' : isUser ? 'var(--amber)' : 'var(--green)';

            return (
              <div key={`${t.name}-${t.task_id}`} className={styles.row}>
                <div className={styles.header}>
                  <span className={styles.name} style={{ color: isUser ? 'var(--amber)' : 'var(--text)' }}>
                    {t.name}
                  </span>
                  <span className={styles.prio} style={{ color: barColor }}>P{t.priority}</span>
                  <span className={styles.stateTag}>{TASK_STATE[t.state] ?? t.state}</span>
                  <span className={styles.cpu}>{pct}%</span>
                  <span className={styles.free} style={{ color: warn ? 'var(--red)' : 'var(--text-dim)' }}>
                    {t.stack_watermark}w
                  </span>
                  {isUser && (
                    <button className={styles.delBtn} onClick={() => deleteTask(slotIdx)}
                      title="Delete task">✕</button>
                  )}
                </div>
                <div className={styles.track}>
                  <div className={styles.fill} style={{ width: `${pct}%`, background: barColor }} />
                </div>
              </div>
            );
          })}
        </div>
      )}

      <div className={styles.divider} />

      <div className={styles.createRow}>
        <select
          className={styles.select}
          value={newType}
          onChange={(e) => setNewType(Number(e.target.value) as 0 | 1 | 2)}>
          {TASK_TYPE_LABELS.map((label, i) => (
            <option key={label} value={i}>{label}</option>
          ))}
        </select>
        <input
          className={styles.paramInput}
          type="number" min={0} max={255}
          value={newParam}
          onChange={(e) => setNewParam(Number(e.target.value))}
          title="param (BLINK=period×100ms, LOAD=cpu%)" />
        <button className={styles.addBtn} onClick={createTask}>+ ADD</button>
      </div>
    </div>
  );
}
