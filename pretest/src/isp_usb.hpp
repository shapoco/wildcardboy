#pragma once

// USB ISP for RP2040/RP2350 logic cards: put the MCU into BOOTSEL (RESET +
// BOOTSEL lines), let core 1's USB host mount its MSC device, stream the
// UF2 blocks from the TF card straight into the device's data area and wait
// for the reboot (USB detach) that marks the end of the flash write.
// Runs on core 0, blocking. The card must be stopped (RESET asserted) on
// entry and is left stopped.

namespace wcb {

using IspUsbProgressFn = void (*)(const char* stage, int percent, void* user);

// Returns nullptr on success or a short error message.
const char* ispUsbProgram(const char* path, IspUsbProgressFn progress, void* user);

}  // namespace wcb
