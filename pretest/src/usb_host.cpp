#include "usb_host.hpp"

#include "hardware/dma.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#include "pio_usb.h"
#include "tusb.h"

#include "board_pins.hpp"

namespace wcb {

static bool sInited = false;
static bool sRunning = false;  // tuh_task() serviced (USB_HOST mode)
static volatile bool sInitFailed = false;
static volatile bool sAttached = false;
static volatile bool sMounted = false;
static volatile uint8_t sDaddr = 0;
static volatile uint32_t sBlockSize = 512;
static volatile uint32_t sBlockCount = 0;
static volatile uint32_t sAttachCount = 0, sDetachCount = 0, sMountCount = 0, sUnmountCount = 0;

enum ReqState : int { REQ_IDLE = 0, REQ_PENDING, REQ_OK, REQ_ERR };
static struct {
  volatile uint32_t lba;
  const uint8_t* volatile buf;
  volatile uint32_t count;
  volatile int state;
} sReq;

static volatile bool sCbDone = false;
static volatile bool sCbOk = false;

static constexpr uint32_t XFER_TIMEOUT_MS = 5000;

//-----------------------------------------------------------------------------
// Core 1
//-----------------------------------------------------------------------------

void usbHostStart() {
  if (!sInited) {
    static pio_usb_configuration_t cfg = PIO_USB_DEFAULT_CONFIG;
    cfg.pin_dp = PIN_LCUSB_DP;
    cfg.pio_tx_num = PIO_USB_INDEX;
    cfg.sm_tx = 0;
    cfg.pio_rx_num = PIO_USB_INDEX;
    cfg.sm_rx = 1;
    cfg.sm_eop = 2;
    // pio_usb claims the channel itself (dma_claim_mask); just find a free one.
    {
      int ch = dma_claim_unused_channel(true);
      dma_channel_unclaim(ch);
      cfg.tx_ch = static_cast<uint8_t>(ch);
    }
    // SOF timer on a core-1 alarm pool so its IRQ stays on this core.
    static alarm_pool_t* pool = alarm_pool_create(2, 8);
    cfg.alarm_pool = pool;
    tuh_configure(BOARD_TUH_RHPORT, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &cfg);

    tusb_rhport_init_t hostInit = {.role = TUSB_ROLE_HOST, .speed = TUSB_SPEED_AUTO};
    if (!tusb_init(BOARD_TUH_RHPORT, &hostInit)) {
      sInitFailed = true;
      return;
    }
    sInited = true;
  }
  // Note: pio_usb_host_stop()/restart() are not usable under the TinyUSB
  // integration (their flags are only consumed by pio_usb_host_task()), so
  // the host stays initialized; "stopped" just means tuh_task() is not
  // serviced. The 1 ms SOF timer keeps running on this core.
  sRunning = true;
}

void usbHostStop() {
  sRunning = false;
  if (sReq.state == REQ_PENDING) sReq.state = REQ_ERR;
}

static bool writeCb(uint8_t, tuh_msc_complete_data_t const* d) {
  sCbOk = (d->csw->status == 0);
  sCbDone = true;
  return true;
}

void usbHostTask() {
  if (!sRunning) return;
  tuh_task();
  if (sReq.state != REQ_PENDING) return;
  if (!sMounted) {
    sReq.state = REQ_ERR;
    return;
  }
  sCbDone = false;
  sCbOk = false;
  if (!tuh_msc_write10(sDaddr, 0, sReq.buf, sReq.lba, static_cast<uint16_t>(sReq.count), writeCb, 0)) {
    sReq.state = REQ_ERR;
    return;
  }
  absolute_time_t deadline = make_timeout_time_ms(XFER_TIMEOUT_MS);
  while (!sCbDone && !time_reached(deadline) && sMounted) tuh_task();
  sReq.state = (sCbDone && sCbOk) ? REQ_OK : REQ_ERR;
}

//-----------------------------------------------------------------------------
// Core 0
//-----------------------------------------------------------------------------

bool usbHostInitFailed() { return sInitFailed; }
bool usbHostDeviceAttached() { return sAttached; }
bool usbHostMscMounted() { return sMounted; }
void usbHostCounters(uint32_t* attach, uint32_t* detach, uint32_t* mount, uint32_t* unmount) {
  *attach = sAttachCount;
  *detach = sDetachCount;
  *mount = sMountCount;
  *unmount = sUnmountCount;
}
uint32_t usbHostMscBlockSize() { return sBlockSize; }
uint32_t usbHostMscBlockCount() { return sBlockCount; }

bool usbHostWriteBlocks(uint32_t lba, const uint8_t* buf, uint32_t count, uint32_t timeoutMs) {
  if (!sMounted) return false;
  sReq.lba = lba;
  sReq.buf = buf;
  sReq.count = count;
  __dmb();
  sReq.state = REQ_PENDING;
  __sev();
  absolute_time_t deadline = make_timeout_time_ms(timeoutMs);
  while (sReq.state == REQ_PENDING) {
    if (time_reached(deadline)) return false;
    tight_loop_contents();
  }
  return sReq.state == REQ_OK;
}

}  // namespace wcb

//-----------------------------------------------------------------------------
// TinyUSB host callbacks (core 1). No printf here: stdio would run the
// device-side tud_task() on this core, racing core 0's IRQ-driven one.
//-----------------------------------------------------------------------------
extern "C" {

void tuh_mount_cb(uint8_t daddr) {
  (void)daddr;
  wcb::sAttached = true;
  wcb::sAttachCount = wcb::sAttachCount + 1;
}

void tuh_umount_cb(uint8_t daddr) {
  (void)daddr;
  wcb::sAttached = false;
  wcb::sMounted = false;
  wcb::sDetachCount = wcb::sDetachCount + 1;
}

void tuh_msc_mount_cb(uint8_t daddr) {
  wcb::sDaddr = daddr;
  wcb::sBlockSize = tuh_msc_get_block_size(daddr, 0);
  wcb::sBlockCount = tuh_msc_get_block_count(daddr, 0);
  wcb::sMounted = true;
  wcb::sMountCount = wcb::sMountCount + 1;
}

void tuh_msc_umount_cb(uint8_t daddr) {
  (void)daddr;
  wcb::sMounted = false;
  wcb::sUnmountCount = wcb::sUnmountCount + 1;
}

}  // extern "C"
