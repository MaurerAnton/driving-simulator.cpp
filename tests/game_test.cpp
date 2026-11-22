// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Game: boot, menu, the drive loop, lessons and the cameras, on a tiny
 * hand-built world with no tiles (the terrain source reports nothing).
 */
#include "../src/game/game.h"
#include <cmath>
#include <cassert>
#include <cstdio>

using namespace fs;

namespace {
class TestHost : public HudHost {
 public:
  HudNode* root = nullptr;
  std::map<std::string, std::string> text;
  std::map<std::string, bool> visible;
  void setRoot(HudNode* r) override { root = r; }
  void setText(const std::string& id, const std::string& t) override { text[id] = t; }
  void setClass(const std::string&, const std::string&) override {}
  void setVisible(const std::string& id, bool v) override { visible[id] = v; }
  void paint(const std::string&, const Canvas2D&) override {}
};

class TestPlatform : public Platform {
 public:
  std::vector<std::string> held;
  bool key(const std::string& c) const override {
    for (const auto& k : held) if (k == c) return true;
    return false;
  }
  PadState readPad() override { return PadState(); }
  double mouseLookYawDelta() override { return 0; }
  double mouseLookPitchDelta() override { return 0; }
  bool dragging() const override { return false; }
  bool touchActive() const override { return false; }
  double touchSteer() const override { return 0; }
  double touchThrottle() const override { return 0; }
  double touchBrake() const override { return 0; }
};

class TestAudio : public AudioBackend {
 public:
  bool started = false;
  bool start(double) override { started = true; return true; }
  bool running() const override { return started; }
  void suspend() override {}
  void resume() override { started = true; }
  double sampleRate() const override { return 48000; }
  double currentTime() const override { return 0; }
  void setTarget(const std::string&, double, double) override {}
  void click(ClickKind) override {}
  void setMasterGain(double, double) override {}
};

class TestSigns : public SignSource {
 public:
  void load(const std::string&) override {}
  std::string svg(const std::string&) override { return ""; }
  std::pair<int, int> size(const std::string&) override { return {0, 0}; }
  void rasterise(const std::string&, Canvas2D&, double, double, double, double) override {}
};

/** A world with no tiles: the terrain source reports NaN everywhere. */
class NoTiles : public TileSource {
 public:
  bool read(const std::string&, std::vector<uint8_t>&, bool*) override { return false; }
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
  j.nodes.push_back(node_(200, 0, 0, CTRL_NONE));
  j.nodes.push_back(node_(0, 0, 200, CTRL_NONE));
  auto edge = [](int a, int b, std::vector<double> p) {
    EdgeJson e;
    e.a = a; e.b = b; e.c = RESIDENTIAL; e.p = std::move(p);
    e.w = 7.0; e.lf = 1; e.lb = 1; e.lw = 3.5; e.mf = 50; e.mb = 50;
    e.f = EF_URBAN;
    return e;
  };
  j.edges.push_back(edge(0, 1, {0, 0, 0, 200, 0, 0}));
  j.edges.push_back(edge(0, 2, {0, 0, 0, 0, 0, 200}));
  auto arm = [](int e, int r) {
    ArmJson a;
    a.e = e; a.r = r;
    return a;
  };
  j.nodes[0].arms = {arm(0, ROLE_PRIORITY), arm(1, ROLE_PRIORITY)};
  j.nodes[1].arms = {arm(0, ROLE_NONE)};
  j.nodes[2].arms = {arm(1, ROLE_NONE)};
  SpawnJson sp;
  sp.name = "Stadtmitte";
  sp.x = 0; sp.z = 0; sp.yaw = 0; sp.kind = "street";
  j.spawns.push_back(sp);
  return j;
}
}  // namespace

int main() {
  // the world JSON must outlive the game (the World references it)
  WorldJson net = makeNet();

  TestHost host;
  TestPlatform platform;
  TestAudio audio;
  TestSigns signs;
  NoTiles tiles;

  Game g(host, platform, audio, signs, tiles, 800, 600);
  g.worldJson_ = net;
  g.boot(QueryParams());

  // boot built the world and went to the menu
  assert(g.state == GameState::Menu);
  assert(g.app.world != nullptr);
  assert(g.lanes && g.signals && g.traffic && g.rules && g.nav);
  assert(g.hud && g.input && g.ground);
  assert(g.signs && g.signalHeads);
  assert(host.root != nullptr);

  // the menu is showing (the overlay replaced the HUD tree)
  assert(g.hud->menuOpen());

  // the renderer reports no draw calls yet (nothing has been rendered)
  assert(g.app.renderer.info.calls == 0);

  // begin a free drive: the vehicle appears and the state moves to placing
  g.beginCall("free", 0, true);
  assert(g.state == GameState::Placing);
  assert(g.vehicle != nullptr);
  assert(g.input->enabled == false);

  // with no tiles the car never places: keep stepping frames
  for (int i = 0; i < 30; i++) g.frame(i * (1.0 / 60.0));
  assert(g.state == GameState::Placing);

  // the tile source reports the terrain as missing, so the ground keeps the
  // last known height (0 at the origin)
  assert(std::isnan(g.app.tiles->heightAt(0, 0)));

  // the settings are applied: quality drives the draw radius
  g.settings.quality = Quality::Low;
  g.applySettingsCall();
  assert(std::fabs(g.app.tiles->viewRadius - 700) < 1e-9);
  g.settings.quality = Quality::High;
  g.applySettingsCall();
  assert(std::fabs(g.app.tiles->viewRadius - 2000) < 1e-9);

  // debugInfo reports the state without crashing
  std::string info = g.debugInfo();
  assert(!info.empty());

  // the pause action toggles the pause menu
  g.state = GameState::Drive;
  g.onActionCall(Action::Pause);
  assert(g.state == GameState::Pause);
  g.onActionCall(Action::Pause);
  assert(g.state == GameState::Drive);

  // the view action switches the camera (the drive starts in the view the
  // settings chose – chase by default, so this flips to the cockpit)
  assert(g.cameraMode == CameraMode::Chase);
  g.onActionCall(Action::View);
  assert(g.cameraMode == CameraMode::Cockpit);
  g.onActionCall(Action::View);
  assert(g.cameraMode == CameraMode::Chase);
  g.onActionCall(Action::View);
  assert(g.cameraMode == CameraMode::Cockpit);

  // finishing without a lesson shows the free-drive result
  g.finishCall();
  assert(g.state == GameState::Result);

  // the menu's demonstration card plans its drive (0.7.0): starting it puts
  // the game in Placing, waiting for the ground to stand the car on
  g.beginCall("demo", 0, true);
  assert(g.state == GameState::Placing);
  for (int i = 0; i < 30; i++) g.frame(i * (1.0 / 60.0));
  assert(g.state == GameState::Placing);   // (no tiles here: it never stands down)

  std::printf("game: all checks passed (state=%d)\n", static_cast<int>(g.state));
  return 0;
}
