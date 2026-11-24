// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/hudlayout.ts @ v1.7.7
/**
 * Arranging the HUD: the player drags its parts where they suit them.
 *
 * "Arrange the display" (a tile of the pause menu, a button of the settings
 * while driving – or a long press on a part of the display) lays a layer over
 * the picture: each part of the HUD named below is outlined and can be
 * dragged with a finger or the mouse – also parts that are hidden just now
 * (shown dimmed meanwhile), and the buttons on the corner map, which can be
 * taken out of it onto the main screen (the "my position" button, the button
 * of the big map).  "Done" ends it, "Reset layout" puts everything back.
 *
 * A part that stays in its place in the page is only shifted (the CSS
 * property `translate`, which adds to whatever `transform` it has, so its
 * own styles stay as they are); a part taken out of the map is moved into
 * the HUD and placed there.  Positions are kept in this browser as shares of
 * the screen, one set for the keyboard layout and one for the touch layout
 * of the HUD (as the corner map's own place, ui/minimap.ts – the map itself
 * is moved by dragging its rim).
 *
 * So that the player finds it: a long press (a finger held still for a
 * moment) on one of the display's read-outs – the street name, the speed,
 * the score, the clock … – begins arranging (not on the on-screen controls,
 * which are held while driving), and once, some seconds into the first
 * drive, a short notice says so.
 */
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "platform/dom.h"
#include "platform/json.h"

namespace fs {

/** how long (ms) a finger is held still to begin arranging */
inline constexpr double LONG_PRESS_MS = 650;
/** the parts a long press on which begins arranging: read-outs, never controls that are held while driving */
inline const std::vector<std::string> LONG_PRESS_IDS = {"nav", "assist", "dash", "tip", "score", "keys", "fps", "clock"};

class HudLayout {
 public:
  HudLayout(const dom::El& hud, std::function<std::string(const std::string& key)> t, std::function<void(const std::string& text)> hint = nullptr);
  /** (the page's listeners name this object: they go with it) */
  ~HudLayout();
  HudLayout(const HudLayout&) = delete;
  HudLayout& operator=(const HudLayout&) = delete;

  bool arranging() const;

  /** Called with the HUD's update (seconds): finds parts that appeared, follows the layout (keys / touch). Cheap: twice a second. */
  void tick(double now);

  /** Begin arranging: the parts outlined and draggable, a bar with "Done" and "Reset layout". */
  void arrange();

  /** End arranging. */
  void stop();

  /** Everything back where it belongs (both layouts). */
  void reset();

 private:
  /** A part of the HUD that can be moved. */
  struct Item {
    std::string id;
    /** where it is in the HUD */
    std::string selector;
    /** the i18n key of its name (shown on it while it is empty) */
    std::string name;
    /** taken out of its box onto the HUD when moved (its box clips it) */
    bool detach = false;
  };
  static const std::vector<Item>& ITEMS();

  /** Where a part is: shifted by (x, y) – or, taken out, its upper left corner at (x, y) – as shares of the HUD's width and height. */
  struct Pos {
    double x = 0;
    double y = 0;
    bool free = false;
  };

  /** A bound part: its element and where it came from (to put it back). */
  struct Bound {
    dom::El el;
    dom::El parent;
    dom::El next;
  };

  const dom::El hud;
  const std::function<std::string(const std::string& key)> t;
  /** `Record<string, Record<string, Pos>>`: layout → part → where it is (kept as the JSON it is stored as) */
  Json saved;
  /** (in the order the parts were bound) */
  std::vector<std::pair<std::string, Bound>> bound;
  std::string layout;
  double lastCheck = -1;
  dom::El bar;
  dom::El shield;
  struct Drag {
    int id = 0;
    const Item* item = nullptr;
    dom::El el;
    double x = 0;
    double y = 0;
    double px = 0;
    double py = 0;
    double k = 1;
    bool moved = false;
  };
  std::optional<Drag> drag;
  /** a finger (or the mouse) held on a part: arranging begins when it stays */
  struct Press {
    int id = 0;
    double x = 0;
    double y = 0;
    int timer = 0;
  };
  std::optional<Press> press;
  /** the notice about arranging (null: none to give) */
  const std::function<void(const std::string& text)> hint;
  bool hinted;
  double hintFrom = -1;
  /** the listeners on the window and the HUD (taken away again with this object) */
  std::vector<int> onWindow;
  std::vector<int> onHud;

  std::string layoutNow() const;
  Bound* boundOf(const std::string& id);
  std::optional<Pos> posOf(const std::string& id) const;
  dom::El find(const Item& item);
  void bind(const Item& item);
  void applyAll();
  void apply(const Item& item, Bound& b);
  void putBack(Bound& b);
  void tickHint(double now);
  void markHinted();
  void save();
  void down(dom::Event& e);
  const Item* readOutAt(double x, double y);
  void pressDown(dom::Event& e);
  void endPress();
  void move(dom::Event& e);
  void up(dom::Event& e);
  void swallow(dom::Event& e);
  void store(const Item& item, double x, double y, bool free);
};

}  // namespace fs
