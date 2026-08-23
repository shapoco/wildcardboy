// Card profile schema (spec/03_card_profile.md): constants,
// default profile, normalization (canonical key order) and validation.

import { CONFIG_BY_ID, PRESET_NAMES } from './lcdtap_config.js';

export const FORMAT = 'WCBCARD';
export const ID_MAX_BYTES = 16;
export const NAME_MAX_BYTES = 64;
export const NUM_LCIO = 14;

export const FUNCTIONS = [
  { v: 0, name: 'Unused' },
  { v: 1, name: 'LCD I/F' },
  { v: 2, name: 'TF card I/F' },
  { v: 16, name: 'Left' }, { v: 17, name: 'Right' }, { v: 18, name: 'Up' }, { v: 19, name: 'Down' },
  { v: 20, name: 'A' }, { v: 21, name: 'B' }, { v: 22, name: 'X' }, { v: 23, name: 'Y' },
  { v: 24, name: 'START' }, { v: 25, name: 'SELECT' }, { v: 26, name: 'L bumper' }, { v: 27, name: 'R bumper' },
  { v: 32, name: 'RESET' }, { v: 33, name: 'BOOTSEL' },
  { v: 34, name: 'ISP CS' }, { v: 35, name: 'ISP SCK' }, { v: 36, name: 'ISP MOSI' }, { v: 37, name: 'ISP MISO' },
];
export const ISP_FUNCTIONS = FUNCTIONS.filter(f => f.v === 0 || f.v >= 32);
export const FUNCTION_VALUES = new Set(FUNCTIONS.map(f => f.v));

export const MODE = { INPUT: 1, OUTPUT: 2, OPEN_DRAIN: 4, PULL_UP: 8, PULL_DOWN: 16, NEGATIVE: 32 };
export const MODE_MAX = 63;

export const ISP_METHODS = [
  { v: 0, name: 'Unused' },
  { v: 1, name: 'SPI' },
  { v: 16, name: 'USB (Mass Storage Class)' },
];
export const ISP_METHOD_VALUES = new Set(ISP_METHODS.map(m => m.v));

export const BUTTONS = ['Left', 'Right', 'Up', 'Down', 'A', 'B', 'X', 'Y', 'START', 'SELECT', 'L bumper', 'R bumper'];

// Fixed signal roles per LCIO from the SPEC tables (hints only).
export const LCIO_HINTS = [
  'LCD RST (SPI/PAR)',
  'LCD CS (SPI/PAR)',
  'LCD SDA (I2C) / SCK (SPI) / WR (PAR)',
  'LCD SCL (I2C) / MOSI (SPI) / D0 (PAR)',
  'LCD DC (SPI) / D1 (PAR)',
  'LCD D2 (PAR) / TF MISO / KEY L',
  'LCD D3 (PAR) / TF CS / KEY R',
  'LCD D4 (PAR) / TF SCK / KEY U',
  'LCD D5 (PAR) / TF MOSI / KEY D',
  'LCD D6 (PAR) / KEY A',
  'LCD D7 (PAR) / KEY B',
  'LCD DC (PAR) / KEY START',
  'KEY SELECT',
  '',
];

export const DEFAULT_PROFILE = {
  format: FORMAT,
  id: 'TJP',
  name: 'Tinyjoypad',
  lcio: {
    ports: [
      { i: 2, f: 1, m: 9 }, { i: 3, f: 1, m: 9 },
      { i: 5, f: 16, m: 36 }, { i: 6, f: 17, m: 36 }, { i: 7, f: 18, m: 36 }, { i: 8, f: 19, m: 36 },
      { i: 9, f: 20, m: 36 }, { i: 13, f: 32, m: 36 },
    ],
  },
  lcdtap: { preset: 'Tinyjoypad' },
  isp: {
    method: 1,
    ports: [
      { i: 2, f: 36, m: 2 }, { i: 3, f: 35, m: 2 }, { i: 9, f: 37, m: 1 }, { i: 13, f: 32, m: 36 },
    ],
  },
  keymap: {
    map: [
      { s: 0, d: 0 }, { s: 1, d: 1 }, { s: 2, d: 2 }, { s: 3, d: 3 },
      { s: 4, d: 4 }, { s: 5, d: 4 }, { s: 6, d: 4 }, { s: 7, d: 4 },
    ],
  },
};

const enc = new TextEncoder();
export function utf8Len(s) { return enc.encode(String(s)).length; }

const toInt = (v, dflt = 0) => {
  const n = typeof v === 'string' ? Number(v) : v;
  return Number.isFinite(n) ? Math.trunc(n) : dflt;
};
const isObj = v => v && typeof v === 'object' && !Array.isArray(v);

function normalizePorts(list) {
  if (!Array.isArray(list)) return [];
  const out = [];
  for (const p of list) {
    if (!isObj(p)) continue;
    out.push({ i: toInt(p.i), f: toInt(p.f), m: toInt(p.m) });
  }
  out.sort((a, b) => a.i - b.i);
  return out;
}

function copyUnknown(dst, src, known) {
  if (!isObj(src)) return dst;
  for (const k of Object.keys(src)) if (!known.includes(k)) dst[k] = src[k];
  return dst;
}

