#pragma once

// System menu + file browser drawn directly on the host LCD (text_draw).
//
//   HOME     : Launch / Apps
//   BROWSER  : TF card browser starting at APPS_DIR
//   CONFIRM  : "Write <file>?"  A = yes, B = no
//   PROGRAM  : progress screen while the host programs the card MCU
//   MESSAGE  : result / error text, A or B returns to the previous screen
//
// The menu never touches LcdTap, the ISP or the card bus itself; those are
// reached through UiHooks so main.cpp stays the only place that sequences
// hardware state.

#include <cstdint>

#include "ili9488.hpp"

namespace wcb {

struct UiHooks {
  bool (*cardPresent)(void* user);
  // Blocking. Must call UiMenu::progress() as it goes. Returns nullptr on
  // success or a short error message.
  const char* (*programApp)(const char* path, void* user);
  void* user;
};

class UiMenu {
 public:
  static constexpr int MAX_ENTRIES = 64;
  static constexpr int NAME_LEN = 64;
  static constexpr int PATH_LEN = 256;

  void init(Ili9488* lcd, const UiHooks& hooks);

  bool isVisible() const { return state_ != State::HIDDEN; }
  void open();
  void close();

  // Feed pressed-edge bitmask (HKEY_*; HOME is handled by the caller).
  void onKeysPressed(uint16_t edge);

  // Progress display during UiHooks::programApp.
  void progress(const char* stage, int percent);

 private:
  enum class State : uint8_t { HIDDEN, HOME, BROWSER, CONFIRM, PROGRAM, MESSAGE };

  struct Entry {
    char name[NAME_LEN];
    bool isDir;
  };

  // Screens
  void drawHome();
  void drawBrowser();
  void drawConfirm();
  void drawProgramBase();
  void showMessage(const char* line1, const char* line2, State back);

  // Browser helpers
  bool mountCard();
  bool loadDir(const char* path);
  void enterChild(const Entry& e);
  void goParent();
  void startProgram();

  // Drawing primitives (30 cols x 10 rows at scale 2)
  void clearScreen();
  void textRow(int row, const char* text, uint16_t fg, uint16_t bg);
  void footer(const char* text);

  Ili9488* lcd_ = nullptr;
  UiHooks hooks_{};
  State state_ = State::HIDDEN;
  State messageBack_ = State::HOME;

  int homeCursor_ = 0;

  char path_[PATH_LEN];
  Entry entries_[MAX_ENTRIES];
  int entryCount_ = 0;
  int cursor_ = 0;
  int scroll_ = 0;
  bool mounted_ = false;

  char selectedPath_[PATH_LEN];
  char lastStage_[16];
  int lastPercent_ = -1;
};

}  // namespace wcb
