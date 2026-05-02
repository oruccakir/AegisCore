import type { OutCmd } from '@/hooks/useAC2Socket';

export type NlpProvider = 'gemini' | 'ollama';

export const DEFAULT_NLP_MODEL: Record<NlpProvider, string> = {
  gemini: 'gemini-3-flash-preview',
  ollama: 'llama3.2:latest',
};

export const DEFAULT_OLLAMA_URL = 'http://127.0.0.1:11434';

const INFERENCE_BASE_URL = process.env.NEXT_PUBLIC_INFERENCE_URL ?? 'http://127.0.0.1:7979';

export interface NlpCommandResponse {
  provider: NlpProvider;
  model: string;
  action: 'get_version' | 'manual_lock' | 'create_task' | 'delete_task' | 'unsupported';
  safe_to_send: boolean;
  gateway_command: OutCmd | null;
  confidence: number;
  reason: string;
}

export async function postJson<T>(path: string, body: Record<string, unknown>): Promise<T> {
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
