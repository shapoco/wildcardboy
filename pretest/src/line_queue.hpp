#pragma once

// Single-producer (core1) / single-consumer (core0) ring of 32-bit
// messages: packed output line ranges produced by the dirty-map scan, or
// MSG_FULL_REPAINT when the whole screen must be resent.

#include <cstdint>

#include "hardware/sync.h"

namespace wcb {

class LineQueue {
 public:
  static constexpr uint32_t CAP = 512;  // power of two
  static constexpr uint32_t MSG_FULL_REPAINT = 0xFFFFFFFFu;

  static uint32_t packLines(uint32_t y0, uint32_t y1) { return (y0 << 16) | (y1 & 0xFFFFu); }
  static uint32_t lineY0(uint32_t msg) { return msg >> 16; }
  static uint32_t lineY1(uint32_t msg) { return msg & 0xFFFFu; }

  bool push(uint32_t msg) {
    uint32_t h = head_, t = tail_;
    if (h - t >= CAP) return false;
    buf_[h & (CAP - 1)] = msg;
    __dmb();
    head_ = h + 1;
    return true;
  }

  bool pop(uint32_t* msg) {
    uint32_t h = head_, t = tail_;
    if (h == t) return false;
    __dmb();
    *msg = buf_[t & (CAP - 1)];
    __dmb();
    tail_ = t + 1;
    return true;
  }

  uint32_t count() const { return head_ - tail_; }
  bool empty() const { return head_ == tail_; }

  // Consumer side, only while the producer is stopped.
  void clear() { tail_ = head_; }

 private:
  volatile uint32_t head_ = 0;
  volatile uint32_t tail_ = 0;
  uint32_t buf_[CAP];
};

}  // namespace wcb
