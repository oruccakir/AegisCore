import { crc16 }                     from './Crc16.js';
import { MAX_FRAME, MAX_PAYLOAD, SYNC, VERSION, OVERHEAD } from './AC2Framer.js';

export interface AC2Frame {
  seq:        number;
  cmd:        number;
  payload:    Buffer;
  hmac:       Buffer;
  crcOk:      boolean;
}

const HMAC_LEN = 8;
const HEADER   = 8; // SYNC+VER+SEQ(4)+LEN+CMD

const enum St {
  WaitSync, WaitVersion,
  WaitSeq0, WaitSeq1, WaitSeq2, WaitSeq3,
  WaitLength, WaitCmd,
  WaitPayload, WaitHmac, WaitCrc0, WaitCrc1,
}

export class AC2Parser {
  private state: St = St.WaitSync;

  private seq         = 0;
  private cmd         = 0;
  private payloadLen  = 0;
  private payloadIdx  = 0;
  private hmacIdx     = 0;

  private readonly raw     = Buffer.allocUnsafe(MAX_FRAME);
  private rawLen           = 0;
  private readonly payload = Buffer.allocUnsafe(MAX_PAYLOAD);
  private readonly hmac    = Buffer.allocUnsafe(HMAC_LEN);
  private rxCrcLo          = 0;

  crcErrors   = 0;
  frameCount  = 0;

  private cb: ((frame: AC2Frame) => void) | null = null;

  onFrame(cb: (frame: AC2Frame) => void): void { this.cb = cb; }

  reset(): void {
    this.state      = St.WaitSync;
    this.rawLen     = 0;
    this.payloadIdx = 0;
    this.hmacIdx    = 0;
  }

  feed(byte: number): void {
    switch (this.state) {
      case St.WaitSync:
        if (byte === SYNC) { this.rawLen = 0; this.push(byte); this.state = St.WaitVersion; }
        break;

      case St.WaitVersion:
        if (byte === VERSION) { this.push(byte); this.state = St.WaitSeq0; }
        else this.reset();
        break;

      case St.WaitSeq0:
        this.seq = byte; this.push(byte); this.state = St.WaitSeq1;
        break;
      case St.WaitSeq1:
        this.seq |= byte << 8; this.push(byte); this.state = St.WaitSeq2;
        break;
      case St.WaitSeq2:
        this.seq |= byte << 16; this.push(byte); this.state = St.WaitSeq3;
        break;
      case St.WaitSeq3:
        this.seq = ((this.seq | (byte << 24)) >>> 0); this.push(byte); this.state = St.WaitLength;
        break;

      case St.WaitLength:
        if (byte > MAX_PAYLOAD) { this.reset(); break; } // SR-08
        this.payloadLen = byte; this.payloadIdx = 0; this.push(byte);
        this.state = St.WaitCmd;
        break;

      case St.WaitCmd:
        this.cmd = byte; this.push(byte);
        this.state = this.payloadLen > 0 ? St.WaitPayload : St.WaitHmac;
        break;

      case St.WaitPayload:
        this.payload[this.payloadIdx++] = byte;
        this.push(byte);
        if (this.payloadIdx >= this.payloadLen) { this.hmacIdx = 0; this.state = St.WaitHmac; }
        break;

      case St.WaitHmac:
        this.hmac[this.hmacIdx++] = byte;
        this.push(byte);
        if (this.hmacIdx >= HMAC_LEN) this.state = St.WaitCrc0;
        break;

      case St.WaitCrc0:
        this.rxCrcLo = byte;
        this.state   = St.WaitCrc1;
        break;

      case St.WaitCrc1: {
        const rxCrc   = this.rxCrcLo | (byte << 8);
        const calcCrc = crc16(this.raw, this.rawLen);
        const ok      = rxCrc === calcCrc;
        if (!ok) this.crcErrors++;
        this.frameCount++;
        this.dispatch(ok);
        this.reset();
        break;
      }
    }
  }

  feedBuffer(buf: Buffer | Uint8Array): void {
    for (let i = 0; i < buf.length; i++) this.feed(buf[i]!);
  }

  private push(byte: number): void {
    if (this.rawLen < MAX_FRAME) this.raw[this.rawLen++] = byte;
  }

  private dispatch(crcOk: boolean): void {
    if (!this.cb) return;
    const frame: AC2Frame = {
      seq:     this.seq,
      cmd:     this.cmd,
      payload: Buffer.from(this.payload.subarray(0, this.payloadLen)),
      hmac:    Buffer.from(this.hmac.subarray(0, HMAC_LEN)),
      crcOk,
    };
    this.cb(frame);
  }
}
