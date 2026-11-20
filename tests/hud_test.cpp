// SPDX-License-Identifier: AGPL-3.0-only
/** HUD: menus, settings, toasts, the dash strip and the mini map. */
#include "../src/ui/hud.h"
#include <cassert>
#include <cstdio>

using namespace fs;

namespace {
/** Records every host call so the test can assert on them. */
class TestHost : public HudHost {
 public:
  HudNode* root = nullptr;
  std::map<std::string, std::string> text;
  std::map<std::string, std::string> cls;
  std::map<std::string, bool> visible;
  int painted = 0;
  int toasts = 0;

  void setRoot(HudNode* r) override { root = r; }
  void setText(const std::string& id, const std::string& t) override { text[id] = t; }
  void setClass(const std::string& id, const std::string& c) override { cls[id] = c; }
  void setVisible(const std::string& id, bool v) override { visible[id] = v; }
  void paint(const std::string&, const Canvas2D&) override { painted++; }
  void toastAdded() override { toasts++; }

  /** Depth-first search for a widget with the given id. */
  HudNode* find(const std::string& id) { return findIn(root, id); }

  static HudNode* findIn(HudNode* n, const std::string& id) {
    if (!n) return nullptr;
    if (n->id == id) return n;
    for (auto& c : n->children) {
      HudNode* r = findIn(c.get(), id);
      if (r) return r;
    }
    return nullptr;
  }

  /** Count the widgets below `n` (or the whole tree). */
  static int count(HudNode* n) {
    if (!n) return 0;
    int k = 1;
    for (auto& c : n->children) k += count(c.get());
    return k;
  }
};

WorldJson makeNet() {
  WorldJson j;
  j.version = 1;
  j.meta.tileSize = 500;
  auto node_ = [](double x, double y, double z, int t) {
    NodeJson n;
    n.x = x; n.y = y; n.z = z; n.t = t;
    return n;
  };
  j.nodes.push_back(node_(0, 0, 0, CTRL_RBL));
  j.nodes.push_back(node_(100, 0, 0, CTRL_NONE));
  auto edge = [](int a, int b, std::vector<double> p) {
    EdgeJson e;
    e.a = a; e.b = b; e.c = RESIDENTIAL; e.p = std::move(p);
    e.w = 7.0; e.lf = 1; e.lb = 1; e.lw = 3.5; e.mf = 50; e.mb = 50;
    e.f = EF_URBAN;
    return e;
  };
  j.edges.push_back(edge(0, 1, {-100, 0, 0, 100, 0, 0}));
  auto arm = [](int e, int r) {
    ArmJson a;
    a.e = e; a.r = r;
    return a;
  };
  j.nodes[0].arms = {arm(0, ROLE_PRIORITY), arm(0, ROLE_PRIORITY)};
  j.nodes[1].arms = {arm(0, ROLE_NONE)};
  SpawnJson sp;
  sp.name = "Baunatal";
  sp.x = 0; sp.z = 0; sp.yaw = 0; sp.kind = "street";
  j.spawns.push_back(sp);
  return j;
}
}  // namespace

