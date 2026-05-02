'use client';
import { useEffect, useRef, useState } from 'react';
import { DetectionInfo, OutCmd, useAC2Socket } from '@/hooks/useAC2Socket';
import RadarDisplay from '@/components/RadarDisplay';
import EventLog from '@/components/EventLog';
import TaskMonitor from '@/components/TaskMonitor';
import styles from './page.module.css';

const STATE_NAMES  = ['IDLE', 'SEARCH', 'TRACK', 'FAIL_SAFE'];
const STATE_COLORS = ['#4a8a54', '#00ff41', '#ffaa00', '#ff2222'];
const STATE_DESC   = [
  'System standing by. No active scan.',
  'Scanning sector. Awaiting target acquisition.',
  'Target acquired. Tracking in progress.',
  'FAIL-SAFE ENGAGED. System locked.',
];
const INFERENCE_BASE_URL = process.env.NEXT_PUBLIC_INFERENCE_URL ?? 'http://127.0.0.1:7979';
const DEFAULT_NLP_MODEL: Record<NlpProvider, string> = {
  gemini: 'gemini-3-flash-preview',
  ollama: 'llama3.2:latest',
};
const DEFAULT_OLLAMA_URL = 'http://127.0.0.1:11434';

type NlpProvider = 'gemini' | 'ollama';

interface NlpCommandResponse {
  provider: NlpProvider;
  model: string;
  action: 'get_version' | 'manual_lock' | 'unsupported';
  safe_to_send: boolean;
  gateway_command: OutCmd | null;
  confidence: number;
  reason: string;
}

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
  const { status, telemetry, sysInfo, log, tasks, detection, rangeScan, send } = useAC2Socket('ws://localhost:8443');

  const state     = telemetry?.state ?? 0;
  const stateColor = STATE_COLORS[state] ?? '#4a8a54';
  const isFailSafe = state === 3;
  const isSafe     = !isFailSafe && status === 'connected';
  const rangeScanActive = tasks.some((task) => task.name.startsWith('RngS'));

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
            <RadarDisplay
              state={state}
              rangeScan={rangeScanActive ? rangeScan : null}
              rangeScanActive={rangeScanActive}
            />
          </div>

          <NlpCommandPanel
            connected={status === 'connected'}
            send={send}
          />

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
              <Metric label="STACK MIN"   value={telemetry ? `${Math.min(telemetry.stack_uart_rx, telemetry.stack_state_core, telemetry.stack_tel_tx, telemetry.stack_heartbeat)}w` : '—'} />
              <Metric label="HB MISS"
                value={telemetry ? String(telemetry.hb_miss_count) : '—'}
                warn={(telemetry?.hb_miss_count ?? 0) > 0} />
            </div>
          </div>

          <VisionPanel
            detection={detection}
            connected={status === 'connected'}
            send={send}
          />

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

          {/* TASK MONITOR */}
          <TaskMonitor tasks={tasks} send={send} />

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

