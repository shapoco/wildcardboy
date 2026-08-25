// LcdTap configuration vocabulary, transcribed from
//   submodule/lcdtap/lib/include/lcdtap/config.hpp  (CONFIG_IDS, *_NAMES)
//   submodule/lcdtap/lib/src/config.cpp             (getConfigEntryById,
//                                                    getDefaultConfig, getPresetConfig)
// at lcdtap commit 42ad488. The firmware is the source of truth; the values
// here are only used for the editor UI (ranges, choices, preset defaults).

export const LCDTAP_COMMIT = '42ad488';

export const CONTROLLER_NAMES = ['ST7789', 'SSD1306', 'SSD1331', 'ILI9341', 'ST7032', 'KS0108'];
export const BUS_NAMES = ['I2C', '4-Line SPI', '3-Line SPI', 'Parallel8', 'Para8 Dual CS'];
// Index 0 = "Off" corresponds to the value -1 (interfaceFormatOverride).
export const INTERFACE_FORMAT_NAMES = ['Off', 'GRAY1', 'RGB111', 'RGB332', 'RGB444', 'RGB565', 'RGB666-LA', 'RGB666-RA'];
export const TRIM_MODE_NAMES = ['Off', 'Auto', 'Custom'];
export const FLIP_MODE_NAMES = ['Off', 'Horizontal', 'Vertical', 'Both'];
export const SCALE_MODE_NAMES = ['Off', 'Integral', 'Fit', 'Stretch'];
export const ROTATION_NAMES = ['0', '90', '180', '270'];
export const ST7032_ROWS_NAMES = ['1', '2', '4'];
export const ST7032_CGRAM_NAMES = ['0x0-0xF', '0x0-0x7', '0x0-0x5', 'None'];
export const ON_OFF_NAMES = ['Off', 'On'];

const F = { ST7789: 0, SSD1306: 1, SSD1331: 2, ILI9341: 3, ST7032: 4, KS0108: 5 };
const B = { I2C: 0, SPI4: 1, SPI3: 2, PAR: 3, PAR2CS: 4 };

// enable: the entry is meaningful only while cfg[enable.id] is in [min, max].
export const CONFIG_ENTRIES = [
  { id: 'ctrlFamily', name: 'Controller Family', type: 'enum', min: 0, max: 5, options: CONTROLLER_NAMES },
  { id: 'busInterface', name: 'Bus Interface', type: 'enum', min: 0, max: 4, options: BUS_NAMES },
  { id: 'i2cAddr', name: 'I2C Address', type: 'hex', min: 0x08, max: 0x77, enable: { id: 'busInterface', min: B.I2C, max: B.I2C } },
  { id: 'buffWidth', name: 'Buffer Width', type: 'int', unit: 'px', min: 32, max: 480, step: 8, enable: { id: 'ctrlFamily', min: F.ST7789, max: F.ILI9341 } },
  { id: 'buffHeight', name: 'Buffer Height', type: 'int', unit: 'px', min: 32, max: 480, step: 8, enable: { id: 'ctrlFamily', min: F.ST7789, max: F.ILI9341 } },
  { id: 'textCols', name: 'Text Buff Cols', type: 'int', min: 2, max: 40, step: 2, enable: { id: 'ctrlFamily', min: F.ST7032, max: F.ST7032 } },
  { id: 'textRows', name: 'Text Buff Rows', type: 'enum', min: 0, max: 2, options: ST7032_ROWS_NAMES, enable: { id: 'ctrlFamily', min: F.ST7032, max: F.ST7032 } },
  { id: 'textCgramArea', name: 'Text CGRAM Area', type: 'enum', min: 0, max: 3, options: ST7032_CGRAM_NAMES, enable: { id: 'ctrlFamily', min: F.ST7032, max: F.ST7032 } },
  { id: 'trimMode', name: 'Trim Mode', type: 'enum', min: 0, max: 2, options: TRIM_MODE_NAMES },
  { id: 'trimX', name: 'Trim Offset X', type: 'int', unit: 'px', min: 0, max: 480, step: 1, enable: { id: 'trimMode', min: 2, max: 2 } },
  { id: 'trimY', name: 'Trim Offset Y', type: 'int', unit: 'px', min: 0, max: 480, step: 1, enable: { id: 'trimMode', min: 2, max: 2 } },
  { id: 'trimWidth', name: 'Trim Width', type: 'int', unit: 'px', min: 0, max: 480, step: 1, enable: { id: 'trimMode', min: 2, max: 2 } },
  { id: 'trimHeight', name: 'Trim Height', type: 'int', unit: 'px', min: 0, max: 480, step: 1, enable: { id: 'trimMode', min: 2, max: 2 } },
  { id: 'flipMode', name: 'Flip Mode', type: 'enum', min: 0, max: 3, options: FLIP_MODE_NAMES },
  { id: 'inverted', name: 'Inverse', type: 'bool' },
  { id: 'swapRB', name: 'Swap Red/Blue', type: 'bool', enable: { id: 'ctrlFamily', min: F.ST7789, max: F.ILI9341 } },
  { id: 'forcePwrOn', name: 'Force Power On', type: 'bool' },
  { id: 'intfFmtOvr', name: 'Format Override', type: 'enum', min: -1, max: 6, options: INTERFACE_FORMAT_NAMES, enable: { id: 'ctrlFamily', min: F.ST7789, max: F.ILI9341 } },
  { id: 'outputRot', name: 'Output Rotation', type: 'enum', unit: 'deg', min: 0, max: 3, options: ROTATION_NAMES },
  { id: 'scaleMode', name: 'Output Scaling', type: 'enum', min: 0, max: 3, options: SCALE_MODE_NAMES },
];

