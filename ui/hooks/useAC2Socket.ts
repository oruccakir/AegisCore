'use client';
import { useEffect, useRef, useCallback, useState } from 'react';

export type ConnStatus = 'connecting' | 'connected' | 'disconnected';

export interface Telemetry {
  state: number;
  cpu_load_x10: number;
  stack_uart_rx: number;
  stack_state_core: number;
  stack_tel_tx: number;
  stack_heartbeat: number;
  hb_miss_count: number;
}

export interface SystemInfo {
  uptime_ms: number;
  version?: string;
  git_sha?: string;
  boot_reason?: string;
  boot_reason_bits?: number;
}

export interface LogEntry {
  id: number;
  ts: number;
  type: string;
  text: string;
  level: 'info' | 'warn' | 'error' | 'ok';
}

export interface TaskInfo {
  name:            string;
  state:           number;
  priority:        number;
  stack_watermark: number;
  cpu_load:        number;
  task_id:         number; // bit7=1 → user task, bits[2:0] = slot index
  last_seen_ms?:   number;
}

export interface DetectionInfo {
  class_id: number;
  class_name: string;
  confidence: number;
  latency_ms: number;
  ts: number;
}

export interface RangeScanInfo {
  angle_deg: number;
  distance_cm: number;
  locked: boolean;
  valid: boolean;
  threshold_cm: number;
  ts: number;
}

export type OutCmd =
  | { type: 'cmd.manual_lock'; lock: boolean }
  | { type: 'cmd.get_version' }
  | { type: 'cmd.system_reset' }
  | { type: 'cmd.heartbeat' }
  | { type: 'cmd.create_task'; task_type: 0 | 3 | 4; param: number }
  | { type: 'cmd.delete_task'; slot_index: number }
  | { type: 'cmd.vision_frame'; jpeg_b64: string };

const ERR_CODES: Record<number, string> = {
  1: 'INVALID_CMD', 2: 'INVALID_PAYLOAD', 3: 'INVALID_TRANSITION',
  4: 'AUTH_FAIL', 5: 'REPLAY', 6: 'RATE_LIMITED', 7: 'FAIL_SAFE_LOCK', 8: 'BUSY',
};

const STATE_NAMES = ['IDLE', 'SEARCH', 'TRACK', 'FAIL_SAFE'];

const AUDIT_EVENTS: Record<number, string> = {
  1: 'BOOT',
  2: 'TASK CREATE',
  3: 'TASK DELETE',
  4: 'RANGE LOCK',
  5: 'VISION HIT',
  6: 'SYSTEM RESET',
  7: 'FAIL SAFE',
};

function decodeBootReason(bits: number): string {
  if ((bits & 0x10000000) !== 0) return 'SOFTWARE RESET';
  if ((bits & 0x20000000) !== 0) return 'IWDG RESET';
  if ((bits & 0x40000000) !== 0) return 'WWDG RESET';
  if ((bits & 0x08000000) !== 0) return 'POWER RESET';
  if ((bits & 0x04000000) !== 0) return 'NRST PIN';
  if ((bits & 0x02000000) !== 0) return 'BROWNOUT';
  if ((bits & 0x80000000) !== 0) return 'LOW POWER RESET';
  return bits === 0 ? 'UNKNOWN' : `RCC 0x${bits.toString(16)}`;
}

function describeCommand(cmd: OutCmd): string {
  switch (cmd.type) {
    case 'cmd.manual_lock':
      return cmd.lock ? 'manual fail-safe lock' : 'manual lock release request';
    case 'cmd.get_version':
      return 'get version';
    case 'cmd.system_reset':
      return 'system reset requested';
    case 'cmd.heartbeat':
      return 'heartbeat sent';
    case 'cmd.create_task':
      return `create task type=${cmd.task_type} param=${cmd.param}`;
    case 'cmd.delete_task':
      return `delete task slot=${cmd.slot_index}`;
    case 'cmd.vision_frame':
      return 'vision frame submitted';
  }
}

let logId  = 0;
let cmdSeq = 0;

