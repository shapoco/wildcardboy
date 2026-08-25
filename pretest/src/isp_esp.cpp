#include "isp_esp.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "ff.h"
#include "hardware/gpio.h"
#include "hardware/uart.h"
#include "pico/stdlib.h"

#include "esp_loader.h"
#include "esp_loader_io.h"

#include "card_io.hpp"

namespace wcb {

// 921600 with the stub is the spec/04 target; drop to 460800 / 115200 here
// if the card-edge wiring turns out to be marginal.
static constexpr uint32_t FAST_BAUD = 921600;
static constexpr uint32_t INIT_BAUD = 115200;
static constexpr uint32_t CHUNK = 4096;

static uint8_t sChunk[CHUNK];

//-----------------------------------------------------------------------------
// esp-serial-flasher port: uart1 on the profile's UART pins, RESET / BOOTSEL
// through card_io (open-drain / negative-logic handling stays in one place).
// The bundled pi_pico port is not usable here: it drives RESET/BOOT as
// push-pull GPIOs and selects GPIO_FUNC_UART, which maps GPIO10/11 to the
// UART1 CTS/RTS role (TX/RX need FUNCSEL UART_AUX on RP2350).
//-----------------------------------------------------------------------------

struct WcbEspPort {
  esp_loader_port_t base;
  uart_inst_t* uart;
  uint txPin;
  uint rxPin;
  uint32_t timeEnd;
};

static esp_loader_error_t portInit(esp_loader_port_t* port) {
  WcbEspPort* p = container_of(port, WcbEspPort, base);
  uart_init(p->uart, INIT_BAUD);
  gpio_set_function(p->txPin, UART_FUNCSEL_NUM(p->uart, p->txPin));
  gpio_set_function(p->rxPin, UART_FUNCSEL_NUM(p->uart, p->rxPin));
  gpio_pull_up(p->rxPin);  // profile mode 9 (input + pull-up)
  return ESP_LOADER_SUCCESS;
}

static void portDeinit(esp_loader_port_t* port) {
  WcbEspPort* p = container_of(port, WcbEspPort, base);
  uart_deinit(p->uart);
  // Both pins back to Hi-Z (spec/04: drive the UART only while programming;
  // the card pulls ESP8266 RXD0 up).
  gpio_init(p->txPin);
  gpio_init(p->rxPin);
  gpio_disable_pulls(p->txPin);
  gpio_disable_pulls(p->rxPin);
}

static esp_loader_error_t portWrite(esp_loader_port_t* port, const uint8_t* data,
                                    const uint16_t size, const uint32_t timeout) {
  WcbEspPort* p = container_of(port, WcbEspPort, base);
  absolute_time_t deadline = make_timeout_time_ms(timeout);
  uint16_t pos = 0;
  while (pos < size && !time_reached(deadline)) {
    if (uart_is_writable(p->uart)) uart_putc_raw(p->uart, static_cast<char>(data[pos++]));
  }
  return (pos == size) ? ESP_LOADER_SUCCESS : ESP_LOADER_ERROR_TIMEOUT;
}

static esp_loader_error_t portRead(esp_loader_port_t* port, uint8_t* data,
                                   const uint16_t size, const uint32_t timeout) {
  WcbEspPort* p = container_of(port, WcbEspPort, base);
  absolute_time_t deadline = make_timeout_time_ms(timeout);
  uint16_t pos = 0;
  while (pos < size && !time_reached(deadline)) {
    if (uart_is_readable(p->uart)) data[pos++] = static_cast<uint8_t>(uart_getc(p->uart));
  }
  return (pos == size) ? ESP_LOADER_SUCCESS : ESP_LOADER_ERROR_TIMEOUT;
}

static void portEnterBootloader(esp_loader_port_t*) {
  // spec/04: BOOTSEL (GPIO0) low across the reset release.
  cardBootselAssert();
  cardResetAssert();
  sleep_ms(SERIAL_FLASHER_RESET_HOLD_TIME_MS);
  cardResetRelease();
  sleep_ms(SERIAL_FLASHER_BOOT_HOLD_TIME_MS);
  cardBootselRelease();
}

static void portResetTarget(esp_loader_port_t*) {
  cardResetAssert();
  sleep_ms(SERIAL_FLASHER_RESET_HOLD_TIME_MS);
  cardResetRelease();
}

static void portDelayMs(esp_loader_port_t*, uint32_t ms) { sleep_ms(ms); }

static void portStartTimer(esp_loader_port_t* port, uint32_t ms) {
  WcbEspPort* p = container_of(port, WcbEspPort, base);
  p->timeEnd = to_ms_since_boot(get_absolute_time()) + ms;
}

static uint32_t portRemainingTime(esp_loader_port_t* port) {
  WcbEspPort* p = container_of(port, WcbEspPort, base);
  int32_t remaining = static_cast<int32_t>(p->timeEnd - to_ms_since_boot(get_absolute_time()));
  return (remaining > 0) ? static_cast<uint32_t>(remaining) : 0;
}

static esp_loader_error_t portChangeRate(esp_loader_port_t* port, uint32_t baud) {
  WcbEspPort* p = container_of(port, WcbEspPort, base);
  uart_set_baudrate(p->uart, baud);
  return ESP_LOADER_SUCCESS;
}

static void portLog(esp_loader_port_t*, esp_loader_log_level_t, const char* fmt, va_list args) {
  printf("[isp-esp] ");
  vprintf(fmt, args);
  printf("\n");
}

static const esp_loader_port_ops_t WCB_ESP_OPS = {
    .init = portInit,
    .deinit = portDeinit,
    .enter_bootloader = portEnterBootloader,
    .reset_target = portResetTarget,
    .start_timer = portStartTimer,
    .remaining_time = portRemainingTime,
    .delay_ms = portDelayMs,
    .log = portLog,
    .log_hex = nullptr,
    .change_transmission_rate = portChangeRate,
    .write = portWrite,
    .read = portRead,
    .spi_set_cs = nullptr,
    .sdio_write = nullptr,
    .sdio_read = nullptr,
    .sdio_card_init = nullptr,
};

//-----------------------------------------------------------------------------
// Programming flow (spec/04 "UART によるアプリ書き込み")
//-----------------------------------------------------------------------------

const char* ispEspProgram(const char* path, IspUsbProgressFn progress, void* user) {
  if (!cardHasReset() || !cardHasBootsel()) return "Card has no RESET/BOOTSEL";
  uint txPin = 0, rxPin = 0;
  if (!cardIspUartPins(&txPin, &rxPin)) return "UART ISP pins not usable";

  FIL f;
  if (f_open(&f, path, FA_READ) != FR_OK) return "Cannot open file";
  const uint32_t size = static_cast<uint32_t>(f_size(&f));
  if (size == 0) {
    f_close(&f);
    return "Empty file";
  }
  const uint32_t alignedSize = (size + 3u) & ~3u;  // loader wants 4-byte multiples

  static WcbEspPort port;
  port.base.ops = &WCB_ESP_OPS;
  port.uart = uart1;
  port.txPin = txPin;
  port.rxPin = rxPin;

  static esp_loader_t loader;
  const char* err = nullptr;
  bool inited = false;
  uint64_t t0 = time_us_64();

  do {
    progress("Sync", 0, user);
    if (esp_loader_init_serial(&loader, &port.base) != ESP_LOADER_SUCCESS) {
      err = "UART init failed";
      break;
    }
    inited = true;

    // Stub first (fast baud + MD5 verify on ESP8266); plain ROM as fallback.
    esp_loader_connect_args_t args = ESP_LOADER_CONNECT_DEFAULT();
    bool stub = (esp_loader_connect_with_stub(&loader, &args) == ESP_LOADER_SUCCESS);
    if (!stub) {
      printf("[isp-esp] stub connect failed; retrying with the ROM loader\n");
      args = ESP_LOADER_CONNECT_DEFAULT();
      if (esp_loader_connect(&loader, &args) != ESP_LOADER_SUCCESS) {
        err = "Connect failed (not in download mode?)";
        break;
      }
    }
    printf("[isp-esp] connected: target=%d stub=%d\n",
           static_cast<int>(esp_loader_get_target(&loader)), stub);
    progress("Sync", 50, user);

    if (stub) {
      if (esp_loader_change_transmission_rate(&loader, FAST_BAUD) == ESP_LOADER_SUCCESS) {
        printf("[isp-esp] baudrate -> %lu\n", static_cast<unsigned long>(FAST_BAUD));
      } else {
        printf("[isp-esp] staying at %lu baud\n", static_cast<unsigned long>(INIT_BAUD));
      }
    }
    progress("Sync", 100, user);

    // Erase + program at flash offset 0 (single Arduino-style .bin).
    esp_loader_flash_cfg_t cfg = {};
    cfg.offset = 0;
    cfg.image_size = alignedSize;
    cfg.block_size = CHUNK;
    cfg.skip_verify = false;  // auto-skipped by the loader on ROM-only ESP8266
    progress("Erase", 0, user);
    if (esp_loader_flash_start(&loader, &cfg) != ESP_LOADER_SUCCESS) {
      err = "Flash erase failed";
      break;
    }
    progress("Erase", 100, user);

    progress("Write", 0, user);
    uint32_t remaining = size;
    uint32_t written = 0;
    while (remaining > 0) {
      UINT want = remaining < CHUNK ? remaining : CHUNK;
      UINT rd = 0;
      if (f_read(&f, sChunk, want, &rd) != FR_OK || rd != want) {
        err = "TF card read failed";
        break;
      }
      UINT sendLen = rd;
      if (remaining == rd && alignedSize != size) {
        // Final chunk: pad to the 4-byte boundary (always fits: pad <= 3 and
        // a short final chunk is < CHUNK; CHUNK itself is 4-byte aligned).
        const uint32_t pad = alignedSize - size;
        memset(sChunk + sendLen, 0xFF, pad);
        sendLen += pad;
      }
      if (esp_loader_flash_write(&loader, &cfg, sChunk, sendLen) != ESP_LOADER_SUCCESS) {
        err = "Flash write failed";
        break;
      }
      remaining -= rd;
      written += rd;
      progress("Write", static_cast<int>(written * 100ull / size), user);
    }
    if (err) break;

    progress("Verify", 0, user);
    esp_loader_error_t fe = esp_loader_flash_finish(&loader, &cfg);
    if (fe == ESP_LOADER_ERROR_INVALID_MD5) {
      err = "Verify failed (MD5)";
      break;
    }
    if (fe != ESP_LOADER_SUCCESS) {
      err = "Flash finish failed";
      break;
    }
    progress("Verify", 100, user);
  } while (false);

  f_close(&f);
  // UART pins back to Hi-Z, card back to the stopped state.
  if (inited) esp_loader_deinit(&loader);  // calls portDeinit
  cardBootselRelease();
  cardResetAssert();
  printf("[isp-esp] %s (%llu ms)\n", err ? err : "done",
         static_cast<unsigned long long>((time_us_64() - t0) / 1000));
  return err;
}

}  // namespace wcb
