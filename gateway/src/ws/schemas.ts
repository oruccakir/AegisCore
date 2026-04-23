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

export const InboundCmdSchema = z.discriminatedUnion('type', [
  CmdSetStateSchema,
  CmdManualLockSchema,
  CmdGetVersionSchema,
  CmdHeartbeatSchema,
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
  free_stack_min_words: number;
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

export type OutboundEvent =
  | EvtReportState
  | EvtTelemetry
  | EvtFaultReport
  | EvtHeartbeat
  | EvtVersionReport
  | EvtNack
  | EvtAck
  | EvtError;

/** Wrap an event in the standard envelope. */
export function envelope(seq: number, event: OutboundEvent): WsEnvelope {
  return { v: 2, type: event.type, ts: Date.now(), seq, data: event };
}
