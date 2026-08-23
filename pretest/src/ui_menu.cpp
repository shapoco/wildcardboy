#include "ui_menu.hpp"

#include <cstdio>
#include <cstring>

#include "ff.h"

#include "board_pins.hpp"
#include "sd_spi.hpp"
#include "text_draw.hpp"

namespace wcb {

static constexpr int COLS = 30;          // 480 / 16
static constexpr int ROWS = 10;          // 320 / 32
static constexpr int ROW_H = 32;
static constexpr int LIST_FIRST_ROW = 1;
static constexpr int LIST_ROWS = 7;      // rows 1..7
static constexpr int FOOTER_Y = 320 - 16;

static constexpr uint16_t COL_BG = COLOR_BLACK;
static constexpr uint16_t COL_TEXT = COLOR_WHITE;
static constexpr uint16_t COL_TITLE = COLOR_YELLOW;
static constexpr uint16_t COL_DIR = rgb565(120, 200, 255);
static constexpr uint16_t COL_SEL_BG = rgb565(40, 80, 160);
static constexpr uint16_t COL_HELP = COLOR_GRAY;
static constexpr uint16_t COL_ERR = rgb565(255, 96, 96);
static constexpr uint16_t COL_OK = COLOR_GREEN;

static FATFS sFs;

//-----------------------------------------------------------------------------
// Primitives
//-----------------------------------------------------------------------------

void UiMenu::clearScreen() { lcd_->clear(COL_BG); }

// Draw one 30-column row, padded/truncated, at scale 2.
void UiMenu::textRow(int row, const char* text, uint16_t fg, uint16_t bg) {
  char buf[COLS + 1];
  int n = 0;
  for (; n < COLS && text[n]; ++n) buf[n] = text[n];
  for (; n < COLS; ++n) buf[n] = ' ';
  buf[COLS] = '\0';
  drawText(*lcd_, 0, row * ROW_H, buf, fg, bg, 2);
}

void UiMenu::footer(const char* text) {
  lcd_->fillRect(0, FOOTER_Y, Ili9488::LCD_W, 16, COL_BG);
  drawText(*lcd_, 0, FOOTER_Y, text, COL_HELP, COL_BG, 1);
}

//-----------------------------------------------------------------------------
// Lifecycle
//-----------------------------------------------------------------------------

void UiMenu::init(Ili9488* lcd, const UiHooks& hooks) {
  lcd_ = lcd;
  hooks_ = hooks;
  state_ = State::HIDDEN;
  strncpy(path_, CARDS_DIR, sizeof(path_) - 1);
  path_[sizeof(path_) - 1] = '\0';
}

void UiMenu::open() {
  state_ = State::HOME;
  homeCursor_ = 0;
  drawHome();
}

void UiMenu::close() {
  if (mounted_) {
    f_unmount("");
    mounted_ = false;
  }
  state_ = State::HIDDEN;
}

//-----------------------------------------------------------------------------
// HOME
//-----------------------------------------------------------------------------

static const char* HOME_ITEMS[] = {"Launch", "Apps", "Profile"};
static constexpr int HOME_ITEM_COUNT = 3;

void UiMenu::drawHome() {
  clearScreen();
  textRow(0, " WildCardBoy", COL_TITLE, COL_BG);
  for (int i = 0; i < HOME_ITEM_COUNT; ++i) {
    char line[COLS + 1];
    snprintf(line, sizeof(line), " %c %s", (i == homeCursor_) ? '>' : ' ',
             HOME_ITEMS[i]);
    textRow(2 + i, line, COL_TEXT, (i == homeCursor_) ? COL_SEL_BG : COL_BG);
  }
  footer("U/D:Select  A:Open  HOME:Close");
}

//-----------------------------------------------------------------------------
// BROWSER
//-----------------------------------------------------------------------------

bool UiMenu::mountCard() {
  if (mounted_) {
    f_unmount("");
    mounted_ = false;
  }
  sdMarkNotReady();  // re-detect: the card may have been swapped
  FRESULT fr = f_mount(&sFs, "", 1);
  if (fr != FR_OK) {
    printf("[ui] f_mount failed (%d)\n", fr);
    return false;
  }
  mounted_ = true;
  return true;
}

bool UiMenu::loadDir(const char* path) {
  DIR dir;
  FRESULT fr = f_opendir(&dir, path);
  if (fr != FR_OK) {
    printf("[ui] f_opendir(%s) failed (%d)\n", path, fr);
    return false;
  }
  static FILINFO fno;
  entryCount_ = 0;
  while (entryCount_ < MAX_ENTRIES) {
    fr = f_readdir(&dir, &fno);
    if (fr != FR_OK || fno.fname[0] == '\0') break;
    if (fno.fattrib & (AM_HID | AM_SYS)) continue;
    Entry& e = entries_[entryCount_++];
    strncpy(e.name, fno.fname, NAME_LEN - 1);
    e.name[NAME_LEN - 1] = '\0';
    e.isDir = (fno.fattrib & AM_DIR) != 0;
  }
  f_closedir(&dir);

  // Directories first, then case-insensitive by name (insertion sort).
  for (int i = 1; i < entryCount_; ++i) {
    Entry key = entries_[i];
    int j = i - 1;
    while (j >= 0) {
      const Entry& o = entries_[j];
      bool after = (o.isDir == key.isDir) ? (strcasecmp(o.name, key.name) > 0)
                                          : (!o.isDir && key.isDir);
      if (!after) break;
      entries_[j + 1] = entries_[j];
      --j;
    }
    entries_[j + 1] = key;
  }

  strncpy(path_, path, sizeof(path_) - 1);
  path_[sizeof(path_) - 1] = '\0';
  cursor_ = 0;
  scroll_ = 0;
  return true;
}

void UiMenu::drawBrowser() {
  clearScreen();
  // Title: path, truncated from the left.
  char title[COLS + 1];
  size_t plen = strlen(path_);
  if (plen > static_cast<size_t>(COLS)) {
    snprintf(title, sizeof(title), "..%s", path_ + (plen - (COLS - 2)));
  } else {
    strncpy(title, path_, sizeof(title));
    title[COLS] = '\0';
  }
  textRow(0, title, COL_TITLE, COL_BG);

  if (entryCount_ == 0) {
    textRow(LIST_FIRST_ROW, "  (empty)", COL_HELP, COL_BG);
  }
  for (int r = 0; r < LIST_ROWS; ++r) {
    int idx = scroll_ + r;
    if (idx >= entryCount_) break;
    const Entry& e = entries_[idx];
    char line[COLS + 1];
    snprintf(line, sizeof(line), "%c%s%s", (idx == cursor_) ? '>' : ' ',
             e.isDir ? "/" : " ", e.name);
    bool sel = (idx == cursor_);
    textRow(LIST_FIRST_ROW + r, line, e.isDir ? COL_DIR : COL_TEXT,
            sel ? COL_SEL_BG : COL_BG);
  }
  footer("U/D:Select  L:Parent  R:Child  A:Open  B:Cancel");
}

void UiMenu::enterChild(const Entry& e) {
  static char next[PATH_LEN];
  if (strcmp(path_, "/") == 0) {
    snprintf(next, sizeof(next), "/%s", e.name);
  } else {
    snprintf(next, sizeof(next), "%s/%s", path_, e.name);
  }
  if (loadDir(next)) {
    drawBrowser();
  } else {
    showMessage("Cannot open directory", e.name, State::BROWSER);
  }
}

void UiMenu::goParent() {
  if (strcmp(path_, "/") == 0 || path_[0] == '\0') {
    // Root: leave the browser.
    state_ = State::HOME;
    drawHome();
    return;
  }
  static char parent[PATH_LEN];
  strncpy(parent, path_, sizeof(parent));
  parent[sizeof(parent) - 1] = '\0';
  char* slash = strrchr(parent, '/');
  if (!slash || slash == parent) {
    strcpy(parent, "/");
  } else {
    *slash = '\0';
  }
  if (loadDir(parent)) {
    drawBrowser();
  } else {
    loadDir("/");
    drawBrowser();
  }
}

//-----------------------------------------------------------------------------
// CONFIRM / PROGRAM / MESSAGE
//-----------------------------------------------------------------------------

void UiMenu::drawConfirm() {
  clearScreen();
  const char* name = strrchr(selectedPath_, '/');
  name = name ? name + 1 : selectedPath_;
  char line[COLS + 1];
  if (purpose_ == Purpose::PROFILE) {
    textRow(0, " Overwrite card profile?", COL_TITLE, COL_BG);
    snprintf(line, sizeof(line), "  file: %s", name);
    textRow(2, line, COL_TEXT, COL_BG);
    snprintf(line, sizeof(line), "  id:   %s", profileId_);
    textRow(3, line, COL_TEXT, COL_BG);
    snprintf(line, sizeof(line), "  name: %s", profileName_);
    textRow(4, line, COL_TEXT, COL_BG);
  } else {
    textRow(0, " Write to card MCU?", COL_TITLE, COL_BG);
    snprintf(line, sizeof(line), "  %s", name);
    textRow(3, line, COL_TEXT, COL_BG);
  }
  textRow(6, "   A:Yes    B:No", COL_HELP, COL_BG);
  footer("A:Write  B:Cancel");
}

void UiMenu::drawProgramBase() {
  clearScreen();
  textRow(0, purpose_ == Purpose::PROFILE ? " Writing card profile" : " Programming card MCU",
          COL_TITLE, COL_BG);
  const char* name = strrchr(selectedPath_, '/');
  name = name ? name + 1 : selectedPath_;
  char line[COLS + 1];
  snprintf(line, sizeof(line), "  %s", name);
  textRow(2, line, COL_TEXT, COL_BG);
  footer("Do not remove the card");
  lastStage_[0] = '\0';
  lastPercent_ = -1;
}

void UiMenu::progress(const char* stage, int percent) {
  if (state_ != State::PROGRAM) return;
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;
  bool stageChanged = strncmp(stage, lastStage_, sizeof(lastStage_)) != 0;
  if (!stageChanged && percent == lastPercent_) return;
  if (stageChanged) {
    strncpy(lastStage_, stage, sizeof(lastStage_) - 1);
    lastStage_[sizeof(lastStage_) - 1] = '\0';
  }
  char line[COLS + 1];
  snprintf(line, sizeof(line), "  %-8s %3d%%", stage, percent);
  textRow(4, line, COL_TEXT, COL_BG);

  // Bar: 400 px wide at row 6.
  const int bx = 40, by = 6 * ROW_H + 8, bw = 400, bh = 16;
  if (stageChanged || lastPercent_ < 0) {
    lcd_->fillRect(bx - 2, by - 2, bw + 4, bh + 4, COL_HELP);
    lcd_->fillRect(bx, by, bw, bh, COL_BG);
  }
  int fillW = bw * percent / 100;
  if (fillW > 0) lcd_->fillRect(bx, by, fillW, bh, COL_OK);
  lastPercent_ = percent;
}

void UiMenu::showMessage(const char* line1, const char* line2, State back) {
  state_ = State::MESSAGE;
  messageBack_ = back;
  clearScreen();
  char buf[COLS + 1];
  snprintf(buf, sizeof(buf), "  %s", line1);
  textRow(3, buf, COL_TEXT, COL_BG);
  if (line2) {
    snprintf(buf, sizeof(buf), "  %s", line2);
    textRow(4, buf, COL_HELP, COL_BG);
  }
  footer("A/B:Back");
}

void UiMenu::startJob() {
  const char* err;
  state_ = State::PROGRAM;
  drawProgramBase();
  if (purpose_ == Purpose::PROFILE) {
    err = hooks_.writeProfile ? hooks_.writeProfile(hooks_.user) : "no writer";
  } else {
    if (hooks_.cardState(hooks_.user) != CardState::READY) {
      showMessage("No Logic Card", nullptr, State::BROWSER);
      return;
    }
    err = hooks_.programApp ? hooks_.programApp(selectedPath_, hooks_.user)
                            : "no programmer";
  }
  if (err) {
    showMessage("FAILED", err, State::BROWSER);
    textRow(3, "  FAILED", COL_ERR, COL_BG);  // red-tint the headline
  } else if (purpose_ == Purpose::PROFILE) {
    showMessage("Done", "Card will be re-detected", State::HOME);
    textRow(3, "  Done", COL_OK, COL_BG);
  } else {
    showMessage("Done", "Press HOME to return to the game", State::BROWSER);
    textRow(3, "  Done", COL_OK, COL_BG);
  }
}

// A file was chosen in the browser: validate (profiles) and ask.
void UiMenu::selectFile(const Entry& e) {
  if (strcmp(path_, "/") == 0) {
    snprintf(selectedPath_, sizeof(selectedPath_), "/%s", e.name);
  } else {
    snprintf(selectedPath_, sizeof(selectedPath_), "%s/%s", path_, e.name);
  }
  if (purpose_ == Purpose::PROFILE) {
    profileId_[0] = profileName_[0] = '\0';
    const char* err = hooks_.validateProfile
                          ? hooks_.validateProfile(selectedPath_, profileId_, sizeof(profileId_),
                                                   profileName_, sizeof(profileName_), hooks_.user)
                          : "no validator";
    if (err) {
      showMessage("Invalid profile", err, State::BROWSER);
      return;
    }
  }
  state_ = State::CONFIRM;
  drawConfirm();
}

void UiMenu::openBrowser(Purpose purpose, const char* startDir) {
  purpose_ = purpose;
  if (!mountCard()) {
    showMessage("No TF card", "Insert a TF card and retry", State::HOME);
    return;
  }
  state_ = State::BROWSER;
  if (!startDir || !loadDir(startDir)) {
    if (!loadDir("/")) {
      showMessage("Cannot read TF card", nullptr, State::HOME);
      return;
    }
  }
  drawBrowser();
}

//-----------------------------------------------------------------------------
// Input
//-----------------------------------------------------------------------------

void UiMenu::onKeysPressed(uint16_t edge) {
  if (edge == 0) return;
  switch (state_) {
    case State::HIDDEN: break;

    case State::HOME:
      if (edge & HKEY_U) {
        if (homeCursor_ > 0) homeCursor_--;
        drawHome();
      } else if (edge & HKEY_D) {
        if (homeCursor_ < HOME_ITEM_COUNT - 1) homeCursor_++;
        drawHome();
      } else if (edge & HKEY_A) {
        if (homeCursor_ == 0) {
          close();  // Launch
        } else if (homeCursor_ == 1) {
          if (hooks_.cardState(hooks_.user) != CardState::READY) {
            showMessage("No Logic Card", "Apps needs a running card", State::HOME);
            break;
          }
          openBrowser(Purpose::APP, hooks_.appsDir ? hooks_.appsDir(hooks_.user) : nullptr);
        } else {
          openBrowser(Purpose::PROFILE, CARDS_DIR);
        }
      }
      break;

    case State::BROWSER:
      if (edge & HKEY_U) {
        if (cursor_ > 0) cursor_--;
        if (cursor_ < scroll_) scroll_ = cursor_;
        drawBrowser();
      } else if (edge & HKEY_D) {
        if (cursor_ < entryCount_ - 1) cursor_++;
        if (cursor_ >= scroll_ + LIST_ROWS) scroll_ = cursor_ - LIST_ROWS + 1;
        drawBrowser();
      } else if (edge & (HKEY_L | HKEY_B)) {
        goParent();
      } else if (edge & HKEY_R) {
        if (cursor_ < entryCount_ && entries_[cursor_].isDir) {
          enterChild(entries_[cursor_]);
        }
      } else if (edge & HKEY_A) {
        if (cursor_ < entryCount_) {
          const Entry& e = entries_[cursor_];
          if (e.isDir) {
            enterChild(e);
          } else {
            selectFile(e);
          }
        }
      }
      break;

    case State::CONFIRM:
      if (edge & HKEY_A) {
        startJob();
      } else if (edge & HKEY_B) {
        state_ = State::BROWSER;
        drawBrowser();
      }
      break;

    case State::PROGRAM: break;  // blocking; no input

    case State::MESSAGE:
      if (edge & (HKEY_A | HKEY_B)) {
        state_ = messageBack_;
        if (state_ == State::BROWSER) {
          drawBrowser();
        } else {
          state_ = State::HOME;
          drawHome();
        }
      }
      break;
  }
}

}  // namespace wcb