export const CONFIG_IDS = CONFIG_ENTRIES.map(e => e.id);
export const CONFIG_BY_ID = Object.fromEntries(CONFIG_ENTRIES.map(e => [e.id, e]));

export const PRESET_NAMES = [
  'ILI9341', 'ILI9342', 'ILI9488', 'SSD1306', 'SSD1331',
  'KS0108', 'ST7735', 'ST7789', 'Text 8x2', 'Text 16x2',
  'Text 16x4', 'Text 20x4', 'Arduboy', 'ESPboy', 'M5Stack CoreS3',
  'PicoCalc', 'PicoPad', 'PicoSystem', 'Thumby', 'Tinyjoypad',
  'Wio Terminal', 'Xiamocon',
];

// getDefaultConfig() in CONFIG_IDS vocabulary (textRows = enum index).
function defaultConfig(family) {
  const c = {
    ctrlFamily: family, busInterface: B.SPI4, i2cAddr: 0x3C,
    buffWidth: 0, buffHeight: 0, textCols: 0, textRows: 0, textCgramArea: 0,
    trimMode: 0, trimX: 0, trimY: 0, trimWidth: 0, trimHeight: 0,
    flipMode: 0, inverted: 0, swapRB: 0, forcePwrOn: 0, intfFmtOvr: -1,
    outputRot: 0, scaleMode: 2,
  };
  switch (family) {
    case F.ST7789: Object.assign(c, { buffWidth: 240, buffHeight: 320, inverted: 1, busInterface: B.SPI4 }); break;
    case F.SSD1306: Object.assign(c, { buffWidth: 128, buffHeight: 64, outputRot: 2, busInterface: B.I2C }); break;
    case F.SSD1331: Object.assign(c, { buffWidth: 96, buffHeight: 64, outputRot: 2, busInterface: B.SPI4 }); break;
    case F.ILI9341: Object.assign(c, { buffWidth: 240, buffHeight: 320, inverted: 1, swapRB: 1, busInterface: B.SPI4 }); break;
    case F.ST7032: Object.assign(c, { textCols: 16, textRows: 1, textCgramArea: 1, busInterface: B.I2C, i2cAddr: 0x3E }); break;
    case F.KS0108: Object.assign(c, { busInterface: B.PAR2CS }); break;
  }
  normalizeConfig(c);
  c.trimMode = 0; c.trimX = 0; c.trimY = 0; c.trimWidth = c.buffWidth; c.trimHeight = c.buffHeight;
  return c;
}

