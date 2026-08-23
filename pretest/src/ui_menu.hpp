#pragma once

// System menu + file browser drawn directly on the host LCD (text_draw).
//
//   HOME         : Start card / Stop card, Apps, Profile
//   BROWSER      : TF card browser (purpose: app image or card profile)
//   CONFIRM      : "Write <file>?" / "Overwrite card profile?"  A = yes, B = no
//   PROGRAM      : progress screen while the host programs the MCU / EEPROM
//   MESSAGE      : result / error text, A or B returns to the previous screen
//   PROMPT_START : "Start card?" (after insertion or app programming)
//
// The menu never touches LcdTap, the ISP, the EEPROM or the card bus
// itself; those are reached through UiHooks so main.cpp stays the only
// place that sequences hardware state.

#include <cstddef>
#include <cstdint>

#include "ili9488.hpp"

namespace wcb {

enum class CardState : uint8_t { NONE, INVALID, STOPPED, RUNNING };

struct UiHooks {
  CardState (*cardState)(void* user);
  // True while the running card owns the TF card (host cannot browse it).
  bool (*tfBusy)(void* user);
  const char* (*cardId)(void* user);   // nullptr when no profile
  // Apps directory of the card ("/WCB/Cards/<id>/Apps"), or nullptr.
  const char* (*appsDir)(void* user);
  // Blocking. Stops the card if it is running and leaves it stopped. Must
  // call UiMenu::progress() as it goes. Returns nullptr on success or a
  // short error message.
  const char* (*programApp)(const char* path, void* user);
  // Load + validate a profile .hex; fills id/name for the confirmation.
  const char* (*validateProfile)(const char* path, char* id, size_t idCap,
                                 char* name, size_t nameCap, void* user);
  // Write the profile validated last (blocking, calls progress()).
  const char* (*writeProfile)(void* user);
  bool (*startCard)(void* user);
  bool (*stopCard)(void* user);
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

  // Standalone "Start card <id>?" prompt (A = start, B = keep stopped).
  void promptStart();

  // Feed pressed-edge bitmask (HKEY_*; HOME is handled by the caller).
  void onKeysPressed(uint16_t edge);

  // Progress display during UiHooks::programApp / writeProfile.
  void progress(const char* stage, int percent);

 private:
  enum class State : uint8_t { HIDDEN, HOME, BROWSER, CONFIRM, PROGRAM, MESSAGE, PROMPT_START };
  enum class Purpose : uint8_t { APP, PROFILE };

  struct Entry {
    char name[NAME_LEN];
    bool isDir;
  };

  // Screens
  void drawHome();
  void drawBrowser();
  void drawConfirm();
  void drawProgramBase();
  void drawPromptStart(const char* headline);
  void showMessage(const char* line1, const char* line2, State back);

  // Browser helpers
  bool mountCard();
  bool loadDir(const char* path);
  void openBrowser(Purpose purpose, const char* startDir);
  void enterChild(const Entry& e);
  void goParent();
  void selectFile(const Entry& e);
  void startJob();

  // Drawing primitives (30 cols x 10 rows at scale 2)
  void clearScreen();
  void textRow(int row, const char* text, uint16_t fg, uint16_t bg);
  void footer(const char* text);

  Ili9488* lcd_ = nullptr;
  UiHooks hooks_{};
  State state_ = State::HIDDEN;
  State messageBack_ = State::HOME;
  Purpose purpose_ = Purpose::APP;

  int homeCursor_ = 0;

  char path_[PATH_LEN];
  Entry entries_[MAX_ENTRIES];
  int entryCount_ = 0;
  int cursor_ = 0;
  int scroll_ = 0;
  bool mounted_ = false;

  char selectedPath_[PATH_LEN];
  char profileId_[24];
  char profileName_[72];
  char lastStage_[16];
  int lastPercent_ = -1;
};

}  // namespace wcb
