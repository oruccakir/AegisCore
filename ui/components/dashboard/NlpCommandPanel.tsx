'use client';

import { useEffect, useState } from 'react';
import type { OutCmd, TaskInfo } from '@/hooks/useAC2Socket';
import {
  DEFAULT_NLP_MODEL,
  DEFAULT_OLLAMA_URL,
  type NlpCommandResponse,
  type NlpProvider,
  postJson,
} from '@/lib/inferenceClient';
import styles from '@/app/page.module.css';

interface Props {
  connected: boolean;
  tasks: TaskInfo[];
  send: (cmd: OutCmd) => void;
}

export default function NlpCommandPanel({ connected, tasks, send }: Props) {
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
        active_tasks: tasks.map((task) => ({
          name: task.name,
          task_id: task.task_id,
        })),
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
            onChange={(evt) => setProvider(evt.target.value as NlpProvider)}
          >
            <option value="ollama">OLLAMA</option>
            <option value="gemini">GEMINI</option>
          </select>
        </label>

        <label className={styles.nlpField}>
          <span>MODEL</span>
          <select className={styles.nlpSelect} value={model} onChange={(evt) => setModel(evt.target.value)}>
            {models.map((name) => (
              <option key={name} value={name}>
                {name}
              </option>
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
        placeholder="START RANGE SCAN AT 40 CM / STOP RANGE SCAN / GET VERSION"
        rows={4}
      />

      <div className={styles.nlpStatusRow}>
        <span className={styles.nlpStatus} data-action={lastAction ?? 'none'}>
          {statusText}
        </span>
      </div>

      <div className={styles.btnRow}>
        <button className={`${styles.btn} ${styles.btnSecondary}`} disabled={loadingModels} onClick={loadModels}>
          MODELS
        </button>
        <button
          className={`${styles.btn} ${styles.btnSecondary}`}
          disabled={!text.trim() || busy}
          onClick={submitCommand}
        >
          SEND
        </button>
      </div>
    </div>
  );
}
