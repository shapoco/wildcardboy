// Minimal CBOR codec for card profiles (see SPEC.md "カードプロファイルチップ").
//
// encode(): definite-length only; integers (major 0/1), text strings
// (major 3), arrays (major 4), maps with text keys (major 5, insertion
// order). Anything else (bool, null, float, non-integer) throws, so a
// profile that the firmware cannot parse is never produced.
//
// decode(): accepts the above plus byte strings, indefinite lengths and the
// simple values true/false/null, so foreign images still load.

const enc = new TextEncoder();
const dec = new TextDecoder('utf-8', { fatal: true });

class Writer {
  constructor() { this.buf = new Uint8Array(1024); this.len = 0; }
  ensure(n) {
    if (this.len + n <= this.buf.length) return;
    let cap = this.buf.length * 2;
    while (cap < this.len + n) cap *= 2;
    const nb = new Uint8Array(cap); nb.set(this.buf.subarray(0, this.len)); this.buf = nb;
  }
  byte(b) { this.ensure(1); this.buf[this.len++] = b & 0xFF; }
  bytes(arr) { this.ensure(arr.length); this.buf.set(arr, this.len); this.len += arr.length; }
  head(major, n) {
    const m = major << 5;
    if (n < 24) { this.byte(m | n); }
    else if (n < 0x100) { this.byte(m | 24); this.byte(n); }
    else if (n < 0x10000) { this.byte(m | 25); this.byte(n >> 8); this.byte(n); }
    else if (n < 0x100000000) { this.byte(m | 26); this.byte(n >>> 24); this.byte(n >>> 16); this.byte(n >>> 8); this.byte(n); }
    else throw new Error('CBOR: integer too large');
  }
  result() { return this.buf.slice(0, this.len); }
}

function encodeValue(w, v, path) {
  if (typeof v === 'number') {
    if (!Number.isInteger(v)) throw new Error(`CBOR: ${path}: non-integer number ${v}`);
    if (v >= 0) w.head(0, v); else w.head(1, -1 - v);
  } else if (typeof v === 'string') {
    const b = enc.encode(v); w.head(3, b.length); w.bytes(b);
  } else if (Array.isArray(v)) {
    w.head(4, v.length);
    v.forEach((e, i) => encodeValue(w, e, `${path}[${i}]`));
  } else if (v && typeof v === 'object') {
    const keys = Object.keys(v);
    w.head(5, keys.length);
    for (const k of keys) { encodeValue(w, k, path); encodeValue(w, v[k], `${path}.${k}`); }
  } else {
    throw new Error(`CBOR: ${path}: unsupported value ${String(v)} (${typeof v})`);
  }
}

/** @returns {Uint8Array} */
export function encode(value) {
  const w = new Writer();
  encodeValue(w, value, '$');
  return w.result();
}

class Reader {
  constructor(bytes) { this.b = bytes; this.p = 0; }
  u8() { if (this.p >= this.b.length) throw new Error('CBOR: truncated'); return this.b[this.p++]; }
  uint(n) { let v = 0; for (let i = 0; i < n; i++) v = v * 256 + this.u8(); return v; }
  take(n) { if (this.p + n > this.b.length) throw new Error('CBOR: truncated'); const s = this.b.subarray(this.p, this.p + n); this.p += n; return s; }
}

const BREAK = Symbol('break');

function readArg(r, info) {
  if (info < 24) return info;
  if (info === 24) return r.uint(1);
  if (info === 25) return r.uint(2);
  if (info === 26) return r.uint(4);
  if (info === 27) return r.uint(8);
  if (info === 31) return -1; // indefinite
  throw new Error(`CBOR: invalid additional info ${info}`);
}

function decodeItem(r) {
  const ib = r.u8();
  const major = ib >> 5, info = ib & 0x1F;
  if (major === 7 && info === 31) return BREAK;
  const n = readArg(r, info);
  switch (major) {
    case 0: return n;
    case 1: return -1 - n;
    case 2: {
      if (n >= 0) return new Uint8Array(r.take(n));
      const parts = []; for (;;) { const c = decodeItem(r); if (c === BREAK) break; parts.push(c); }
      const total = parts.reduce((a, c) => a + c.length, 0); const out = new Uint8Array(total);
      let o = 0; for (const c of parts) { out.set(c, o); o += c.length; } return out;
    }
    case 3: {
      if (n >= 0) return dec.decode(r.take(n));
      let s = ''; for (;;) { const c = decodeItem(r); if (c === BREAK) break; s += c; } return s;
    }
    case 4: {
      const a = [];
      if (n >= 0) { for (let i = 0; i < n; i++) a.push(decodeItem(r)); }
      else { for (;;) { const c = decodeItem(r); if (c === BREAK) break; a.push(c); } }
      return a;
    }
    case 5: {
      const o = {};
      const put = (k, v) => { o[typeof k === 'string' ? k : String(k)] = v; };
      if (n >= 0) { for (let i = 0; i < n; i++) { const k = decodeItem(r); put(k, decodeItem(r)); } }
      else { for (;;) { const k = decodeItem(r); if (k === BREAK) break; put(k, decodeItem(r)); } }
      return o;
    }
    case 6: return decodeItem(r); // tag: ignore, return content
    case 7:
      if (info === 20) return false;
      if (info === 21) return true;
      if (info === 22 || info === 23) return null;
      throw new Error(`CBOR: unsupported simple/float value (info ${info})`);
    default: throw new Error('CBOR: bad major type');
  }
}

/** @param {Uint8Array} bytes @returns {{value:any, length:number}} */
export function decode(bytes) {
  const r = new Reader(bytes);
  const value = decodeItem(r);
  if (value === BREAK) throw new Error('CBOR: unexpected break');
  return { value, length: r.p };
}
