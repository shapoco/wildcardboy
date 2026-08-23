#pragma once

// System clock: 288 MHz from PLL_SYS (XOSC 12 MHz x 120 / 5), vreg 1.25 V,
// flash (QMI) divider 3 (96 MHz), clk_peri 144 MHz. 288 MHz is a multiple
// of 12 MHz as Pico-PIO-USB requires (48/96 MHz dividers are integers) and
// lets the LcdTap SPI capture keep up with 62.5 MHz SCLK.
// Call once at boot before any peripheral is initialized.

namespace wcb {

void sysClockInit288();

}  // namespace wcb
