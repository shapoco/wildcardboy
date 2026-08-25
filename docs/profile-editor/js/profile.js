// Card profile schema (spec/03_card_profile.md): constants,
// default profile, normalization (canonical key order) and validation.

import { CONFIG_BY_ID, PRESET_NAMES } from './lcdtap_config.js';

export const FORMAT = 'WCBCARD';
export const ID_MAX_BYTES = 16;
export const NAME_MAX_BYTES = 64;
// LCIO numbering: 0..13 = bus GPIOs, 32..47 = card-side PCA9555 ports
// (P0_0..P1_7 via LCAUX I2C), 64..79 = virtual I/O expander ports
// (GPA0..GPB7, host-emulated on LCIO6/7). Other numbers do not exist.
export const NUM_LCIO = 14;  // GPIO LCIOs
export const LCIO_PCA_FIRST = 32;
export const LCIO_PCA_COUNT = 16;
export const LCIO_GPIO_LIST = Array.from({ length: NUM_LCIO }, (_, i) => i);
export const LCIO_PCA_LIST = Array.from({ length: LCIO_PCA_COUNT }, (_, i) => LCIO_PCA_FIRST + i);
export const LCIO_VIRT_FIRST = 64;
export const LCIO_VIRT_COUNT = 16;
export const LCIO_VIRT_LIST = Array.from({ length: LCIO_VIRT_COUNT }, (_, i) => LCIO_VIRT_FIRST + i);
export const isPcaLcio = i => i >= LCIO_PCA_FIRST && i < LCIO_PCA_FIRST + LCIO_PCA_COUNT;
export const isVirtLcio = i => i >= LCIO_VIRT_FIRST && i < LCIO_VIRT_FIRST + LCIO_VIRT_COUNT;
export const isValidLcio = i => (i >= 0 && i < NUM_LCIO) || isPcaLcio(i) || isVirtLcio(i);

export const FUNCTIONS = [
  { v: 0, name: 'Unused' },
  { v: 1, name: 'LCD I/F' },
  { v: 2, name: 'TF card I/F' },
  { v: 16, name: 'Left' }, { v: 17, name: 'Right' }, { v: 18, name: 'Up' }, { v: 19, name: 'Down' },
  { v: 20, name: 'A' }, { v: 21, name: 'B' }, { v: 22, name: 'X' }, { v: 23, name: 'Y' },
  { v: 24, name: 'START' }, { v: 25, name: 'SELECT' }, { v: 26, name: 'L bumper' }, { v: 27, name: 'R bumper' },
  { v: 32, name: 'RESET' }, { v: 33, name: 'BOOTSEL' },
  { v: 34, name: 'ISP CS' }, { v: 35, name: 'ISP SCK' }, { v: 36, name: 'ISP MOSI' }, { v: 37, name: 'ISP MISO' },
  { v: 38, name: 'ISP UART TX' }, { v: 39, name: 'ISP UART RX' },
  { v: 48, name: 'I2C slave' },
];
export const ISP_FUNCTIONS = FUNCTIONS.filter(f => f.v === 0 || (f.v >= 32 && f.v <= 39));
export const FUNCTION_VALUES = new Set(FUNCTIONS.map(f => f.v));

export const MODE = { INPUT: 1, OUTPUT: 2, OPEN_DRAIN: 4, PULL_UP: 8, PULL_DOWN: 16, NEGATIVE: 32 };
export const MODE_MAX = 63;

// MCU IDs (spec/03_card_profile.md "MCU ID").
export const MCU_IDS = ['attiny85', 'atmega32u4', 'rp2040', 'rp2350', 'esp8266'];
export const MCU_ID_MAX_BYTES = 16;
// MCUs programmable per ISP method.
export const MCU_BY_METHOD = { 1: ['attiny85', 'atmega32u4'], 2: ['esp8266'], 16: ['rp2040', 'rp2350'] };

export const ISP_METHODS = [
  { v: 0, name: 'Unused' },
  { v: 1, name: 'SPI' },
  { v: 2, name: 'UART (Espressif serial bootloader)' },
  { v: 16, name: 'USB (Mass Storage Class)' },
];
export const ISP_METHOD_VALUES = new Set(ISP_METHODS.map(m => m.v));

// Virtual I/O expander chip IDs (spec/03_card_profile.md "virtIoExp").
export const VIRT_CHIPS = ['mcp23017'];

