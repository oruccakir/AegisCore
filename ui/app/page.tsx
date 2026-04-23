'use client';
import { useAC2Socket } from '@/hooks/useAC2Socket';
import RadarDisplay from '@/components/RadarDisplay';
import EventLog from '@/components/EventLog';
import styles from './page.module.css';

const STATE_NAMES  = ['IDLE', 'SEARCH', 'TRACK', 'FAIL_SAFE'];
const STATE_COLORS = ['#4a8a54', '#00ff41', '#ffaa00', '#ff2222'];
const STATE_DESC   = [
  'System standing by. No active scan.',
  'Scanning sector. Awaiting target acquisition.',
  'Target acquired. Tracking in progress.',
  'FAIL-SAFE ENGAGED. System locked.',
];

function fmtUptime(ms: number) {
  const s = Math.floor(ms / 1000);
  const m = Math.floor(s / 60);
  const h = Math.floor(m / 60);
  return `${String(h).padStart(2,'0')}:${String(m%60).padStart(2,'0')}:${String(s%60).padStart(2,'0')}`;
}

function ConnDot({ status }: { status: string }) {
  const color = status === 'connected' ? '#00ff41' : status === 'connecting' ? '#ffaa00' : '#ff2222';
  return (
    <span style={{ display:'inline-block', width:8, height:8, borderRadius:'50%',
      background: color, boxShadow: `0 0 6px ${color}`,
      animation: status === 'connecting' ? 'blink 1s infinite' : undefined }} />
  );
}

export default function Dashboard() {
  const { status, telemetry, sysInfo, log, send } = useAC2Socket('ws://localhost:8443');

  const state     = telemetry?.state ?? 0;
  const stateColor = STATE_COLORS[state] ?? '#4a8a54';
  const isFailSafe = state === 3;
  const isSafe     = !isFailSafe && status === 'connected';

  function setSystemState(s: 'idle' | 'search' | 'track') {
    send({ type: 'cmd.set_state', targetState: s });
  }

  return (
    <div className={styles.root}>

      {/* ── TOP BAR ── */}
      <header className={styles.topbar}>
        <div className={styles.logo}>
          <span className={styles.logoAegis}>AEGIS</span>
          <span className={styles.logoCore}>CORE</span>
          <span className={styles.logoCci}>CCI v2</span>
        </div>

        <div className={styles.stateChip} style={{ borderColor: stateColor, color: stateColor,
          boxShadow: `0 0 10px ${stateColor}33` }}>
          <span className={styles.stateDot} style={{ background: stateColor, boxShadow: `0 0 6px ${stateColor}` }} />
          {STATE_NAMES[state]}
        </div>

        <div className={styles.connRow}>
          <ConnDot status={status} />
          <span style={{ color: status === 'connected' ? 'var(--text-dim)' : '#ffaa00',
            marginLeft: 6, fontSize: 11, letterSpacing: '0.1em' }}>
            {status.toUpperCase()}
          </span>
          {sysInfo.version && (
            <span style={{ marginLeft: 12, color: 'var(--text-faint)', fontSize: 10 }}>
              fw {sysInfo.version}
            </span>
          )}
        </div>
      </header>

      {/* ── MAIN ── */}
      <main className={styles.main}>

        {/* LEFT — radar + state description */}
        <section className={styles.left}>
          <div className={styles.radarWrap}>
            <RadarDisplay state={state} />
          </div>

          <div className={styles.stateDesc} style={{ borderColor: `${stateColor}44` }}>
            <div className={styles.stateDescTitle} style={{ color: stateColor }}>
              {STATE_NAMES[state]}
            </div>
            <div className={styles.stateDescText}>{STATE_DESC[state]}</div>
          </div>
        </section>

        {/* CENTER — telemetry + controls */}
        <section className={styles.center}>

          {/* TELEMETRY */}
          <div className={styles.panel}>
            <div className={styles.panelTitle}>TELEMETRY</div>
            <div className={styles.metrics}>
              <Metric label="UPTIME"      value={fmtUptime(sysInfo.uptime_ms)} />
              <Metric label="CPU LOAD"    value={telemetry ? `${(telemetry.cpu_load_x10 / 10).toFixed(1)}%` : '—'} />
              <Metric label="STACK FREE"  value={telemetry ? `${telemetry.free_stack_min_words}w` : '—'} />
              <Metric label="HB MISS"
                value={telemetry ? String(telemetry.hb_miss_count) : '—'}
                warn={(telemetry?.hb_miss_count ?? 0) > 0} />
            </div>
          </div>

          {/* CONTROLS */}
          <div className={styles.panel}>
            <div className={styles.panelTitle}>STATE CONTROL</div>
            <div className={styles.btnGroup}>
              <StateBtn label="IDLE"   active={state===0}
                disabled={!isSafe || state===0}
                color="#4a8a54"
                onClick={() => setSystemState('idle')} />
              <StateBtn label="SEARCH" active={state===1}
                disabled={!isSafe || state===1}
                color="#00ff41"
                onClick={() => setSystemState('search')} />
              <StateBtn label="TRACK"  active={state===2}
                disabled={!isSafe || state===2 || state===0}
                color="#ffaa00"
                onClick={() => setSystemState('track')} />
            </div>

            <div className={styles.divider} />

            <div className={styles.btnRow}>
              <button className={`${styles.btn} ${styles.btnSecondary}`}
                disabled={!isSafe}
                onClick={() => send({ type: 'cmd.manual_lock', lock: true })}>
                MANUAL LOCK
              </button>
              <button className={`${styles.btn} ${styles.btnSecondary}`}
                onClick={() => send({ type: 'cmd.get_version' })}>
                GET VERSION
              </button>
            </div>
          </div>

          {/* FAIL-SAFE BANNER */}
          {isFailSafe && (
            <div className={styles.failBanner}>
              ⚠ FAIL-SAFE ENGAGED — SYSTEM LOCKED
            </div>
          )}
          {status === 'disconnected' && (
            <div className={styles.warnBanner}>
              ● GATEWAY DISCONNECTED — RECONNECTING…
            </div>
          )}
        </section>

        {/* RIGHT — event log */}
        <section className={styles.right}>
          <EventLog entries={log} />
        </section>

      </main>

      {/* ── FOOTER ── */}
      <footer className={styles.footer}>
        <span>STM32F407G-DISC1 · AC2 v2 · 115200 8N1</span>
        {sysInfo.git_sha && <span>git:{sysInfo.git_sha}</span>}
        <span style={{ marginLeft: 'auto', color: 'var(--text-faint)' }}>
          seq {log[0]?.id ?? 0}
        </span>
      </footer>
    </div>
  );
}

function Metric({ label, value, warn }: { label: string; value: string; warn?: boolean }) {
  return (
    <div className={styles.metric}>
      <span className={styles.metricLabel}>{label}</span>
      <span className={styles.metricValue} style={{ color: warn ? 'var(--amber)' : undefined }}>
        {value}
      </span>
    </div>
  );
}

function StateBtn({ label, active, disabled, color, onClick }:
  { label: string; active: boolean; disabled: boolean; color: string; onClick: () => void }) {
  return (
    <button
      className={`${styles.stateBtn} ${active ? styles.stateBtnActive : ''}`}
      style={active ? { borderColor: color, color, boxShadow: `0 0 12px ${color}44` } : undefined}
      disabled={disabled}
      onClick={onClick}>
      {active && <span className={styles.activeDot} style={{ background: color }} />}
      {label}
    </button>
  );
}