int main() {
  WorldJson net = makeNet();
  World w(net);
  Settings st;
  st.lang = "de";   // the original default without a browser
  TestHost host;
  Hud hud(host, st, w);

  // the HUD tree exists with the widgets the original builds
  assert(host.root != nullptr);
  assert(TestHost::count(host.root) > 10);
  assert(host.find("dash") != nullptr);
  assert(host.find("navArrow") != nullptr);
  assert(host.find("dashV") != nullptr);
  assert(host.find("map") != nullptr);

  // loading overlay
  hud.showLoading("Loading map ...");
  assert(hud.menuOpen());
  assert(host.root != nullptr && host.root->cls.find("overlay") != std::string::npos);

  // the menu carries the subtitle, the start cards and the lesson cards
  std::vector<SpawnJson> spawns = net.spawns;
  hud.showMenu(spawns, 0);
  assert(hud.menuOpen());
  int buttons = 0;
  std::string all;
  std::function<void(HudNode*)> walk = [&](HudNode* n) {
    if (!n) return;
    all += n->text + " " + n->html + " ";
    if (n->tag == "button") buttons++;
    for (auto& c : n->children) walk(c.get());
  };
  walk(host.root);
  // free drive + 8 lessons
  assert(buttons >= 9);
  assert(all.find("subtitle") == std::string::npos);   // the text was translated
  assert(all.find("Fahrsim") != std::string::npos);

  // starting a lesson fires onStart with the right mode
  StartRequest got;
  hud.onStart = [&](const StartRequest& r) { got = r; };
  // find the card for the "rbl" lesson and click it
  std::function<bool(HudNode*)> clickMode = [&](HudNode* n) -> bool {
    if (!n) return false;
    if (n->tag == "button" && n->html.find("Rechts vor links") != std::string::npos) {
      if (n->click) n->click();
      return true;
    }
    for (auto& c : n->children)
      if (clickMode(c.get())) return true;
    return false;
  };
  assert(clickMode(host.root));
  assert(got.mode == "rbl");

  // settings: changing a value saves and notifies
  int changed = 0;
  hud.onSettingsChanged = [&]() { changed++; };
  hud.showSettings([]() {});
  hud.hideOverlay();
  assert(!hud.menuOpen());

  // the HUD strip updates at ~11 Hz and paints the map at ~8 Hz
  HudState s;
  s.speedKmh = 47;
  s.limit = 50;
  s.showDash = true;
  s.gear = "3";
  s.rpm = 2100;
  s.navText = "turn left";
  s.x = 0; s.z = 0;
  hud.update(s, 0.0);
  assert(host.painted >= 1);
  assert(host.text.count("dashV") && host.text["dashV"] == "47");
  assert(host.text.count("dashG") && host.text["dashG"] == "3");
  assert(host.visible.count("nav") && host.visible["nav"]);
  assert(host.visible.count("dash") && host.visible["dash"]);

  // a second update 10 ms on does nothing (throttled)
  int painted = host.painted;
  hud.update(s, 0.01);
  assert(host.painted == painted);

  // and 130 ms later the map is repainted
  hud.update(s, 0.15);
  assert(host.painted > painted);

  // over the limit the roundel is marked
  s.speedKmh = 62;
  hud.update(s, 0.30);
  assert(host.cls.count("roundel") && host.cls["roundel"].find("over") != std::string::npos);

  // no limit shown when the rules help is off
  st.hudRules = false;
  hud.update(s, 0.45);
  assert(host.visible.count("roundel") && !host.visible["roundel"]);
  st.hudRules = true;

  // a fault or hint goes into the street-name block (0.5.0): the nav box is
  // always visible and shows the notice instead of the street
  Violation v;
  v.id = "speed";
  v.severity = Severity::Minor;
  v.data = {{"limit", "50"}, {"speed", "62"}};
  hud.toast(v);
  HudState sn;
  sn.navText = "Bahnhofstraße";
  hud.update(sn, 10.0);
  assert(host.cls.count("nav") && host.cls["nav"].find("alert") != std::string::npos);
  assert(host.cls["nav"].find("yellow") != std::string::npos);   // a minor fault is yellow
  assert(host.text.count("navText") && host.text["navText"].find("80") != std::string::npos ||
         host.text["navText"].find("62") != std::string::npos);
  // a message is a hint: it does not push a fault aside
  hud.message("Hello", "sub");
  hud.update(sn, 10.1);
  assert(host.cls["nav"].find("alert") != std::string::npos);
  // after the notices ran out the street name shows again
  hud.clearNotices();
  hud.update(sn, 10.2);
  assert(host.cls.count("nav") && host.cls["nav"] == "nav");
  assert(host.text["navText"] == "Bahnhofstraße");
  assert(host.toasts == 0);   // no toasts: the notices live in the nav box

  // the pause menu offers resume, reality, settings, controls and menu
  hud.showPause();
  int pauseButtons = 0;
  std::function<void(HudNode*)> countBtns = [&](HudNode* n) {
    if (!n) return;
    if (n->tag == "button") pauseButtons++;
    for (auto& c : n->children) countBtns(c.get());
  };
  countBtns(host.root);
  assert(pauseButtons >= 5);
  // free drive: the pause menu offers to jump to an address
  all.clear();
  walk(host.root);
  assert(all.find("Weiterfahren") != std::string::npos);
  hud.hideOverlay();
  assert(!hud.menuOpen());

  // the result screen shows the score
  ResultSummary r;
  r.passed = true;
  r.points = 3;
  r.title = "Pruefungsfahrt";
  hud.showResult(r);
  all.clear();
  walk(host.root);
  assert(all.find("3") != std::string::npos);

  // touch controls are added to the HUD tree (0.5.0: always built, shown by
  // setInputMode)
  Input in(*(Platform*)nullptr, st);   // the platform is not used by this call
  hud.attachInput(in);
  assert(host.find("touch") != nullptr);
  hud.setInputMode("touch");
  assert(host.cls.count("hud") && host.cls["hud"].find("touchmode") != std::string::npos);
  assert(host.visible.count("touch") && host.visible["touch"]);
  hud.setInputMode("keys");
  assert(host.cls["hud"].find("touchmode") == std::string::npos);
  // the buttons stand in for keys (they send untrusted ones: mode = touch)
  hud.setInputMode("touch");
  std::function<bool(HudNode*)> clickId = [&](HudNode* n) -> bool {
    if (!n) return false;
    if (n->id == "tbGearUp" && n->click) {
      n->click();
      return true;
    }
    for (auto& c : n->children)
      if (clickId(c.get())) return true;
    return false;
  };
  int gearBefore = in.c.gear;
  assert(clickId(host.root));
  assert(in.c.gear == gearBefore + 1);   // ▲ shifts up
  assert(in.mode == "touch");

  std::printf("hud: all checks passed (%d widgets, %d painted)\n",
              TestHost::count(host.root), host.painted);
  return 0;
}
