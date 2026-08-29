#pragma once

// System clock: 312 MHz from PLL_SYS (XOSC 12 MHz x 130 / 5), vreg 1.25 V,
// flash (QMI) divider 3 (104 MHz), clk_peri 156 MHz. 312 MHz is a multiple
// of 12 MHz as Pico-PIO-USB requires and raises the LcdTap SPI capture
// margin against 62.5 MHz SCLK (same voltage / QMI timing as the LcdTap
// pico2 examples, which run 312 MHz on the same board class).
// Call once at boot before any peripheral is initialized.

namespace wcb {

void sysClockInit312();

}  // namespace wcb
