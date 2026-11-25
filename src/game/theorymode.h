// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/theorymode.ts @ v1.7.7
/**
 * The theory lessons as a mode of the game (a way to start from the main
 * menu): the list of situations, then one lesson after another on the
 * practice yard – its script played (game/theory.ts), drawn
 * (render/theory.ts) and explained in the bar at the foot of the screen
 * (ui/theory.ts).  The camera circles the scene and is turned and moved
 * closer by dragging and pinching (or the mouse and its wheel); a tap near
 * a vehicle answers a question about it.  With the spoken instructions on,
 * the texts are read out.
 *
 * Nothing of a drive runs meanwhile – no traffic, no rules, no HUD: game.ts
 * only hands over each frame and the camera.
 */
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <vector>
#include <string>

#include "engine/engine.h"
#include "game/theory.h"
#include "platform/dom.h"
#include "render/theoryprops.h"

namespace fs {

class App;
class Input;
class SignRenderer;
class TheoryPanel;
class TheoryView;
class YardFrame;
struct Settings;

/** What the mode needs of the game. */
struct TheoryHost {
  App* app = nullptr;
  dom::El ui;
  Settings* settings = nullptr;
  Input* input = nullptr;
  SignRenderer* signs = nullptr;
  const YardFrame* yard = nullptr;
  /** keep the ground, the trees and the far hills loaded and drawn round this point (each frame) */
  std::function<void(double x, double z)> around;
  /** back to the main menu */
  std::function<void()> close;
};

class TheoryMode {
 public:
  /** what the camera looks at (for the sky and the shadows) */
  Vector3 focus;

  explicit TheoryMode(TheoryHost host);
  ~TheoryMode();
  TheoryMode(const TheoryMode&) = delete;
  TheoryMode& operator=(const TheoryMode&) = delete;

  /** the lesson laid out and drawn for a few frames (for the automated pictures) */
  bool ready() const;

  /** Into the mode: the list – or straight into one lesson (by its id). */
  void open(const std::optional<std::string>& id = std::nullopt);

  /** Out of the mode (the main menu takes over). */
  void close();

  /** Escape: from a lesson to the list, from the list to the main menu. */
  void back();

  void frame(double dt, double now);

 private:
  TheoryHost host;
  std::unique_ptr<TheoryView> view;
  std::unique_ptr<TheoryPanel> panel;
  StageFrame frameOf;
  /** 'off' | 'list' | 'loading' | 'lesson' */
  std::string phase = "off";
  int index = 0;
  std::shared_ptr<theory::Scene> scene;
  std::unique_ptr<theory::TheoryRun> run;
  /** what the bar shows (it is drawn anew when this changes) */
  std::string shown;
  /** which lessons were once answered all right (in the order they were, as a Set keeps it) */
  std::vector<std::string> done;
  struct Cam {
    double yaw = 0, pitch = -0.6, dist = 34, panX = 0, panZ = 0;
    bool ready = false;
  } cam;
  Vector3 target;
  struct Down {
    double x = 0, y = 0, t = 0;
    int id = 0;
  };
  std::optional<Down> down;
  /** frames drawn since the lesson's place was laid out (for the automated pictures) */
  int builtFrames = 0;
  /** the sky as it was before a lesson with a sky of its own (dusk, night, fog) – given back after it */
  struct SkyKept {
    double el = 0, az = 0, near = 0, far = 0;
  };
  std::optional<SkyKept> skyKept;
  /** ?theoryAt=<s>: the first lesson opened starts at that moment of its script (for pictures of it) */
  double jump = 0;

  const std::string& lang() const;
  const theory::LessonDef& lesson() const;
  void showList();
  void start(int i);
  /** The scene stopped: a question, or the end. */
  void stopped();
  std::string score() const;
  void answer(const std::string& id);
  /** A tap at a point of the screen: the vehicle there, if the open question is about it. */
  void tap(double cx, double cy);
  void drawBar();
  /** The lesson's own view: from where its script says, the drag and the zoom forgotten. */
  void resetCamera();
  /** Circling what the lesson looks at: turned by dragging, closer and further by pinching or the wheel, moved with two fingers or the right mouse button. */
  void camera(double dt);
  /** Behind the list and while a place is laid out: the yard from above, slowly circled. */
  void overview(double now);
  /**
   * A lesson's own sky: the sun that low (dusk, night) and fog that thick –
   * the game's sky kept and given back when `sky` is nullopt.
   */
  void ownSky(const std::optional<theory::LessonSky>& sky);
  /** Read a text out – when the spoken instructions are on (as the directions of a drive are). */
  void speak(const std::string& text);
  void silence();
};

}  // namespace fs