function NlpCommandPanel({ connected, send }: {
  connected: boolean;
  send: (cmd: OutCmd) => void;
}) {
  const [provider, setProvider] = useState<NlpProvider>('ollama');
  const [models, setModels] = useState<string[]>([DEFAULT_NLP_MODEL.ollama]);
  const [model, setModel] = useState(DEFAULT_NLP_MODEL.ollama);
  const [apiKey, setApiKey] = useState('');
  const [ollamaUrl, setOllamaUrl] = useState(DEFAULT_OLLAMA_URL);
  const [text, setText] = useState('');
  const [busy, setBusy] = useState(false);
  const [loadingModels, setLoadingModels] = useState(false);
  const [statusText, setStatusText] = useState('READY');
  const [lastAction, setLastAction] = useState<NlpCommandResponse['action'] | null>(null);

  useEffect(() => {
    const fallback = DEFAULT_NLP_MODEL[provider];
    setModels([fallback]);
    setModel(fallback);
    setStatusText('READY');
  }, [provider]);

  async function loadModels() {
    setLoadingModels(true);
    setStatusText('LOADING MODELS');
    try {
      const response = await postJson<{ models: string[] }>('/nlp/models', {
        provider,
        api_key: provider === 'gemini' && apiKey ? apiKey : undefined,
        ollama_url: provider === 'ollama' ? ollamaUrl : undefined,
      });
      const nextModels = response.models.length > 0 ? response.models : [DEFAULT_NLP_MODEL[provider]];
      setModels(nextModels);
      setModel(nextModels[0]);
      setStatusText(`${nextModels.length} MODEL${nextModels.length === 1 ? '' : 'S'}`);
    } catch (err) {
      setStatusText(err instanceof Error ? err.message.toUpperCase().slice(0, 48) : 'MODEL LOAD FAILED');
      setModels([DEFAULT_NLP_MODEL[provider]]);
      setModel(DEFAULT_NLP_MODEL[provider]);
    } finally {
      setLoadingModels(false);
    }
  }

  async function submitCommand() {
    const trimmed = text.trim();
    if (!trimmed || !model || busy) return;

    setBusy(true);
    setLastAction(null);
    setStatusText('CLASSIFYING');
    try {
      const result = await postJson<NlpCommandResponse>('/nlp/command', {
        text: trimmed,
        provider,
        model,
        api_key: provider === 'gemini' && apiKey ? apiKey : undefined,
        ollama_url: provider === 'ollama' ? ollamaUrl : undefined,
      });
      setLastAction(result.action);

      if (!result.safe_to_send || !result.gateway_command) {
        setStatusText(`BLOCKED ${Math.round(result.confidence * 100)}%`);
        return;
      }

      if (!connected) {
        setStatusText('GATEWAY OFFLINE');
        return;
      }

      send(result.gateway_command);
      setStatusText(`SENT ${result.action.toUpperCase()} ${Math.round(result.confidence * 100)}%`);
      setText('');
    } catch (err) {
      setStatusText(err instanceof Error ? err.message.toUpperCase().slice(0, 48) : 'NLP FAILED');
    } finally {
      setBusy(false);
    }
  }

  return (
    <div className={`${styles.panel} ${styles.nlpPanel}`}>
      <div className={styles.panelTitle}>AI COMMAND</div>

      <div className={styles.nlpGrid}>
        <label className={styles.nlpField}>
          <span>PROVIDER</span>
          <select
            className={styles.nlpSelect}
            value={provider}
            onChange={(evt) => setProvider(evt.target.value as NlpProvider)}>
            <option value="ollama">OLLAMA</option>
            <option value="gemini">GEMINI</option>
          </select>
        </label>

        <label className={styles.nlpField}>
          <span>MODEL</span>
          <select
            className={styles.nlpSelect}
            value={model}
            onChange={(evt) => setModel(evt.target.value)}>
            {models.map((name) => (
              <option key={name} value={name}>{name}</option>
            ))}
          </select>
        </label>
      </div>

      {provider === 'gemini' ? (
        <input
          className={styles.nlpInput}
          type="password"
          value={apiKey}
          onChange={(evt) => setApiKey(evt.target.value)}
          placeholder="GEMINI API KEY"
          autoComplete="off"
        />
      ) : (
        <input
          className={styles.nlpInput}
          value={ollamaUrl}
          onChange={(evt) => setOllamaUrl(evt.target.value)}
          placeholder="OLLAMA URL"
          autoComplete="off"
        />
      )}

      <textarea
        className={styles.nlpText}
        value={text}
        onChange={(evt) => setText(evt.target.value)}
        placeholder="GET VERSION / MANUAL LOCK"
        rows={4}
      />

      <div className={styles.nlpStatusRow}>
        <span className={styles.nlpStatus} data-action={lastAction ?? 'none'}>{statusText}</span>
      </div>

      <div className={styles.btnRow}>
        <button
          className={`${styles.btn} ${styles.btnSecondary}`}
          disabled={loadingModels}
          onClick={loadModels}>
          MODELS
        </button>
        <button
          className={`${styles.btn} ${styles.btnSecondary}`}
          disabled={!text.trim() || busy}
          onClick={submitCommand}>
          SEND
        </button>
      </div>
    </div>
  );
}