export const BUTTONS = ['Left', 'Right', 'Up', 'Down', 'A', 'B', 'X', 'Y', 'START', 'SELECT', 'L bumper', 'R bumper'];

// Fixed signal roles per LCIO from the SPEC tables (hints only).
export const LCIO_HINTS = [
  'LCD RST (SPI/PAR)',
  'LCD CS (SPI/PAR)',
  'LCD SDA (I2C) / SCK (SPI) / WR (PAR)',
  'LCD SCL (I2C) / MOSI (SPI) / D0 (PAR)',
  'LCD DC (SPI) / D1 (PAR)',
  'LCD D2 (PAR) / KEY L',
  'LCD D3 (PAR) / KEY R',
  'LCD D4 (PAR) / KEY U',
  'LCD D5 (PAR) / TF MOSI / KEY D',
  'LCD D6 (PAR) / TF CS / KEY A',
  'LCD D7 (PAR) / TF SCK / KEY B',
  'LCD DC (PAR) / TF MISO / KEY START',
  'KEY SELECT',
  '',
];
export function lcioHint(i) {
  if (isPcaLcio(i)) { const n = i - LCIO_PCA_FIRST; return `PCA9555 P${n >> 3}_${n & 7}`; }
  if (isVirtLcio(i)) { const n = i - LCIO_VIRT_FIRST; return `VirtIoExp GP${n < 8 ? 'A' : 'B'}${n & 7}`; }
  return LCIO_HINTS[i] ?? '';
}

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
    lcio: copyUnknown(
      Object.assign(
        lcio.useTfCard === true ? { useTfCard: true } : {},
        lcio.useVirtIoExp === true ? { useVirtIoExp: true } : {},
        { ports: normalizePorts(lcio.ports) }),
      lcio, ['ports', 'useTfCard', 'useVirtIoExp']),
    ...(isObj(s.virtIoExp) ? {
      virtIoExp: copyUnknown(
        { chip: s.virtIoExp.chip == null ? '' : String(s.virtIoExp.chip), addr: toInt(s.virtIoExp.addr) },
        s.virtIoExp, ['chip', 'addr']),
    } : {}),
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
    isp: copyUnknown(
      Object.assign(
        typeof isp.mcu === 'string' && isp.mcu !== '' ? { mcu: String(isp.mcu) } : {},
        { method: toInt(isp.method), ports: normalizePorts(isp.ports) }),
      isp, ['mcu', 'method', 'ports']),
    keymap: copyUnknown({
      map: (Array.isArray(keymap.map) ? keymap.map : [])
        .filter(isObj).map(e => ({ s: toInt(e.s), d: toInt(e.d) })).sort((a, b) => a.s - b.s),
    }, keymap, ['map']),
  };
  copyUnknown(out, s, ['format', 'id', 'name', 'lcio', 'virtIoExp', 'lcdtap', 'isp', 'keymap']);
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
      if (!isValidLcio(q.i)) err(`${label}: LCIO${q.i} is not a valid port (0-${NUM_LCIO - 1}, ${LCIO_PCA_FIRST}-${LCIO_PCA_FIRST + LCIO_PCA_COUNT - 1})`);
      if (seen.has(q.i)) err(`${label}: LCIO${q.i} listed twice`);
      seen.add(q.i);
      if (!allowed.has(q.f)) err(`${label}: LCIO${q.i}: unknown function ${q.f}`);
      if (q.m < 0 || q.m > MODE_MAX) err(`${label}: LCIO${q.i}: mode ${q.m} out of range (0-${MODE_MAX})`);
      const dir = q.m & (MODE.INPUT | MODE.OUTPUT | MODE.OPEN_DRAIN);
      if (dir !== 0 && dir !== MODE.INPUT && dir !== MODE.OUTPUT && dir !== MODE.OPEN_DRAIN) warn(`${label}: LCIO${q.i}: mode ${q.m} mixes input/output/open-drain`);
      if ((q.m & MODE.PULL_UP) && (q.m & MODE.PULL_DOWN)) warn(`${label}: LCIO${q.i}: both pull-up and pull-down set`);
      if (dir === MODE.OPEN_DRAIN && !(q.m & MODE.NEGATIVE)) err(`${label}: LCIO${q.i}: open-drain output needs negative logic`);
      if (q.f !== 0 && dir === 0) warn(`${label}: LCIO${q.i}: function set but direction is "unused"`);
      if (isPcaLcio(q.i) || isVirtLcio(q.i)) {
        const kind = isPcaLcio(q.i) ? 'PCA9555' : 'virtual expander';
        if (dir !== 0 && dir !== MODE.OUTPUT && dir !== MODE.OPEN_DRAIN) err(`${label}: LCIO${q.i}: ${kind} ports must be OUTPUT or open-drain`);
        if (q.m & (MODE.PULL_UP | MODE.PULL_DOWN)) warn(`${label}: LCIO${q.i}: pull settings are ignored on ${kind} ports`);
        if (q.f >= 32) err(`${label}: LCIO${q.i}: RESET/BOOTSEL/ISP cannot be on a ${kind} port`);
      }
      if (q.f === 48 && q.i !== 6 && q.i !== 7) err(`${label}: LCIO${q.i}: I2C slave (48) is fixed to LCIO6/7`);
    }
  };
  checkPorts(p.lcio.ports, 'lcio', FUNCTION_VALUES);
  checkPorts(p.isp.ports, 'isp', new Set(ISP_FUNCTIONS.map(f => f.v)));

  const hasPort = (list, f) => list.some(q => q.f === f && (q.m & (MODE.INPUT | MODE.OUTPUT | MODE.OPEN_DRAIN)) !== 0);
  const hasAny = f => hasPort(p.isp.ports, f) || hasPort(p.lcio.ports, f);
  if (p.isp.method === 16) {
    if (!hasAny(32)) err('isp: USB ISP needs a RESET (32) port');
    if (!hasAny(33)) err('isp: USB ISP needs a BOOTSEL (33) port');
  } else if (p.isp.method === 1) {
    for (const [f, name] of [[36, 'MOSI'], [35, 'SCK'], [37, 'MISO']]) {
      if (!hasPort(p.isp.ports, f)) err(`isp: SPI ISP needs an ISP ${name} (${f}) port`);
    }
    if (!hasAny(32)) err('isp: SPI ISP needs a RESET (32) port');
  } else if (p.isp.method === 2) {
    for (const [f, name] of [[38, 'UART TX'], [39, 'UART RX']]) {
      if (!hasPort(p.isp.ports, f)) err(`isp: UART ISP needs an ISP ${name} (${f}) port`);
    }
    if (!hasAny(32)) err('isp: UART ISP needs a RESET (32) port');
    if (!hasAny(33)) err('isp: UART ISP needs a BOOTSEL (33) port');
  }

  {
    const useVio = p.lcio.useVirtIoExp === true;
    if (useVio && !p.virtIoExp) err('lcio.useVirtIoExp is set but virtIoExp is missing');
    if (!useVio && p.virtIoExp) warn('virtIoExp is set but lcio.useVirtIoExp is not true');
    if (p.virtIoExp) {
      if (!VIRT_CHIPS.includes(p.virtIoExp.chip)) warn(`virtIoExp.chip "${p.virtIoExp.chip}" is not a known chip ID`);
      if (!Number.isInteger(p.virtIoExp.addr) || p.virtIoExp.addr < 0 || p.virtIoExp.addr > 127) err('virtIoExp.addr must be 0-127');
    }
    if (useVio) {
      for (const i of [6, 7]) {
        if (!p.lcio.ports.some(q => q.i === i && q.f === 48)) err(`lcio: virtual I/O expander needs LCIO${i} with function I2C slave (48)`);
      }
    }
    if (p.lcio.ports.some(q => isVirtLcio(q.i)) && !useVio) err('lcio: LCIO64-79 require lcio.useVirtIoExp');
  }

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
  if (p.isp.mcu != null) {
    const mcuLen = utf8Len(p.isp.mcu);
    if (mcuLen > MCU_ID_MAX_BYTES) err(`isp.mcu is ${mcuLen} bytes (max ${MCU_ID_MAX_BYTES})`);
    if (!MCU_IDS.includes(p.isp.mcu)) warn(`isp.mcu "${p.isp.mcu}" is not a known MCU ID`);
    const allowed = MCU_BY_METHOD[p.isp.method];
    if (allowed && MCU_IDS.includes(p.isp.mcu) && !allowed.includes(p.isp.mcu)) {
      warn(`isp.mcu "${p.isp.mcu}" does not match isp.method ${p.isp.method}`);
    }
  } else if (p.isp.method !== 0) {
    warn('isp.mcu is not set (the host assumes a default MCU for the method)');
  }

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
