import type { SystemInfo, Telemetry } from '@/hooks/useAC2Socket';
import { fmtUptime } from '@/lib/systemState';
import Metric from '@/components/ui/Metric';
import styles from '@/app/page.module.css';

interface Props {
  telemetry: Telemetry | null;
  sysInfo: SystemInfo;
}

export default function TelemetryPanel({ telemetry, sysInfo }: Props) {
  const stackMin = telemetry
    ? Math.min(telemetry.stack_uart_rx, telemetry.stack_state_core, telemetry.stack_tel_tx, telemetry.stack_heartbeat)
    : null;

  return (
    <div className={styles.panel}>
      <div className={styles.panelTitle}>TELEMETRY</div>
      <div className={styles.metrics}>
        <Metric label="UPTIME" value={fmtUptime(sysInfo.uptime_ms)} />
        <Metric label="CPU LOAD" value={telemetry ? `${(telemetry.cpu_load_x10 / 10).toFixed(1)}%` : '-'} />
        <Metric label="STACK MIN" value={stackMin !== null ? `${stackMin}w` : '-'} />
        <Metric
          label="HB MISS"
          value={telemetry ? String(telemetry.hb_miss_count) : '-'}
          warn={(telemetry?.hb_miss_count ?? 0) > 0}
        />
      </div>
    </div>
  );
}
