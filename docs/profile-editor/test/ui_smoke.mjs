// Headless smoke test of the editor UI with jsdom.
//   JSDOM_PATH=/path/to/node_modules/jsdom/lib/api.js node docs/profile-editor/test/ui_smoke.mjs
// Skips (exit 0) when jsdom is not available.
import { readFileSync } from 'node:fs';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { dirname, join } from 'node:path';
import assert from 'node:assert/strict';

const here = dirname(fileURLToPath(import.meta.url));
let JSDOM;
try {
  ({ JSDOM } = await import(process.env.JSDOM_PATH ? pathToFileURL(process.env.JSDOM_PATH).href : 'jsdom'));
} catch {
  console.log('skip: jsdom not available');
  process.exit(0);
}

const html = readFileSync(join(here, '../index.html'), 'utf8');
const dom = new JSDOM(html, { url: 'http://localhost/profile-editor/' });
const { window } = dom;
globalThis.window = window;
globalThis.document = window.document;
globalThis.HTMLElement = window.HTMLElement;
globalThis.Blob = window.Blob;
window.URL.createObjectURL = () => 'blob:x';
window.URL.revokeObjectURL = () => {};
globalThis.URL = window.URL;

await import('../js/app.js');
const { normalize, DEFAULT_PROFILE } = await import('../js/profile.js');
const sleep = ms => new Promise(r => setTimeout(r, ms));
const $ = s => document.querySelector(s);
const fire = (el, type) => el.dispatchEvent(new window.Event(type, { bubbles: true }));
const json = () => JSON.parse($('#json').value);

// 1. Initial render equals the built-in default (TJP).
assert.deepEqual(json(), normalize(DEFAULT_PROFILE));
assert.equal($('#f-id').value, 'TJP');
assert.equal($('#f-preset').value, 'Tinyjoypad');
assert.equal($('#t-lcio tbody').children.length, 14);
assert.equal($('#t-cfg tbody').children.length, 20);
assert.equal($('#t-keymap tbody').children.length, 12);
assert.equal($('#btn-export-hex').disabled, false);

// 2. Settings -> JSON: change name, LCIO0 function/direction, keymap, cfg override.
$('#f-name').value = 'Renamed';
fire($('#f-name'), 'input');
assert.equal(json().name, 'Renamed');

const row0 = $('#t-lcio tbody').children[0];
const [fn0, dir0, pull0] = row0.querySelectorAll('select');
fn0.value = '21'; fire(fn0, 'change');          // B button
dir0.value = '4'; fire(dir0, 'change');         // open-drain
pull0.value = '8'; fire(pull0, 'change');       // pull-up
row0.querySelector('input[type=checkbox]').checked = true;
fire(row0.querySelector('input[type=checkbox]'), 'change');
assert.deepEqual(json().lcio.ports[0], { i: 0, f: 21, m: 4 | 8 | 32 });

const km8 = $('#t-keymap tbody').children[8].querySelector('select');  // START
km8.value = '8'; fire(km8, 'change');
assert.ok(json().keymap.map.some(e => e.s === 8 && e.d === 8));

const cfgRows = [...$('#t-cfg tbody').children];
const rotRow = cfgRows.find(r => r.children[1].textContent === 'Output Rotation');
const rotOvr = rotRow.querySelector('input[type=checkbox]');
const rotSel = rotRow.querySelector('select');
assert.equal(rotSel.disabled, true);
assert.equal(rotRow.children[2].textContent, '180');  // preset value
rotOvr.checked = true; fire(rotOvr, 'change');
assert.equal(rotSel.disabled, false);
rotSel.value = '0'; fire(rotSel, 'change');
assert.deepEqual(json().lcdtap.cfg, { outputRot: 0 });

// i2cAddr is only enabled while busInterface == I2C (preset default for Tinyjoypad)
const i2cRow = cfgRows.find(r => r.children[1].textContent === 'I2C Address');
assert.equal(i2cRow.classList.contains('disabled'), false);
assert.equal(i2cRow.children[2].textContent, '0x3C');

// 3. JSON -> settings (debounced).
const edited = json();
edited.id = 'XYZ';
edited.lcdtap.preset = 'Arduboy';
edited.isp.method = 16;
edited.lcio.ports = [{ i: 13, f: 32, m: 36 }];
$('#json').value = JSON.stringify(edited);
fire($('#json'), 'input');
await sleep(400);
assert.equal($('#f-id').value, 'XYZ');
assert.equal($('#f-preset').value, 'Arduboy');
assert.equal($('#f-isp-method').value, '16');
assert.equal(fn0.value, '0');
const [fn13] = $('#t-lcio tbody').children[13].querySelectorAll('select');
assert.equal(fn13.value, '32');
// With Arduboy (SPI) the I2C address row must be disabled now.
assert.equal(i2cRow.classList.contains('disabled'), true);

// 4. Invalid JSON keeps the settings and shows an error.
$('#json').value = '{ not json';
fire($('#json'), 'input');
await sleep(400);
assert.equal($('#f-id').value, 'XYZ');
assert.match($('#status').textContent, /JSON:/);
assert.equal($('#btn-export-hex').disabled, true);

// 5. Validation error blocks Export HEX (id too long).
$('#json').value = JSON.stringify({ ...edited, id: 'A'.repeat(17) });
fire($('#json'), 'input');
await sleep(400);
assert.match($('#status').textContent, /id is 17 bytes/);
assert.equal($('#btn-export-hex').disabled, true);

// 6. Tabs.
const tabs = document.querySelectorAll('#tabs .tab');
tabs[1].click();
assert.equal($('#main').dataset.tab, 'json');

console.log('ok: ui smoke test passed');