// normalizeConfig(): derived framebuffer geometry.
function normalizeConfig(c) {
  c.i2cAddr &= 0x7F;
  if (c.ctrlFamily === F.KS0108) { c.buffWidth = 128; c.buffHeight = 64; return; }
  if (c.ctrlFamily !== F.ST7032) return;
  let cols = c.textCols & ~1; cols = cols < 2 ? 2 : cols > 40 ? 40 : cols; c.textCols = cols;
  c.textRows = Math.min(2, Math.max(0, c.textRows | 0));
  const rows = [1, 2, 4][c.textRows];
  c.textCgramArea &= 3;
  c.buffWidth = cols * 6 - 1;
  c.buffHeight = rows * 9 - 1;
}

const PRESET_DEFS = {
  'ILI9341': [F.ILI9341, {}],
  'ILI9342': [F.ILI9341, { buffWidth: 320, buffHeight: 240 }],
  'ILI9488': [F.ILI9341, { buffWidth: 320, buffHeight: 480 }],
  'SSD1306': [F.SSD1306, {}],
  'SSD1331': [F.SSD1331, {}],
  'KS0108': [F.KS0108, {}],
  'Text 8x2': [F.ST7032, { textCols: 8, textRows: 1, textCgramArea: 2 }],
  'Text 16x2': [F.ST7032, { textCols: 16, textRows: 1, textCgramArea: 1 }],
  'Text 16x4': [F.ST7032, { textCols: 16, textRows: 2, textCgramArea: 0, busInterface: B.PAR }],
  'Text 20x4': [F.ST7032, { textCols: 20, textRows: 2, textCgramArea: 0, busInterface: B.PAR }],
  'ST7735': [F.ST7789, { buffWidth: 128, buffHeight: 160 }],
  'ST7789': [F.ST7789, {}],
  'Arduboy': [F.SSD1306, { busInterface: B.SPI4 }],
  'ESPboy': [F.ST7789, { inverted: 0, swapRB: 1, buffWidth: 136, buffHeight: 136, trimMode: 2, trimX: 6, trimY: 5, trimWidth: 128, trimHeight: 128, outputRot: 2 }],
  'M5Stack CoreS3': [F.ILI9341, { buffWidth: 320, buffHeight: 240 }],
  'PicoCalc': [F.ST7789, { buffWidth: 320, buffHeight: 320, outputRot: 0, flipMode: 1, swapRB: 1 }],
  'PicoPad': [F.ST7789, { outputRot: 3 }],
  'PicoSystem': [F.ST7789, { buffWidth: 240, buffHeight: 240, busInterface: B.PAR }],
  'Thumby': [F.SSD1306, { busInterface: B.SPI4, trimMode: 2, trimX: 28, trimY: 24, trimWidth: 72, trimHeight: 40 }],
  'Tinyjoypad': [F.SSD1306, { busInterface: B.I2C }],
  'Wio Terminal': [F.ILI9341, { flipMode: 2, outputRot: 3 }],
  'Xiamocon': [F.ST7789, { buffWidth: 240, buffHeight: 240 }],
};

/** Effective config for a preset (getPresetConfig), or null if unknown. */
export function presetConfig(name) {
  const def = PRESET_DEFS[name];
  if (!def) return null;
  const c = defaultConfig(def[0]);
  Object.assign(c, def[1]);
  normalizeConfig(c);
  if (c.trimMode !== 2) { c.trimX = 0; c.trimY = 0; c.trimWidth = c.buffWidth; c.trimHeight = c.buffHeight; }
  return c;
}

/** Preset + cfg overrides merged (setConfigValueById semantics, clamped). */
export function effectiveConfig(presetName, cfg) {
  const c = presetConfig(presetName) || presetConfig('SSD1306');
  for (const e of CONFIG_ENTRIES) {
    if (cfg && Object.prototype.hasOwnProperty.call(cfg, e.id)) {
      let v = Number(cfg[e.id]) | 0;
      if (e.type === 'bool') v = v ? 1 : 0;
      else v = Math.min(e.max, Math.max(e.min, v));
      c[e.id] = v;
    }
  }
  normalizeConfig(c);
  return c;
}

export function isEntryEnabled(entry, effective) {
  if (!entry.enable) return true;
  const v = effective[entry.enable.id];
  return v >= entry.enable.min && v <= entry.enable.max;
}
