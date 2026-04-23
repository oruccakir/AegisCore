import { SerialPort }       from 'serialport';
import { EventEmitter }     from 'node:events';
import { log }              from '../config.js';
import { AC2Parser, AC2Frame } from './AC2Parser.js';

export class SerialBridge extends EventEmitter {
  private port:   SerialPort;
  private parser: AC2Parser;

  constructor(path: string, baudRate: number) {
    super();
    this.parser = new AC2Parser();
    this.parser.onFrame((frame) => this.emit('frame', frame));

    this.port = new SerialPort({ path, baudRate, autoOpen: false });

    this.port.on('data', (chunk: Buffer) => {
      this.parser.feedBuffer(chunk);
    });

    this.port.on('error', (err: Error) => {
      log('error', 'SerialPort error', err.message);
      this.emit('error', err);
    });

    this.port.on('close', () => {
      log('warn', 'SerialPort closed');
      this.emit('close');
    });
  }

  open(): Promise<void> {
    return new Promise((resolve, reject) => {
      this.port.open((err) => {
        if (err) { reject(err); return; }
        log('info', `Serial port opened`, { path: this.port.path });
        resolve();
      });
    });
  }

  close(): Promise<void> {
    return new Promise((resolve) => {
      if (!this.port.isOpen) { resolve(); return; }
      this.port.close(() => resolve());
    });
  }

  write(frame: Buffer): void {
    log('debug', 'serial TX', { bytes: frame.length, cmd: `0x${frame[7]!.toString(16).padStart(2,'0')}` });
    this.port.write(frame, (err) => {
      if (err) log('error', 'Serial write error', err.message);
      else     log('debug', 'serial TX done');
    });
  }

  get crcErrors(): number  { return this.parser.crcErrors; }
  get frameCount(): number { return this.parser.frameCount; }
}

export type { AC2Frame };
