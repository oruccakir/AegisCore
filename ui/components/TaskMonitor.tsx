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

type TaskType = 0 | 1 | 2 | 3;

const TASK_TYPE_LABELS: Record<TaskType, string> = {
  0: 'BLINK',
  1: 'COUNTER',
  2: 'LOAD',
  3: 'RANGE SCAN',
};

const PARAM_META: Record<TaskType, { label: string; hint: (p: number) => string }> = {
  0: {
    label: 'half-period (×100 ms)',
    hint:  (p) => `LED toggles every ${p * 100} ms → ${p > 0 ? (1000 / (p * 100)).toFixed(2) : '∞'} Hz`,
  },
  1: {
    label: 'period (×10 ms)',
    hint:  (p) => `counts every ${p * 10} ms → ${p > 0 ? Math.round(1000 / (p * 10)) : '∞'} /s`,
  },
  2: {
    label: 'spin multiplier (×10 000)',
    hint:  (p) => `busy-wait ~${(p * 10000 * 20 / 1_000_000).toFixed(1)} ms / 100 ms window`,
  },
  3: {
    label: 'near threshold (cm)',
    hint:  (p) => `servo scans until an object is within ${p > 0 ? p : 20} cm`,
  },
};

export default function TaskMonitor({ tasks, send }: Props) {
  const [newType,  setNewType]  = useState<TaskType>(0);
  const [newParam, setNewParam] = useState(5);

  const meta = PARAM_META[newType];

  function createTask() {
    send({ type: 'cmd.create_task', task_type: newType, param: newParam });
  }

  function selectTaskType(taskType: TaskType) {
    setNewType(taskType);
    if (taskType === 3 && newParam < 10) {
      setNewParam(30);
    } else if (taskType !== 3 && newParam === 30) {
      setNewParam(5);
    }
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

      <div className={styles.createSection}>
        <div className={styles.createRow}>
          <select
            className={styles.select}
            value={newType}
            onChange={(e) => selectTaskType(Number(e.target.value) as TaskType)}>
            {(Object.keys(TASK_TYPE_LABELS) as Array<`${TaskType}`>).map((key) => (
              <option key={key} value={key}>{TASK_TYPE_LABELS[Number(key) as TaskType]}</option>
            ))}
          </select>
          <div className={styles.paramWrap}>
            <input
              className={styles.paramInput}
              type="number" min={0} max={255}
              value={newParam}
              onChange={(e) => setNewParam(Math.max(0, Math.min(255, Number(e.target.value))))} />
          </div>
          <button className={styles.addBtn} onClick={createTask}>+ ADD</button>
        </div>
        <div className={styles.paramHint}>
          <span className={styles.paramLabel}>{meta.label}</span>
          <span className={styles.paramCalc}>{meta.hint(newParam)}</span>
        </div>
      </div>
    </div>
  );
}
