// Card Profile Editor: settings UI <-> JSON <-> EEPROM image (Intel HEX).

import {
  DEFAULT_PROFILE, FUNCTIONS, ISP_FUNCTIONS, ISP_METHODS, MCU_IDS, BUTTONS, MODE,
  LCIO_GPIO_LIST, LCIO_PCA_LIST, LCIO_VIRT_LIST, isPcaLcio, isVirtLcio, lcioHint,
  ID_MAX_BYTES, NAME_MAX_BYTES, VSYNC_HZ_DEFAULT, VSYNC_PULSE_US_DEFAULT,
  utf8Len, normalize, validate, hasErrors,
} from './profile.js';
import { CONFIG_ENTRIES, PRESET_NAMES, effectiveConfig, isEntryEnabled } from './lcdtap_config.js';
import { build, parse, hex32 } from './eeprom_image.js';
import { toHex, fromHex } from './ihex.js';

const $ = sel => document.querySelector(sel);
const el = (tag, attrs = {}, children = []) => {
  const e = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) {
    if (k === 'class') e.className = v;
    else if (k === 'text') e.textContent = v;
    else if (k.startsWith('on')) e.addEventListener(k.slice(2), v);
    else if (v === false || v == null) continue;
    else e.setAttribute(k, v === true ? '' : v);
  }
  for (const c of [].concat(children)) e.append(c);
  return e;
};
const option = (value, text) => el('option', { value: String(value), text });

const DIRECTIONS = [
  { v: 0, name: 'Unused' }, { v: MODE.INPUT, name: 'Input' },
  { v: MODE.OUTPUT, name: 'Output' }, { v: MODE.OPEN_DRAIN, name: 'Open-drain' },
];
const PULLS = [{ v: 0, name: 'None' }, { v: MODE.PULL_UP, name: 'Pull-up' }, { v: MODE.PULL_DOWN, name: 'Pull-down' }];
const DIR_MASK = MODE.INPUT | MODE.OUTPUT | MODE.OPEN_DRAIN;
const PULL_MASK = MODE.PULL_UP | MODE.PULL_DOWN;

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
let profile = normalize(DEFAULT_PROFILE);
let suppressUi = false;  // true while renderSettings() writes controls

// ---------------------------------------------------------------------------
// Settings panel construction
// ---------------------------------------------------------------------------
// Port rows: ui.lcio covers both tables (GPIO 0-13 and PCA9555 32-47),
// ui.isp only the GPIO table.
const ui = { lcio: [], isp: [], cfg: [], keymap: [] };

function buildPortTable(tbody, rows, functions, indices) {
  tbody.replaceChildren();
  for (const i of indices) {
    const pca = isPcaLcio(i) || isVirtLcio(i);  // pulls ignored
    // No input role on PCA9555; virtual ports use Input only for the
    // display-reset port (LCD I/F) -- validation enforces the pairing.
    const dirs = isPcaLcio(i) ? DIRECTIONS.filter(d => d.v !== MODE.INPUT) : DIRECTIONS;
    const fn = el('select', { onchange: onUiChange }, functions.map(f => option(f.v, f.name)));
    const dir = el('select', { onchange: onUiChange }, dirs.map(d => option(d.v, d.name)));
    const pull = el('select', { onchange: onUiChange, disabled: pca }, PULLS.map(p => option(p.v, p.name)));
    const neg = el('input', { type: 'checkbox', onchange: onUiChange, title: 'Negative logic' });
    rows.push({ i, fn, dir, pull, neg });
    tbody.append(el('tr', {}, [
      el('td', { text: `LCIO${i}` }), el('td', {}, fn), el('td', {}, dir), el('td', {}, pull),
      el('td', { class: 'neg' }, neg), el('td', { class: 'hint', text: lcioHint(i) }),
    ]));
  }
}

