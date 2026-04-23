// Runtime configuration — override via environment variables.
export const Config = {
  serialPort:   process.env['SERIAL_PORT']   ?? '/dev/ttyUSB0',
  serialBaud:   Number(process.env['SERIAL_BAUD'] ?? '115200'),
  wsPort:       Number(process.env['WS_PORT']  ?? '8443'),
  wsHost:       process.env['WS_HOST']       ?? '0.0.0.0',

  // Pre-shared key (hex string, 16 bytes = 32 hex chars).
  // Must match kPsk[] in edge/app/main.cpp.
  pskHex:       process.env['AC2_PSK'] ??
                'DEADBEEFCAFEBABE0123456789ABCDEF',

  heartbeatMs:  Number(process.env['HEARTBEAT_MS'] ?? '1000'),
  logLevel:     process.env['LOG_LEVEL'] ?? 'info',
} as const;

export type LogLevel = 'debug' | 'info' | 'warn' | 'error';

export function log(level: LogLevel, msg: string, data?: unknown): void {
  const levels: Record<LogLevel, number> = { debug: 0, info: 1, warn: 2, error: 3 };
  const configured = (levels[Config.logLevel as LogLevel] ?? 1);
  if (levels[level] < configured) return;

  const ts  = new Date().toISOString();
  const line = data !== undefined
    ? `[${ts}] ${level.toUpperCase()} ${msg} ${JSON.stringify(data)}`
    : `[${ts}] ${level.toUpperCase()} ${msg}`;
  if (level === 'error') {
    process.stderr.write(line + '\n');
  } else {
    process.stdout.write(line + '\n');
  }
}
