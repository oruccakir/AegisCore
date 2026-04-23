// CRC-16/CCITT-FALSE: poly=0x1021, init=0xFFFF, no reflection, xorout=0x0000.
// Matches the firmware implementation in edge/app/crc16.cpp.

const TABLE = (() => {
  const t = new Uint16Array(256);
  for (let i = 0; i < 256; i++) {
    let crc = i << 8;
    for (let j = 0; j < 8; j++) {
      crc = (crc & 0x8000) ? ((crc << 1) ^ 0x1021) : (crc << 1);
    }
    t[i] = crc & 0xffff;
  }
  return t;
})();

export function crc16(buf: Uint8Array, len = buf.length): number {
  let crc = 0xffff;
  for (let i = 0; i < len; i++) {
    const b = buf[i];
    if (b === undefined) break;
    crc = ((crc << 8) ^ (TABLE[((crc >> 8) ^ b) & 0xff]!)) & 0xffff;
  }
  return crc;
}