function buildCfgTable() {
  const tbody = $('#t-cfg tbody');
  for (const e of CONFIG_ENTRIES) {
    const ovr = el('input', { type: 'checkbox', onchange: onUiChange, title: 'Override the preset value' });
    let val;
    if (e.type === 'enum') {
      val = el('select', { onchange: onUiChange }, e.options.map((name, idx) => option(e.min + idx, name)));
    } else if (e.type === 'bool') {
      val = el('input', { type: 'checkbox', onchange: onUiChange });
    } else if (e.type === 'hex') {
      val = el('input', { type: 'text', class: 'mono', spellcheck: 'false', onchange: onUiChange });
    } else {
      val = el('input', { type: 'number', min: e.min, max: e.max, step: e.step || 1, onchange: onUiChange });
    }
    const base = el('td', { class: 'base' });
    const row = el('tr', {}, [
      el('td', {}, ovr), el('td', { text: e.name, title: e.id }), base,
      el('td', { class: 'val' }, val), el('td', { class: 'unit', text: e.unit || '' }),
    ]);
    ui.cfg.push({ e, ovr, val, base, row });
    tbody.append(row);
  }
}

function buildKeymapTable() {
  const tbody = $('#t-keymap tbody');
  BUTTONS.forEach((name, s) => {
    const sel = el('select', { onchange: onUiChange },
      [option('', '(none)'), ...BUTTONS.map((n, d) => option(d, n))]);
    ui.keymap.push(sel);
    tbody.append(el('tr', {}, [el('td', { text: name }), el('td', {}, sel)]));
  });
}

function buildSettings() {
  buildPortTable($('#t-lcio tbody'), ui.lcio, FUNCTIONS, LCIO_GPIO_LIST);
  buildPortTable($('#t-lcio-pca tbody'), ui.lcio, FUNCTIONS.filter(f => f.v < 32 && f.v !== 3), LCIO_PCA_LIST);
  buildPortTable($('#t-lcio-virt tbody'), ui.lcio, FUNCTIONS.filter(f => f.v === 0 || f.v === 1 || (f.v >= 16 && f.v <= 27)), LCIO_VIRT_LIST);
  buildPortTable($('#t-isp tbody'), ui.isp, ISP_FUNCTIONS, LCIO_GPIO_LIST);
  $('#f-usetf').addEventListener('change', onUiChange);
  $('#f-usevio').addEventListener('change', onUiChange);
  $('#f-preset').append(...PRESET_NAMES.map(n => option(n, n)));
  $('#f-isp-method').append(...ISP_METHODS.map(m => option(m.v, m.name)));
  $('#f-isp-mcu').append(option('', '(not set)'), ...MCU_IDS.map(id => option(id, id)));
  $('#f-isp-mcu').addEventListener('change', onUiChange);
  for (const id of ['#f-vsync-hz', '#f-vsync-pulse']) $(id).addEventListener('change', onUiChange);
  buildCfgTable();
  buildKeymapTable();
  for (const id of ['#f-id', '#f-name']) $(id).addEventListener('input', onUiChange);
  $('#f-preset').addEventListener('change', onUiChange);
  $('#f-isp-method').addEventListener('change', onUiChange);
}

// ---------------------------------------------------------------------------
// Formatting helpers
// ---------------------------------------------------------------------------
function formatValue(e, v) {
  if (e.type === 'enum') { const i = v - e.min; return e.options[i] ?? String(v); }
  if (e.type === 'bool') return v ? 'On' : 'Off';
  if (e.type === 'hex') return '0x' + (v & 0xFF).toString(16).toUpperCase().padStart(2, '0');
  return String(v);
}

function parseHex(s) {
  const t = String(s).trim().replace(/^0x/i, '');
  const v = parseInt(t, 16);
  return Number.isFinite(v) ? v : 0;
}