/** Canonical form: fixed key order, integers, sorted ports. Unknown keys kept. */
export function normalize(src) {
  const s = isObj(src) ? src : {};
  const lcio = isObj(s.lcio) ? s.lcio : {};
  const lcdtap = isObj(s.lcdtap) ? s.lcdtap : {};
  const isp = isObj(s.isp) ? s.isp : {};
  const keymap = isObj(s.keymap) ? s.keymap : {};

  const out = {
    format: typeof s.format === 'string' ? s.format : FORMAT,
    id: s.id == null ? '' : String(s.id),
    name: s.name == null ? '' : String(s.name),
    lcio: copyUnknown({ ports: normalizePorts(lcio.ports) }, lcio, ['ports']),
    lcdtap: (() => {
      const o = { preset: lcdtap.preset == null ? '' : String(lcdtap.preset) };
      if (isObj(lcdtap.cfg)) {
        const cfg = {};
        for (const id of Object.keys(CONFIG_BY_ID)) {
          if (Object.prototype.hasOwnProperty.call(lcdtap.cfg, id)) cfg[id] = toInt(lcdtap.cfg[id]);
        }
        for (const k of Object.keys(lcdtap.cfg)) if (!(k in CONFIG_BY_ID)) cfg[k] = lcdtap.cfg[k];
        if (Object.keys(cfg).length) o.cfg = cfg;
      }
      return copyUnknown(o, lcdtap, ['preset', 'cfg']);
    })(),
    isp: copyUnknown({ method: toInt(isp.method), ports: normalizePorts(isp.ports) }, isp, ['method', 'ports']),
    keymap: copyUnknown({
      map: (Array.isArray(keymap.map) ? keymap.map : [])
        .filter(isObj).map(e => ({ s: toInt(e.s), d: toInt(e.d) })).sort((a, b) => a.s - b.s),
    }, keymap, ['map']),
  };
  copyUnknown(out, s, ['format', 'id', 'name', 'lcio', 'lcdtap', 'isp', 'keymap']);
  return out;
}

/** @returns {{level:'error'|'warn', msg:string}[]} */
export function validate(p) {
  const msgs = [];
  const err = msg => msgs.push({ level: 'error', msg });
  const warn = msg => msgs.push({ level: 'warn', msg });

  if (p.format !== FORMAT) err(`format must be "${FORMAT}"`);
  const idLen = utf8Len(p.id);
  if (idLen === 0) err('id is empty');
  if (idLen > ID_MAX_BYTES) err(`id is ${idLen} bytes (max ${ID_MAX_BYTES})`);
  if (p.id && !/^[A-Za-z0-9_-]+$/.test(p.id)) warn('id should use only A-Z a-z 0-9 _ - (it names a directory on the TF card)');
  const nameLen = utf8Len(p.name);
  if (nameLen > NAME_MAX_BYTES) err(`name is ${nameLen} bytes (max ${NAME_MAX_BYTES})`);

  const checkPorts = (list, label, allowed) => {
    const seen = new Set();
    for (const q of list) {
      if (q.i < 0 || q.i >= NUM_LCIO) err(`${label}: LCIO${q.i} is out of range (0-${NUM_LCIO - 1})`);
      if (seen.has(q.i)) err(`${label}: LCIO${q.i} listed twice`);
      seen.add(q.i);
      if (!allowed.has(q.f)) err(`${label}: LCIO${q.i}: unknown function ${q.f}`);
      if (q.m < 0 || q.m > MODE_MAX) err(`${label}: LCIO${q.i}: mode ${q.m} out of range (0-${MODE_MAX})`);
      const dir = q.m & (MODE.INPUT | MODE.OUTPUT | MODE.OPEN_DRAIN);
      if (dir !== 0 && dir !== MODE.INPUT && dir !== MODE.OUTPUT && dir !== MODE.OPEN_DRAIN) warn(`${label}: LCIO${q.i}: mode ${q.m} mixes input/output/open-drain`);
      if ((q.m & MODE.PULL_UP) && (q.m & MODE.PULL_DOWN)) warn(`${label}: LCIO${q.i}: both pull-up and pull-down set`);
      if (q.f !== 0 && dir === 0) warn(`${label}: LCIO${q.i}: function set but direction is "unused"`);
    }
  };
  checkPorts(p.lcio.ports, 'lcio', FUNCTION_VALUES);
  checkPorts(p.isp.ports, 'isp', new Set(ISP_FUNCTIONS.map(f => f.v)));

  if (!PRESET_NAMES.includes(p.lcdtap.preset)) err(`lcdtap.preset "${p.lcdtap.preset}" is not a LcdTap preset name`);
  if (p.lcdtap.cfg) {
    for (const [k, v] of Object.entries(p.lcdtap.cfg)) {
      const e = CONFIG_BY_ID[k];
      if (!e) { warn(`lcdtap.cfg: unknown key "${k}"`); continue; }
      if (!Number.isInteger(v)) err(`lcdtap.cfg.${k}: not an integer`);
      else if (e.type === 'bool' ? (v !== 0 && v !== 1) : (v < e.min || v > e.max)) {
        err(`lcdtap.cfg.${k}: ${v} out of range (${e.type === 'bool' ? '0-1' : `${e.min}-${e.max}`})`);
      }
    }
  }

  if (!ISP_METHOD_VALUES.has(p.isp.method)) err(`isp.method ${p.isp.method} is unknown`);

  const seenS = new Set();
  for (const e of p.keymap.map) {
    if (e.s < 0 || e.s >= BUTTONS.length) err(`keymap: source button ${e.s} out of range`);
    if (e.d < 0 || e.d >= BUTTONS.length) err(`keymap: destination button ${e.d} out of range`);
    if (seenS.has(e.s)) err(`keymap: source button ${e.s} mapped twice`);
    seenS.add(e.s);
  }
  return msgs;
}

export function hasErrors(msgs) { return msgs.some(m => m.level === 'error'); }
