#include "sys_clock.hpp"

#include "hardware/clocks.h"
#include "hardware/pll.h"
#include "hardware/structs/ioqspi.h"
#include "hardware/structs/qmi.h"
#include "hardware/sync.h"
#include "hardware/vreg.h"
#include "pico/stdlib.h"

#include "board_pins.hpp"

namespace wcb {

// Flash timing for 312 MHz: clkdiv 3 (104 MHz), rxdelay 2 (identical to the
// LcdTap pico2 example at 312 MHz). Runs from SRAM and waits for the QSPI
// bus to be idle.
static void __no_inline_not_in_flash_func(setQmiTiming312)() {
  while ((ioqspi_hw->io[1].status & IO_QSPI_GPIO_QSPI_SS_STATUS_OUTTOPAD_BITS) !=
         IO_QSPI_GPIO_QSPI_SS_STATUS_OUTTOPAD_BITS) {
  }
  qmi_hw->m[0].timing = 0x40000203u;
  volatile uint32_t* xip = (volatile uint32_t*)0x14000000u;
  (void)*xip;
}

void __no_inline_not_in_flash_func(sysClockInit312)() {
  const uint32_t intr = save_and_disable_interrupts();

  // Slow the flash clock first so it stays in spec during the transition.
  hw_write_masked(&qmi_hw->m[0].timing, 6, QMI_M0_TIMING_CLKDIV_BITS);
  {
    volatile uint32_t* xip = (volatile uint32_t*)0x14000000u;
    (void)*xip;
  }

  vreg_set_voltage(VREG_VOLTAGE_1_25);
  busy_wait_us(2000);

  // clk_sys -> clk_ref (XOSC) while PLL_SYS is reprogrammed.
  hw_clear_bits(&clocks_hw->clk[clk_sys].ctrl, CLOCKS_CLK_SYS_CTRL_SRC_BITS);
  while (clocks_hw->clk[clk_sys].selected != 0x1u) tight_loop_contents();

  // VCO 1560 MHz / 5 / 1 = 312 MHz.
  pll_init(pll_sys, PLL_SYS_REFDIV, 1560 * MHZ, 5, 1);

  clock_configure(clk_sys, CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLKSRC_CLK_SYS_AUX,
                  CLOCKS_CLK_SYS_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS, SYS_CLOCK_HZ,
                  SYS_CLOCK_HZ);
  clock_configure(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
                  SYS_CLOCK_HZ, SYS_CLOCK_HZ / 2);

  setQmiTiming312();
  restore_interrupts(intr);
}

}  // namespace wcb