// ---------------------------------------------------------------------------
// Render: state -> settings controls
// ---------------------------------------------------------------------------
function renderPorts(rows, ports) {
  const byIdx = new Map(ports.map(p => [p.i, p]));
  rows.forEach(r => {
    const i = r.i;
    const p = byIdx.get(i);
    const f = p ? p.f : 0, m = p ? p.m : 0;
    r.fn.value = String(f);
    if (r.fn.value !== String(f)) r.fn.value = '0';  // unknown function
    r.dir.value = String(m & DIR_MASK);
    if (r.dir.value !== String(m & DIR_MASK)) r.dir.value = '0';
    r.pull.value = String(m & PULL_MASK);
    if (r.pull.value !== String(m & PULL_MASK)) r.pull.value = '0';
    r.neg.checked = (m & MODE.NEGATIVE) !== 0;
  });
}

function renderCfg() {
  const preset = profile.lcdtap.preset;
  const cfg = profile.lcdtap.cfg || {};
  const eff = effectiveConfig(preset, cfg);
  const presetOnly = effectiveConfig(preset, {});
  for (const r of ui.cfg) {
    const e = r.e;
    const overridden = Object.prototype.hasOwnProperty.call(cfg, e.id);
    const enabled = isEntryEnabled(e, eff);
    const v = overridden ? cfg[e.id] : presetOnly[e.id];
    r.ovr.checked = overridden;
    r.ovr.disabled = !enabled;
    r.val.disabled = !enabled || !overridden;
    r.row.classList.toggle('disabled', !enabled);
    r.base.textContent = formatValue(e, presetOnly[e.id]);
    if (e.type === 'bool') r.val.checked = !!v;
    else if (e.type === 'hex') r.val.value = formatValue(e, v);
    else r.val.value = String(v);
  }
}

function renderSettings() {
  suppressUi = true;
  try {
    $('#f-id').value = profile.id;
    $('#f-name').value = profile.name;
    renderCounters();
    renderPorts(ui.lcio, profile.lcio.ports);
    $('#f-usetf').checked = profile.lcio.useTfCard === true;
    $('#f-usevio').checked = profile.lcio.useVirtIoExp === true;
    {
      const hasVsync = profile.lcio.ports.some(q => q.f === 3);
      $('#f-vsync-hz').value = String(profile.vsync ? profile.vsync.hz : VSYNC_HZ_DEFAULT);
      $('#f-vsync-pulse').value = String(profile.vsync ? profile.vsync.pulseUs : VSYNC_PULSE_US_DEFAULT);
      $('#f-vsync-hz').disabled = !hasVsync;
      $('#f-vsync-pulse').disabled = !hasVsync;
    }
    const presetSel = $('#f-preset');
    presetSel.value = profile.lcdtap.preset;
    if (presetSel.value !== profile.lcdtap.preset) presetSel.value = '';  // unknown preset: show blank
    renderCfg();
    const m = $('#f-isp-method');
    m.value = String(profile.isp.method);
    const mcuSel = $('#f-isp-mcu');
    mcuSel.value = profile.isp.mcu ?? '';
    if (profile.isp.mcu && mcuSel.value !== profile.isp.mcu) mcuSel.value = '';  // unknown id: blank
    renderPorts(ui.isp, profile.isp.ports);
    $('#t-isp').classList.toggle('disabled', profile.isp.method === 0);
    const map = new Map(profile.keymap.map.map(e => [e.s, e.d]));
    ui.keymap.forEach((sel, s) => { sel.value = map.has(s) ? String(map.get(s)) : ''; });
  } finally {
    suppressUi = false;
  }
}

function renderCounters() {
  const set = (id, len, max) => {
    const c = $(id);
    c.textContent = `${len}/${max} bytes`;
    c.classList.toggle('over', len > max);
  };
  set('#c-id', utf8Len($('#f-id').value), ID_MAX_BYTES);
  set('#c-name', utf8Len($('#f-name').value), NAME_MAX_BYTES);
}

// ---------------------------------------------------------------------------
// Read: settings controls -> state
// ---------------------------------------------------------------------------
function readPorts(rows) {
  const ports = [];
  rows.forEach(r => {
    const i = r.i;
    const f = Number(r.fn.value) | 0;
    const m = (Number(r.dir.value) | 0) | (Number(r.pull.value) | 0) | (r.neg.checked ? MODE.NEGATIVE : 0);
    if (f !== 0 || m !== 0) ports.push({ i, f, m });
  });
  return ports;
}

