// TinyUSB configuration: dual role.
//   rhport 0 = native USB, device (CDC for pico_stdio_usb)
//   rhport 1 = PIO-USB on GPIO30/31, host (MSC, for the USB ISP)
// Replaces pico_stdio_usb's own tusb_config.h (which steps aside once
// LIB_TINYUSB_DEVICE is defined); the CDC values below match it.
#ifndef _WCB_TUSB_CONFIG_H
#define _WCB_TUSB_CONFIG_H

#include "pico/stdio_usb.h"

// CFG_TUSB_MCU / CFG_TUSB_OS come from the SDK build.

#define CFG_TUSB_RHPORT0_MODE (OPT_MODE_DEVICE)
#define CFG_TUSB_RHPORT1_MODE (OPT_MODE_HOST)
#define BOARD_TUD_RHPORT 0
#define BOARD_TUH_RHPORT 1

#define CFG_TUD_ENABLED 1
#define CFG_TUH_ENABLED 1
#define CFG_TUH_RPI_PIO_USB 1

//--- device (CDC stdio) ---
#define CFG_TUD_ENDPOINT0_SIZE 64
#define CFG_TUD_CDC 1
#ifndef CFG_TUD_CDC_RX_BUFSIZE
#define CFG_TUD_CDC_RX_BUFSIZE 64
#endif
#ifndef CFG_TUD_CDC_TX_BUFSIZE
#define CFG_TUD_CDC_TX_BUFSIZE 64
#endif
#ifndef CFG_TUD_CDC_EP_BUFSIZE
#define CFG_TUD_CDC_EP_BUFSIZE 64
#endif

//--- host (MSC) ---
#define CFG_TUH_ENUMERATION_BUFSIZE 256
#define CFG_TUH_HUB 0
#define CFG_TUH_DEVICE_MAX 1
#define CFG_TUH_MSC 1
#define CFG_TUH_CDC 0
#define CFG_TUH_HID 0
#define CFG_TUH_VENDOR 0
#define CFG_TUH_MEM_SECTION
#define CFG_TUH_MEM_ALIGN __attribute__((aligned(4)))

#endif
