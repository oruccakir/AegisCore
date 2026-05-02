'use client';
import { useState } from 'react';
import type { TaskInfo, OutCmd } from '@/hooks/useAC2Socket';
import styles from '@/app/page.module.css';

interface Props {
  tasks: TaskInfo[];
  send:  (cmd: OutCmd) => void;
}

const TASK_STATE: Record<number, string> = {
  0: 'RUNNING', 1: 'READY', 2: 'BLOCKED', 3: 'SUSP', 4: 'DELETED',
};

type TaskType = 0 | 3 | 4;

const TASK_TYPE_LABELS: Record<TaskType, string> = {
  0: 'BLINK',
  3: 'RANGE SCAN',
  4: 'LCD STATUS',
};

const PARAM_META: Record<TaskType, { label: string; hint: (p: number) => string }> = {
  0: {
    label: 'half-period (×100 ms)',
    hint:  (p) => `LED toggles every ${p * 100} ms → ${p > 0 ? (1000 / (p * 100)).toFixed(2) : '∞'} Hz`,
  },
  3: {
    label: 'near threshold (cm)',
    hint:  (p) => `servo scans until an object is within ${p > 0 ? p : 30} cm`,
  },
  4: {
    label: 'refresh period (×250 ms)',
    hint:  (p) => `LCD refreshes every ${(p > 0 ? p : 4) * 250} ms`,
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
    } else if (taskType === 4) {
      setNewParam(4);
    } else if (taskType !== 3 && newParam === 30) {
      setNewParam(5);
    }
  }

  function deleteTask(slotIndex: number) {
    send({ type: 'cmd.delete_task', slot_index: slotIndex });
  }

  return (
    <div className={styles.taskMonitorPanel}>
      <div className={styles.taskMonitorTitle}>TASK MONITOR</div>

      {tasks.length === 0 ? (
        <div className={styles.taskMonitorEmpty}>Waiting for task list from STM32…</div>
      ) : (
        <div className={styles.taskMonitorRows}>
          {tasks.map((t) => {
            const isUser  = (t.task_id & 0x80) !== 0;
            const slotIdx = t.task_id & 0x7f;
            const pct     = Math.min(100, t.cpu_load);
            const warn    = (t.stack_watermark < 32);
            const barColor = warn ? 'var(--red)' : isUser ? 'var(--amber)' : 'var(--green)';

            return (
              <div key={`${t.name}-${t.task_id}`} className={styles.taskMonitorRow}>
                <div className={styles.taskMonitorHeader}>
                  <span className={styles.taskMonitorName} style={{ color: isUser ? 'var(--amber)' : 'var(--text)' }}>
                    {t.name}
                  </span>
                  <span className={styles.taskMonitorPrio} style={{ color: barColor }}>P{t.priority}</span>
                  <span className={styles.taskMonitorStateTag}>{TASK_STATE[t.state] ?? t.state}</span>
                  <span className={styles.taskMonitorCpu}>{pct}%</span>
                  <span className={styles.taskMonitorFree} style={{ color: warn ? 'var(--red)' : 'var(--text-dim)' }}>
                    {t.stack_watermark}w
                  </span>
                  {isUser && (
                    <button className={styles.taskMonitorDelBtn} onClick={() => deleteTask(slotIdx)}
                      title="Delete task">✕</button>
                  )}
                </div>
                <div className={styles.taskMonitorTrack}>
                  <div className={styles.taskMonitorFill} style={{ width: `${pct}%`, background: barColor }} />
                </div>
              </div>
            );
          })}
        </div>
      )}

      <div className={styles.taskMonitorDivider} />

      <div className={styles.taskMonitorCreateSection}>
        <div className={styles.taskMonitorCreateRow}>
          <select
            className={styles.taskMonitorSelect}
            value={newType}
            onChange={(e) => selectTaskType(Number(e.target.value) as TaskType)}>
            {(Object.keys(TASK_TYPE_LABELS) as Array<`${TaskType}`>).map((key) => (
              <option key={key} value={key}>{TASK_TYPE_LABELS[Number(key) as TaskType]}</option>
            ))}
          </select>
          <div className={styles.taskMonitorParamWrap}>
            <input
              className={styles.taskMonitorParamInput}
              type="number" min={0} max={255}
              value={newParam}
              onChange={(e) => setNewParam(Math.max(0, Math.min(255, Number(e.target.value))))} />
          </div>
          <button className={styles.taskMonitorAddBtn} onClick={createTask}>+ ADD</button>
        </div>
        <div className={styles.taskMonitorParamHint}>
          <span className={styles.taskMonitorParamLabel}>{meta.label}</span>
          <span className={styles.taskMonitorParamCalc}>{meta.hint(newParam)}</span>
        </div>
      </div>
    </div>
  );
}