function VisionPanel({ detection, connected, send }: {
  detection: DetectionInfo | null;
  connected: boolean;
  send: (cmd: OutCmd) => void;
}) {
  const videoRef = useRef<HTMLVideoElement | null>(null);
  const canvasRef = useRef<HTMLCanvasElement | null>(null);
  const streamRef = useRef<MediaStream | null>(null);
  const [active, setActive] = useState(false);
  const [cameraError, setCameraError] = useState<string | null>(null);

  useEffect(() => {
    return () => {
      streamRef.current?.getTracks().forEach(track => track.stop());
    };
  }, []);

  useEffect(() => {
    if (!active || !connected) return;

    const interval = window.setInterval(() => {
      const video = videoRef.current;
      const canvas = canvasRef.current;
      if (!video || !canvas || video.readyState < HTMLMediaElement.HAVE_CURRENT_DATA) return;

      const ctx = canvas.getContext('2d');
      if (!ctx) return;

      canvas.width = 320;
      canvas.height = 240;
      ctx.drawImage(video, 0, 0, canvas.width, canvas.height);
      const jpegB64 = canvas.toDataURL('image/jpeg', 0.55).split(',')[1];
      if (jpegB64) send({ type: 'cmd.vision_frame', jpeg_b64: jpegB64 });
    }, 500);

    return () => window.clearInterval(interval);
  }, [active, connected, send]);

  async function startCamera() {
    setCameraError(null);
    try {
      const stream = await navigator.mediaDevices.getUserMedia({
        video: { width: 640, height: 480, facingMode: 'environment' },
        audio: false,
      });
      streamRef.current = stream;
      if (videoRef.current) {
        videoRef.current.srcObject = stream;
        await videoRef.current.play();
      }
      setActive(true);
    } catch (err) {
      setCameraError(err instanceof Error ? err.message : 'camera unavailable');
      setActive(false);
    }
  }

  function stopCamera() {
    streamRef.current?.getTracks().forEach(track => track.stop());
    streamRef.current = null;
    if (videoRef.current) videoRef.current.srcObject = null;
    setActive(false);
  }

  const detected = detection?.class_id === 1;
  const label = detection ? detection.class_name.toUpperCase() : 'NO SAMPLE';
  const confidence = detection ? `${detection.confidence}%` : '--';

  return (
    <div className={styles.panel}>
      <div className={styles.panelTitle}>VISION</div>
      <div className={styles.visionGrid}>
        <div className={styles.videoShell}>
          <video ref={videoRef} className={styles.video} muted playsInline />
          <canvas ref={canvasRef} className={styles.hiddenCanvas} />
          {!active && <div className={styles.videoPlaceholder}>CAMERA OFF</div>}
        </div>
        <div className={styles.visionReadout}>
          <div className={styles.detectionLabel} data-detected={detected}>
            {label}
          </div>
          <div className={styles.detectionMeta}>
            CONF {confidence}
            {detection && <span> · {detection.latency_ms}ms</span>}
          </div>
          {cameraError && <div className={styles.cameraError}>{cameraError}</div>}
          <div className={styles.btnRow}>
            <button
              className={`${styles.btn} ${styles.btnSecondary}`}
              disabled={!connected || active}
              onClick={startCamera}>
              START
            </button>
            <button
              className={`${styles.btn} ${styles.btnSecondary}`}
              disabled={!active}
              onClick={stopCamera}>
              STOP
            </button>
          </div>
        </div>
      </div>
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

async function postJson<T>(path: string, body: Record<string, unknown>): Promise<T> {
  const response = await fetch(`${INFERENCE_BASE_URL}${path}`, {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify(body),
  });

  if (!response.ok) {
    let detail = `HTTP ${response.status}`;
    try {
      const errorBody = await response.json() as { detail?: unknown };
      if (typeof errorBody.detail === 'string') detail = errorBody.detail;
    } catch {
      // Keep the HTTP fallback.
    }
    throw new Error(detail);
  }

  return response.json() as Promise<T>;
}