export function useAC2Socket(url: string) {
  const wsRef   = useRef<WebSocket | null>(null);
  const timerRef = useRef<ReturnType<typeof setTimeout> | null>(null);

  const [status,    setStatus]    = useState<ConnStatus>('connecting');
  const [telemetry, setTelemetry] = useState<Telemetry | null>(null);
  const [sysInfo,   setSysInfo]   = useState<SystemInfo>({ uptime_ms: 0 });
  const [log,       setLog]       = useState<LogEntry[]>([]);
  const [tasks,     setTasks]     = useState<TaskInfo[]>([]);
  const [detection, setDetection] = useState<DetectionInfo | null>(null);
  const [rangeScan, setRangeScan] = useState<RangeScanInfo | null>(null);

  const addLog = useCallback((type: string, text: string, level: LogEntry['level'] = 'info') => {
    setLog(prev => {
      const entry: LogEntry = { id: logId++, ts: Date.now(), type, text, level };
      return [entry, ...prev].slice(0, 30);
    });
  }, []);

  const connect = useCallback(() => {
    if (wsRef.current?.readyState === WebSocket.OPEN) return;

    setStatus('connecting');
    const ws = new WebSocket(url, 'ac2.v2');
    wsRef.current = ws;

    ws.onopen = () => {
      setStatus('connected');
      addLog('SYS', 'WebSocket connected', 'ok');
    };

    ws.onclose = () => {
      setStatus('disconnected');
      addLog('SYS', 'WebSocket disconnected — retrying in 3s', 'warn');
      timerRef.current = setTimeout(connect, 3000);
    };

    ws.onerror = () => {
      addLog('SYS', 'WebSocket error', 'error');
    };

    ws.onmessage = (evt) => {
      try {
        const envelope = JSON.parse(evt.data as string);
        const d = envelope.data ?? envelope;

        switch (d.type) {
          case 'evt.telemetry':
            setTelemetry({
              state:            d.state,
              cpu_load_x10:     d.cpu_load_x10,
              stack_uart_rx:    d.stack_uart_rx,
              stack_state_core: d.stack_state_core,
              stack_tel_tx:     d.stack_tel_tx,
              stack_heartbeat:  d.stack_heartbeat,
              hb_miss_count:    d.hb_miss_count,
            });
            break;

          case 'evt.heartbeat':
            setSysInfo(prev => ({ ...prev, uptime_ms: d.uptime_ms }));
            break;

          case 'evt.boot_report': {
            const reason = decodeBootReason(d.reset_reason_bits);
            setSysInfo(prev => ({
              ...prev,
              boot_reason: reason,
              boot_reason_bits: d.reset_reason_bits,
            }));
            addLog('BOOT', `${reason} rst=0x${d.reset_reason_bits.toString(16)}`, 'ok');
            break;
          }

          case 'evt.report_state':
            addLog('STATE',
              `${STATE_NAMES[d.prev_state] ?? d.prev_state} → ${STATE_NAMES[d.state] ?? d.state}`,
              'info');
            break;

          case 'evt.ack':
            addLog('ACK', `seq ${d.echoed_seq}`, 'ok');
            break;

          case 'evt.nack':
            addLog('NACK',
              `seq ${d.echoed_seq} — ${ERR_CODES[d.err_code] ?? `ERR ${d.err_code}`}`,
              'warn');
            break;

          case 'evt.version_report':
            setSysInfo(prev => ({
              ...prev,
              version: `${d.major}.${d.minor}.${d.patch}`,
              git_sha: (d.git_sha as string).slice(0, 8),
            }));
            addLog('VER', `v${d.major}.${d.minor}.${d.patch} @${(d.git_sha as string).slice(0,8)}`, 'info');
            break;

          case 'evt.fault_report':
            addLog('FAULT',
              `code=${d.fault_code} ctx=0x${d.ctx.toString(16)} rst=0x${d.reset_reason_bits.toString(16)}`,
              'error');
            break;

          case 'evt.audit_event': {
            const name = AUDIT_EVENTS[d.event_code] ?? `AUDIT ${d.event_code}`;
            const level: LogEntry['level'] =
              d.event_code === 6 || d.event_code === 7 ? 'warn' :
              d.event_code === 2 || d.event_code === 4 || d.event_code === 5 ? 'ok' : 'info';
            addLog('EDGE', `${name} value=${d.count}`, level);
            break;
          }

          case 'evt.task_list':
            setTasks((d.tasks as TaskInfo[]).map((task) => ({
              ...task,
              last_seen_ms: Date.now(),
            })));
            break;

          case 'evt.range_scan':
            setRangeScan({
              angle_deg: d.angle_deg,
              distance_cm: d.distance_cm,
              locked: d.locked,
              valid: d.valid,
              threshold_cm: d.threshold_cm,
              ts: Date.now(),
            });
            break;

          case 'evt.detection':
            setDetection({
              class_id: d.class_id,
              class_name: d.class_name,
              confidence: d.confidence,
              latency_ms: d.latency_ms,
              ts: Date.now(),
            });
            addLog('VISION',
              `${d.class_name} ${d.confidence}% (${d.latency_ms}ms)`,
              d.class_id === 1 ? 'ok' : 'info');
            break;

          case 'evt.error':
            addLog('ERR', d.message, 'error');
            break;
        }
      } catch {
        // ignore malformed
      }
    };
  }, [url, addLog]);

  useEffect(() => {
    connect();
    return () => {
      timerRef.current && clearTimeout(timerRef.current);
      wsRef.current?.close();
    };
  }, [connect]);

  const send = useCallback((cmd: OutCmd) => {
    if (wsRef.current?.readyState === WebSocket.OPEN) {
      const envelope = { v: 2, type: cmd.type, ts: Date.now(), seq: cmdSeq++, data: cmd };
      wsRef.current.send(JSON.stringify(envelope));
      if (cmd.type !== 'cmd.vision_frame') {
        addLog('UI', describeCommand(cmd), cmd.type === 'cmd.system_reset' ? 'warn' : 'info');
      }
    } else {
      addLog('UI', `dropped ${cmd.type}; websocket offline`, 'warn');
    }
  }, [addLog]);

  return { status, telemetry, sysInfo, log, tasks, detection, rangeScan, send };
}
