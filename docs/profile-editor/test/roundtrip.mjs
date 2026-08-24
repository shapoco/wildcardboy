// node docs/profile-editor/test/roundtrip.mjs
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';
import assert from 'node:assert/strict';

import { crc32 } from '../js/crc32.js';
import { encode, decode } from '../js/cbor.js';
import { toHex, fromHex } from '../js/ihex.js';
import { build, parse } from '../js/eeprom_image.js';
import { normalize, validate, DEFAULT_PROFILE } from '../js/profile.js';

const here = dirname(fileURLToPath(import.meta.url));

// CRC-32 check value ("123456789" -> 0xCBF43926)
assert.equal(crc32(new TextEncoder().encode('123456789')), 0xCBF43926);

// CBOR basics
assert.deepEqual([...encode(0)], [0x00]);
assert.deepEqual([...encode(-1)], [0x20]);
assert.deepEqual([...encode(1000)], [0x19, 0x03, 0xE8]);
assert.deepEqual([...encode('a')], [0x61, 0x61]);
assert.deepEqual([...encode({ a: [1, 2] })], [0xA1, 0x61, 0x61, 0x82, 0x01, 0x02]);
assert.throws(() => encode({ x: 1.5 }));
assert.deepEqual([...encode({ x: true, y: false })], [0xA2, 0x61, 0x78, 0xF5, 0x61, 0x79, 0xF4]);
assert.deepEqual(decode(new Uint8Array([0xF5])).value, true);
assert.throws(() => encode({ x: null }));
// useTfCard round-trips as a CBOR bool and survives normalize()
{
  const p = normalize({ ...DEFAULT_PROFILE, lcio: { useTfCard: true, ports: [] } });
  assert.equal(p.lcio.useTfCard, true);
  assert.deepEqual(Object.keys(p.lcio), ['useTfCard', 'ports']);
  assert.deepEqual(normalize(decode(encode(p)).value), p);
  assert.equal(normalize({ ...DEFAULT_PROFILE, lcio: { useTfCard: false, ports: [] } }).lcio.useTfCard, undefined);
}
// PCA9555 port validation
{
  const p = normalize({ ...DEFAULT_PROFILE, lcio: { ports: [{ i: 32, f: 16, m: 34 }, { i: 33, f: 17, m: 36 }, { i: 34, f: 18, m: 33 }, { i: 20, f: 0, m: 1 }] } });
  const errs = validate(p).filter(m => m.level === 'error').map(m => m.msg);
  assert.ok(errs.some(m => m.includes('LCIO34') && m.includes('OUTPUT')));  // input is not allowed
  assert.ok(errs.some(m => m.includes('LCIO20')));
  assert.ok(!errs.some(m => m.includes('LCIO32')));
  assert.ok(!errs.some(m => m.includes('LCIO33')));  // open-drain + negative is fine
}
// isp.mcu round-trips and is validated
{
  const p = normalize({ ...DEFAULT_PROFILE, isp: { ...DEFAULT_PROFILE.isp, mcu: 'atmega32u4' } });
  assert.equal(p.isp.mcu, 'atmega32u4');
  assert.deepEqual(Object.keys(p.isp), ['mcu', 'method', 'ports']);
  assert.deepEqual(normalize(decode(encode(p)).value), p);
  assert.ok(!validate(p).some(m => m.level === 'error'));
  const q = normalize({ ...DEFAULT_PROFILE, isp: { ...DEFAULT_PROFILE.isp, mcu: 'z80' } });
  assert.ok(validate(q).some(m => m.level === 'warn' && m.msg.includes('z80')));
  const r = normalize({ ...DEFAULT_PROFILE, isp: { ...DEFAULT_PROFILE.isp, mcu: 'A'.repeat(17) } });
  assert.ok(validate(r).some(m => m.level === 'error' && m.msg.includes('isp.mcu')));
}
// USB ISP needs RESET + BOOTSEL
{
  const p = normalize({ ...DEFAULT_PROFILE, isp: { method: 16, ports: [{ i: 13, f: 32, m: 36 }] } });
  assert.ok(validate(p).some(m => m.level === 'error' && m.msg.includes('BOOTSEL')));
  const q = normalize({ ...DEFAULT_PROFILE, isp: { method: 16, ports: [{ i: 13, f: 32, m: 36 }, { i: 12, f: 33, m: 36 }] } });
  assert.ok(!validate(q).some(m => m.level === 'error'));
}
assert.deepEqual(decode(new Uint8Array([0x9F, 0x01, 0x02, 0xFF])).value, [1, 2]); // indefinite array

// Profile round trip: cards/TJP/profile.json -> CBOR -> frame -> HEX -> back
const src = JSON.parse(readFileSync(join(here, '../../../cards/TJP/profile.json'), 'utf8'));
const prof = normalize(src);
assert.deepEqual(validate(prof).filter(m => m.level === 'error'), []);
const { bytes, cbor, crc } = build(prof);
const hex = toHex(bytes);
assert.ok(hex.startsWith(':10000000'));
assert.equal(((bytes[0] << 24) | (bytes[1] << 16) | (bytes[2] << 8) | bytes[3]) >>> 0, cbor.length);
const back = fromHex(hex);
assert.deepEqual([...back], [...bytes]);
const parsed = parse(back);
assert.equal(parsed.crc, crc);
assert.deepEqual(normalize(parsed.profile), prof);
assert.deepEqual(normalize(DEFAULT_PROFILE), prof, 'built-in default must equal cards/TJP/profile.json');

// Corruption is detected
const bad = bytes.slice(); bad[10] ^= 0x01;
assert.throws(() => parse(bad), /CRC mismatch/);

console.log(`ok: CBOR ${cbor.length} bytes, image ${bytes.length} bytes, CRC 0x${crc.toString(16)}`);
