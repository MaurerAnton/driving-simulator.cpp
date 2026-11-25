// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/forkliftpanel.ts @ v1.7.7
/**
 * The forklift's display and the mast's buttons (plain DOM, like the HUD):
 *
 *  - the display, small in the top corner: the load moment against the
 *    point where the rear wheels lift (amber from 80 %, red from 100 %),
 *    the side stability the same way, the load on the forks against what
 *    the load chart allows at its load centre, the forks' height and the
 *    mast's tilt;
 *  - on a touch screen, buttons for the mast where the indicators usually
 *    are (a forklift has no use for them on the yard): forks up and down,
 *    the mast tilted back and forwards, the side-shift left and right –
 *    held down, they work like the keys (they press the same keys of
 *    input/input.h);
 *  - with keys, a line at the bottom saying which keys work the mast.
 * (Its style sheet: web/css/forklift.css.)
 */
#pragma once

#include <functional>
#include <string>

#include "i18n/index.h"
#include "platform/dom.h"

namespace fs {

/** The mast's keys (by position: KeyboardEvent.code) – read by Input.forkInput. */
struct ForkKeys {
  const char* up = "PageUp";
  const char* down = "PageDown";
  const char* back = "Home";
  const char* forward = "End";
  const char* left = "Insert";
  const char* right = "Delete";
};
inline constexpr ForkKeys FORK_KEYS{};

/** What the display shows. */
struct ForkInfo {
  /** the load moment and the side stability (1: the wheels lift) */
  double moment = 0;
  double side = 0;
  /** the load on the forks (kg; 0: none), what the load chart allows at its load centre */
  double load = 0;
  double allowed = 0;
  /** the forks' height (m), the mast's tilt (deg, + back) */
  double lift = 0;
  double tilt = 0;
};

class ForkPanel {
 public:
  const dom::El root = dom::el("div", "fork-ui");

  ForkPanel(const dom::El& parent, std::function<Lang()> lang, std::function<void(const std::string& code, bool down)> press, std::function<void()> click);

  /** Touch screen or keys: the buttons, or the line of keys. */
  void setMode(bool touch);
  void update(const ForkInfo& i);
  void dispose();

 private:
  std::function<Lang()> lang;
  const dom::El bar = dom::el("i");
  const dom::El barText = dom::el("b");
  const dom::El sideBar = dom::el("i");
  const dom::El vals = dom::el("div", "fork-vals");
  const dom::El keys = dom::el("div", "fork-keys");
  const dom::El box = dom::el("div", "fork-box");
  std::string keysLang;
  std::string last;
};

}  // namespace fs