function readSettings() {
  const p = structuredClone(profile);  // keeps unknown keys
  p.id = $('#f-id').value;
  p.name = $('#f-name').value;
  p.lcio.ports = readPorts(ui.lcio);
  if ($('#f-usetf').checked) p.lcio.useTfCard = true; else delete p.lcio.useTfCard;
  if ($('#f-usevio').checked) p.lcio.useVirtIoExp = true; else delete p.lcio.useVirtIoExp;
  if (p.lcio.ports.some(q => q.f === 3)) {
    p.vsync = Object.assign(p.vsync || {}, {
      hz: Number($('#f-vsync-hz').value) | 0,
      pulseUs: Number($('#f-vsync-pulse').value) | 0,
    });
  } else {
    delete p.vsync;
  }
  const presetSel = $('#f-preset');
  if (presetSel.value !== '') p.lcdtap.preset = presetSel.value;
  const cfg = {};
  for (const r of ui.cfg) {
    if (!r.ovr.checked || r.ovr.disabled) continue;
    const e = r.e;
    let v;
    if (e.type === 'bool') v = r.val.checked ? 1 : 0;
    else if (e.type === 'hex') v = parseHex(r.val.value);
    else v = Number(r.val.value) | 0;
    cfg[e.id] = v;
  }
  // Keep unknown cfg keys from the JSON side.
  if (p.lcdtap.cfg) for (const [k, v] of Object.entries(p.lcdtap.cfg)) if (!CONFIG_ENTRIES.some(e => e.id === k)) cfg[k] = v;
  if (Object.keys(cfg).length) p.lcdtap.cfg = cfg; else delete p.lcdtap.cfg;
  p.isp.method = Number($('#f-isp-method').value) | 0;
  {
    const mcu = $('#f-isp-mcu').value;
    if (mcu !== '') p.isp.mcu = mcu;
    else if (!(profile.isp.mcu && !MCU_IDS.includes(profile.isp.mcu))) delete p.isp.mcu;
    // (an unknown id from the JSON side is kept while the select shows blank)
  }
  p.isp.ports = readPorts(ui.isp);
  p.keymap.map = [];
  ui.keymap.forEach((sel, s) => { if (sel.value !== '') p.keymap.map.push({ s, d: Number(sel.value) | 0 }); });
  return normalize(p);
}

function onUiChange() {
  if (suppressUi) return;
  profile = readSettings();
  renderCounters();
  renderCfg();  // enable/disable state may have changed
  $('#t-isp').classList.toggle('disabled', profile.isp.method === 0);
  renderJson();
  updateStatus();
}

// ---------------------------------------------------------------------------
// JSON panel
// ---------------------------------------------------------------------------
const jsonArea = $('#json');
let jsonTimer = null;
let jsonError = null;
let jsonDirty = false;  // textarea edited since the last renderJson()

function renderJson(force = false) {
  if (!force && document.activeElement === jsonArea) return;  // user is typing there
  jsonArea.value = JSON.stringify(profile, null, 2) + '\n';
  jsonDirty = false;
  jsonError = null;
}

function applyJsonText(text) {
  let obj;
  try {
    obj = JSON.parse(text);
  } catch (e) {
    jsonError = `JSON: ${e.message}`;
    updateStatus();
    return;
  }
  if (!obj || typeof obj !== 'object' || Array.isArray(obj)) {
    jsonError = 'JSON: root must be an object';
    updateStatus();
    return;
  }
  jsonError = null;
  profile = normalize(obj);
  renderSettings();
  updateStatus();
}

jsonArea.addEventListener('input', () => {
  jsonDirty = true;
  clearTimeout(jsonTimer);
  jsonTimer = setTimeout(() => applyJsonText(jsonArea.value), 300);
});
jsonArea.addEventListener('blur', () => {
  clearTimeout(jsonTimer);
  if (!jsonDirty) return;
  applyJsonText(jsonArea.value);
  if (!jsonError) renderJson(true);  // re-format
});

