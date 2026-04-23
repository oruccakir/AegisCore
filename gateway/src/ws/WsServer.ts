import { WebSocketServer, WebSocket } from 'ws';
import { EventEmitter }              from 'node:events';
import { log }                       from '../config.js';
import {
  WsEnvelopeSchema,
  InboundCmdSchema,
  OutboundEvent,
  envelope,
} from './schemas.js';

const SUBPROTOCOL = 'ac2.v2';

export class WsServer extends EventEmitter {
  private wss:    WebSocketServer;
  private outSeq = 0;

  constructor(host: string, port: number) {
    super();
    this.wss = new WebSocketServer({ host, port });

    this.wss.on('listening', () => {
      log('info', `WebSocket server listening`, { host, port });
    });

    this.wss.on('connection', (ws: WebSocket, req) => {
      const ip = req.socket.remoteAddress ?? 'unknown';
      log('info', 'WS client connected', { ip });

      // Reject clients that don't speak ac2.v2
      if (ws.protocol !== SUBPROTOCOL) {
        log('warn', 'WS client rejected: wrong subprotocol', { protocol: ws.protocol });
        ws.close(1002, 'subprotocol must be ac2.v2');
        return;
      }

      ws.on('message', (raw) => {
        this.handleMessage(raw.toString(), ws);
      });

      ws.on('close', () => {
        log('info', 'WS client disconnected', { ip });
      });

      ws.on('error', (err) => {
        log('error', 'WS client error', err.message);
      });
    });

    this.wss.on('error', (err) => {
      log('error', 'WsServer error', err.message);
    });
  }

  /** Broadcast an outbound event to all connected clients. */
  broadcast(event: OutboundEvent): void {
    const msg = JSON.stringify(envelope(this.outSeq++, event));
    for (const client of this.wss.clients) {
      if (client.readyState === WebSocket.OPEN) {
        client.send(msg);
      }
    }
  }

  close(): Promise<void> {
    return new Promise((resolve) => this.wss.close(() => resolve()));
  }

  private handleMessage(raw: string, _ws: WebSocket): void {
    let json: unknown;
    try {
      json = JSON.parse(raw);
    } catch {
      log('warn', 'WS: non-JSON message ignored');
      return;
    }

    const envResult = WsEnvelopeSchema.safeParse(json);
    if (!envResult.success) {
      log('warn', 'WS: invalid envelope', envResult.error.issues);
      return;
    }

    const cmdResult = InboundCmdSchema.safeParse(envResult.data.data);
    if (!cmdResult.success) {
      log('warn', 'WS: unrecognised command', cmdResult.error.issues);
      return;
    }

    this.emit('command', cmdResult.data, envResult.data.seq);
  }
}
