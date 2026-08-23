// Intel HEX encode/decode.

function hex2(v) { return (v & 0xFF).toString(16).toUpperCase().padStart(2, '0'); }

/** @param {Uint8Array} bytes @returns {string} */
export function toHex(bytes, { recordLen = 16 } = {}) {
  const lines = [];
  let upper = 0;  // no type-04 record until the address crosses 64 KB
  for (let addr = 0; addr < bytes.length; addr += recordLen) {
    const hi = addr >>> 16;
    if (hi !== upper) {
      upper = hi;
      const rec = [2, 0, 0, 4, (hi >> 8) & 0xFF, hi & 0xFF];
      lines.push(record(rec));
    }
    const n = Math.min(recordLen, bytes.length - addr);
    const lo = addr & 0xFFFF;
    const rec = [n, lo >> 8, lo & 0xFF, 0];
    for (let i = 0; i < n; i++) rec.push(bytes[addr + i]);
    lines.push(record(rec));
  }
  lines.push(':00000001FF');
  return lines.join('\r\n') + '\r\n';
}

function record(fields) {
  let sum = 0;
  let s = ':';
  for (const b of fields) { sum += b; s += hex2(b); }
  s += hex2((-sum) & 0xFF);
  return s;
}

/**
 * Parse Intel HEX into a contiguous image (gaps filled with 0xFF).
 * @param {string} text @returns {Uint8Array}
 */
export function fromHex(text) {
  const data = new Map(); // addr -> byte
  let base = 0, maxAddr = -1, gotEof = false, lineNo = 0;
  for (const raw of text.split(/\r?\n/)) {
    lineNo++;
    const line = raw.trim();
    if (line === '') continue;
    if (line[0] !== ':') throw new Error(`HEX line ${lineNo}: missing ':'`);
    if (line.length < 11 || (line.length - 1) % 2) throw new Error(`HEX line ${lineNo}: bad length`);
    const b = [];
    for (let i = 1; i < line.length; i += 2) {
      const v = parseInt(line.substr(i, 2), 16);
      if (Number.isNaN(v)) throw new Error(`HEX line ${lineNo}: bad hex digit`);
      b.push(v);
    }
    const sum = b.reduce((a, v) => (a + v) & 0xFF, 0);
    if (sum !== 0) throw new Error(`HEX line ${lineNo}: checksum mismatch`);
    const count = b[0], addr = (b[1] << 8) | b[2], type = b[3];
    if (b.length !== count + 5) throw new Error(`HEX line ${lineNo}: byte count mismatch`);
    const payload = b.slice(4, 4 + count);
    switch (type) {
      case 0x00:
        for (let i = 0; i < count; i++) {
          const a = base + addr + i;
          data.set(a, payload[i]);
          if (a > maxAddr) maxAddr = a;
        }
        break;
      case 0x01: gotEof = true; break;
      case 0x02: base = ((payload[0] << 8) | payload[1]) << 4; break;
      case 0x04: base = ((payload[0] << 8) | payload[1]) << 16; break;
      case 0x03: case 0x05: break;
      default: throw new Error(`HEX line ${lineNo}: unsupported record type ${type}`);
    }
    if (gotEof) break;
  }
  if (!gotEof) throw new Error('HEX: missing EOF record');
  const out = new Uint8Array(maxAddr + 1).fill(0xFF);
  for (const [a, v] of data) out[a] = v;
  return out;
}
