// EEPROM image framing: [length BE32][CBOR][CRC32(length||CBOR) BE32].

import { encode as cborEncode, decode as cborDecode } from './cbor.js';
import { crc32 } from './crc32.js';

export const EEPROM_SIZE = 32768;  // 24LC256
const HEADER = 4, TRAILER = 4;
export const MAX_CBOR_LEN = EEPROM_SIZE - HEADER - TRAILER;

/** @returns {{bytes:Uint8Array, cbor:Uint8Array, crc:number}} */
export function build(profile) {
  const cbor = cborEncode(profile);
  if (cbor.length > MAX_CBOR_LEN) throw new Error(`profile too large for the EEPROM (${cbor.length} > ${MAX_CBOR_LEN} bytes)`);
  const bytes = new Uint8Array(HEADER + cbor.length + TRAILER);
  const n = cbor.length;
  bytes[0] = n >>> 24; bytes[1] = n >>> 16; bytes[2] = n >>> 8; bytes[3] = n;
  bytes.set(cbor, HEADER);
  const crc = crc32(bytes.subarray(0, HEADER + n));
  bytes[HEADER + n] = crc >>> 24; bytes[HEADER + n + 1] = crc >>> 16;
  bytes[HEADER + n + 2] = crc >>> 8; bytes[HEADER + n + 3] = crc;
  return { bytes, cbor, crc };
}

/** @param {Uint8Array} bytes @returns {{profile:any, cborLen:number, crc:number}} */
export function parse(bytes) {
  if (bytes.length < HEADER + TRAILER) throw new Error('image too short');
  const n = ((bytes[0] << 24) | (bytes[1] << 16) | (bytes[2] << 8) | bytes[3]) >>> 0;
  if (n > MAX_CBOR_LEN || n === 0) throw new Error(`invalid CBOR length field (${n})`);
  if (HEADER + n + TRAILER > bytes.length) throw new Error(`CBOR length (${n}) exceeds image size (${bytes.length})`);
  const stored = ((bytes[HEADER + n] << 24) | (bytes[HEADER + n + 1] << 16) | (bytes[HEADER + n + 2] << 8) | bytes[HEADER + n + 3]) >>> 0;
  const calc = crc32(bytes.subarray(0, HEADER + n));
  if (stored !== calc) throw new Error(`CRC mismatch (stored ${hex32(stored)}, computed ${hex32(calc)})`);
  const { value, length } = cborDecode(bytes.subarray(HEADER, HEADER + n));
  if (length !== n) throw new Error(`CBOR object is ${length} bytes but the length field says ${n}`);
  if (!value || typeof value !== 'object' || Array.isArray(value)) throw new Error('CBOR root is not a map');
  return { profile: value, cborLen: n, crc: stored };
}

export function hex32(v) { return '0x' + (v >>> 0).toString(16).toUpperCase().padStart(8, '0'); }