// ---------------------------------------------------------------------------
// Status line
// ---------------------------------------------------------------------------
function updateStatus() {
  const box = $('#status');
  box.replaceChildren();
  const msgs = validate(profile);
  const errors = hasErrors(msgs);
  if (jsonError) box.append(el('div', { class: 'msg-error', text: jsonError }));
  if (msgs.length) {
    box.append(el('ul', {}, msgs.map(m => el('li', { class: m.level === 'error' ? 'msg-error' : 'msg-warn', text: m.msg }))));
  }
  let info;
  try {
    const img = build(profile);
    info = `CBOR ${img.cbor.length} bytes, image ${img.bytes.length} bytes, CRC ${hex32(img.crc)}`;
  } catch (e) {
    info = `cannot encode: ${e.message}`;
  }
  box.append(el('div', { class: 'info', text: info }));
  $('#btn-export-hex').disabled = errors || !!jsonError;
  $('#btn-export-hex').title = errors ? 'Fix the errors first' : 'Download the EEPROM image as Intel HEX';
}

// ---------------------------------------------------------------------------
// Toolbar
// ---------------------------------------------------------------------------
function download(name, text, type) {
  const blob = new Blob([text], { type });
  const url = URL.createObjectURL(blob);
  const a = el('a', { href: url, download: name });
  document.body.append(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}

function baseName() {
  const id = (profile.id || 'profile').replace(/[^A-Za-z0-9_-]/g, '_');
  return id || 'profile';
}

function setProfile(p) {
  clearTimeout(jsonTimer);
  profile = normalize(p);
  jsonError = null;
  renderSettings();
  renderJson(true);
  updateStatus();
}

function readFile(input, onText) {
  const f = input.files && input.files[0];
  input.value = '';
  if (!f) return;
  f.text().then(onText).catch(e => showError(`cannot read ${f.name}: ${e.message}`));
}

function showError(msg) {
  jsonError = msg;
  updateStatus();
}

$('#btn-import-json').addEventListener('click', () => $('#file-json').click());
$('#file-json').addEventListener('change', ev => readFile(ev.target, text => {
  try {
    setProfile(JSON.parse(text));
  } catch (e) {
    showError(`Import JSON failed: ${e.message}`);
  }
}));
$('#btn-export-json').addEventListener('click', () => {
  download(`${baseName()}.json`, JSON.stringify(profile, null, 2) + '\n', 'application/json');
});
$('#btn-import-hex').addEventListener('click', () => $('#file-hex').click());
$('#file-hex').addEventListener('change', ev => readFile(ev.target, text => {
  try {
    const bytes = fromHex(text);
    const { profile: p } = parse(bytes);
    setProfile(p);
  } catch (e) {
    showError(`Import HEX failed: ${e.message}`);
  }
}));
$('#btn-export-hex').addEventListener('click', () => {
  try {
    const { bytes } = build(profile);
    download('profile.hex', toHex(bytes), 'text/plain');
  } catch (e) {
    showError(`Export HEX failed: ${e.message}`);
  }
});

// ---------------------------------------------------------------------------
// Tabs (narrow layout) and header height for sticky panels
// ---------------------------------------------------------------------------
for (const btn of document.querySelectorAll('#tabs .tab')) {
  btn.addEventListener('click', () => {
    for (const b of document.querySelectorAll('#tabs .tab')) b.classList.toggle('active', b === btn);
    $('#main').dataset.tab = btn.dataset.tab;
  });
}
function updateHeaderHeight() {
  const h = $('#header').offsetHeight;
  document.documentElement.style.setProperty('--header-h', `${h}px`);
}
window.addEventListener('resize', updateHeaderHeight);

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------
buildSettings();
updateHeaderHeight();
renderSettings();
renderJson();
updateStatus();
