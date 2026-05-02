import { z } from 'zod';

// WebSocket envelope — all messages use this wrapper (IRS §6).
export const WsEnvelopeSchema = z.object({
  v:    z.literal(2),
  type: z.string(),
  ts:   z.number(),       // Unix ms
  seq:  z.number().int().nonnegative(),
  data: z.unknown(),
});
export type WsEnvelope = z.infer<typeof WsEnvelopeSchema>;

// ---- Host → Gateway commands ------------------------------------------------

export const CmdSetStateSchema = z.object({
  type:       z.literal('cmd.set_state'),
  targetState: z.enum(['idle', 'search', 'track', 'fail_safe']),
});

export const CmdManualLockSchema = z.object({
  type: z.literal('cmd.manual_lock'),
  lock: z.boolean(),
});

export const CmdGetVersionSchema = z.object({
  type: z.literal('cmd.get_version'),
});

export const CmdHeartbeatSchema = z.object({
  type: z.literal('cmd.heartbeat'),
});

export const CmdCreateTaskSchema = z.object({
  type:      z.literal('cmd.create_task'),
  task_type: z.number().int().min(0).max(3), // 0=BLINK, 1=COUNTER, 2=LOAD, 3=RANGE_SCAN
  param:     z.number().int().min(0).max(255),
});

export const CmdDeleteTaskSchema = z.object({
  type:       z.literal('cmd.delete_task'),
  slot_index: z.number().int().min(0).max(3),
});

export const CmdVisionFrameSchema = z.object({
  type:     z.literal('cmd.vision_frame'),
  jpeg_b64: z.string().min(1),
});

export const InboundCmdSchema = z.discriminatedUnion('type', [
  CmdSetStateSchema,
  CmdManualLockSchema,
  CmdGetVersionSchema,
  CmdHeartbeatSchema,
  CmdCreateTaskSchema,
  CmdDeleteTaskSchema,
  CmdVisionFrameSchema,
]);
export type InboundCmd = z.infer<typeof InboundCmdSchema>;

// ---- Gateway → Host events --------------------------------------------------

export interface EvtReportState {
  type:       'evt.report_state';
  state:      number;
  prev_state: number;
  uptime_ms:  number;
}

export interface EvtTelemetry {
  type:               'evt.telemetry';
  state:              number;
  cpu_load_x10:       number;
  stack_uart_rx:      number;
  stack_state_core:   number;
  stack_tel_tx:       number;
  stack_heartbeat:    number;
  hb_miss_count:      number;
}

export interface EvtFaultReport {
  type:              'evt.fault_report';
  fault_code:        number;
  ctx:               number;
  reset_reason_bits: number;
}

export interface EvtHeartbeat {
  type:      'evt.heartbeat';
  uptime_ms: number;
}

export interface EvtVersionReport {
  type:       'evt.version_report';
  major:      number;
  minor:      number;
  patch:      number;
  git_sha:    string;
  build_ts:   number;
}

export interface EvtNack {
  type:        'evt.nack';
  echoed_seq:  number;
  err_code:    number;
}

export interface EvtAck {
  type:       'evt.ack';
  echoed_seq: number;
}

export interface EvtError {
  type:    'evt.error';
  message: string;
}

export interface PackedTaskEntry {
  name:            string;
  state:           number;
  priority:        number;
  stack_watermark: number;
  cpu_load:        number;
  task_id:         number; // bit7=1 → user task, bits[2:0] = slot index
}

export interface EvtTaskList {
  type:  'evt.task_list';
  tasks: PackedTaskEntry[];
}

export interface EvtDetection {
  type:       'evt.detection';
  class_id:   number;
  class_name: string;
  confidence: number;
  latency_ms: number;
}

export type OutboundEvent =
  | EvtReportState
  | EvtTelemetry
  | EvtFaultReport
  | EvtHeartbeat
  | EvtVersionReport
  | EvtNack
  | EvtAck
  | EvtError
  | EvtTaskList
  | EvtDetection;

/** Wrap an event in the standard envelope. */
export function envelope(seq: number, event: OutboundEvent): WsEnvelope {
  return { v: 2, type: event.type, ts: Date.now(), seq, data: event };
}
