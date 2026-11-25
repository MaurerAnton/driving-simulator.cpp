// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/theorymode.ts @ v1.7.7
#include "game/theorymode.h"

#include <algorithm>
#include <cmath>

#include "audio/speech.h"
#include "core/js.h"
#include "game/app.h"
#include "game/ce.h"
#include "game/game.h"
#include "game/settings.h"
#include "i18n/index.h"
#include "input/input.h"
#include "platform/json.h"
#include "platform/plat.h"
#include "render/signs.h"
#include "render/theory.h"
#include "ui/theory.h"

namespace fs {

namespace {
constexpr double kPi = 3.14159265358979323846;

/** which lessons were once answered all right (kept in the browser) */
const char* const KEY = "fahrsim-baunatal.theory.v1";

std::vector<std::string> loadDone() {
  std::vector<std::string> out;
  const std::optional<std::string> raw = plat::localStorage().getItem(KEY);
  Json v;
  if (!raw || !Json::parse(*raw, v)) return out;
  const Json& list = v.at("done");
  if (!list.isArray()) return out;
  for (const Json& x : list.array)
    if (x.isString() && std::find(out.begin(), out.end(), x.str) == out.end()) out.push_back(x.str);
  return out;
}

void saveDone(const std::vector<std::string>& done) {
  Json list = Json::makeArray();
  for (const std::string& d : done) list.push(Json(d));
  Json o = Json::makeObject();
  o.set("done", list);
  // (private mode: not kept)
  plat::localStorage().setItem(KEY, o.stringify());
}
}  // namespace

TheoryMode::TheoryMode(TheoryHost h) : host(std::move(h)), done(loadDone()) {
  {
    const std::optional<std::string> at = URLSearchParams(plat::locationSearch()).get("theoryAt");
    jump = at ? jsNumber(*at) : std::nan("");
  }
  view = std::make_unique<TheoryView>(*host.signs);
  host.app->scene->add(view->group);
  panel = std::make_unique<TheoryPanel>(host.ui, [this](const std::string& k, const TrParams& vars) { return tr(k, lang(), vars); });
  // the stage's north (where the learner drives) along the yard's long side
  const YardFrame* yard = host.yard;
  frameOf = StageFrame{
      .toWorld = [yard](double x, double z) { return yard->toWorld(-z, x); },
      .yawToWorld = [yard](double yaw) { return yard->yawToWorld(yaw - kPi / 2); },
  };
  // a tap on the picture (not a drag): an answer, when it is near a vehicle a question is about
  const dom::El& canvas = host.app->canvas;
  canvas.on("pointerdown", [this](dom::Event& e) {
    if (phase == "lesson") down = Down{.x = e.clientX, .y = e.clientY, .t = plat::nowMs(), .id = e.pointerId};
  });
  canvas.on("pointerup", [this](dom::Event& e) {
    const std::optional<Down> d = down;
    down = std::nullopt;
    if (!d || d->id != e.pointerId || phase != "lesson") return;
    if (std::hypot(e.clientX - d->x, e.clientY - d->y) > 12 || plat::nowMs() - d->t > 700) return;
    tap(e.clientX, e.clientY);
  });
}

TheoryMode::~TheoryMode() = default;

const std::string& TheoryMode::lang() const { return host.settings->lang; }

bool TheoryMode::ready() const { return phase == "lesson" && builtFrames > 3; }

void TheoryMode::open(const std::optional<std::string>& id) {
  int i = -1;
  if (id) {
    const std::vector<theory::LessonDef>& all = theory::THEORY();
    for (size_t k = 0; k < all.size(); k++)
      if (all[k].id == *id) i = static_cast<int>(k);
  }
  cam.ready = false;
  if (i >= 0) start(i);
  else showList();
}

void TheoryMode::close() {
  ownSky(std::nullopt);
  phase = "off";
  view->hide();
  panel->hide();
  scene = nullptr;
  run = nullptr;
  shown = "";
  silence();
}

void TheoryMode::back() {
  if (phase == "lesson" || phase == "loading") showList();
  else host.close();
}

void TheoryMode::showList() {
  ownSky(std::nullopt);
  phase = "list";
  view->hide();
  scene = nullptr;
  run = nullptr;
  shown = "";
  silence();
  std::vector<TheoryItem> items;
  for (const theory::LessonDef& l : theory::THEORY())
    items.push_back(TheoryItem{.id = l.id, .title = tr(theory::theoryKeys::title(l.id), lang()), .intro = tr(theory::theoryKeys::intro(l.id), lang()), .done = std::find(done.begin(), done.end(), l.id) != done.end()});
  panel->showList(
      items,
      [this](const std::string& id) {
        const std::vector<theory::LessonDef>& all = theory::THEORY();
        int i = -1;
        for (size_t k = 0; k < all.size(); k++)
          if (all[k].id == id) i = static_cast<int>(k);
        start(i);
      },
      [this] { host.close(); });
}

void TheoryMode::start(int i) {
  ownSky(std::nullopt);
  index = std::max(0, std::min(static_cast<int>(theory::THEORY().size()) - 1, i));
  phase = "loading";
  scene = theory::prepare(theory::THEORY()[static_cast<size_t>(index)]);
  run = nullptr;
  builtFrames = 0;
  shown = "";
  silence();
  drawBar();
}

const theory::LessonDef& TheoryMode::lesson() const { return theory::THEORY()[static_cast<size_t>(index)]; }

// ------------------------------------------------------------------ each frame

void TheoryMode::frame(double dt, double now) {
  App& app = *host.app;
  if (phase == "loading" && scene) {
    // (the yard and what is round it are loaded first)
    const auto [cx, cz] = frameOf.toWorld(0, 10);
    host.around(cx, cz);
    overview(now);
    if (app.tiles->settled() && view->build(scene, frameOf, [&app](double x, double z) { return app.tiles->heightAt(x, z); })) {
      phase = "lesson";
      run = std::make_unique<theory::TheoryRun>(scene);
      if (std::isfinite(jump) && jump > 0) {
        run->skipTo(jump);
        jump = std::nan("");
      }
      resetCamera();
      view->update(run->t, now);
      speak(tr(theory::theoryKeys::title(lesson().id), lang()) + ". " + tr(theory::theoryKeys::intro(lesson().id), lang()));
      drawBar();
    }
    return;
  }
  if (phase == "list" || phase == "off") {
    const auto [cx, cz] = frameOf.toWorld(0, 10);
    host.around(cx, cz);
    overview(now);
    return;
  }
  if (!run || !scene) return;
  builtFrames++;
  // (each frame: the game places the sun of its own clock now and then)
  if (lesson().sky) ownSky(lesson().sky);
  if (run->advance(dt)) stopped();
  view->update(run->t, now);
  // the names over the vehicles the open question is about; "you" over the learner's car all along
  const theory::StepDef* step = run->current();
  std::vector<std::pair<std::string, std::string>> names;
  if (run->phase == "ask" && step) {
    for (const std::string& a : step->answers) {
      for (const theory::ActorDef& actor : lesson().actors)
        if (actor.id == a) {
          names.emplace_back(a, tr(theory::actorName(actor), lang()));
          break;
        }
    }
  }
  view->setLabels(names, tr("theory.who.me", lang()));
  view->placeLabels(now);
  view->showZone(run->phase == "ask" && step && !step->blind.empty() ? step->blind : "");
  const std::optional<theory::LessonGap>& gap = lesson().gap;
  if (gap && run->t >= gap->t0 && run->t <= gap->t1) view->showGap(gap->a, gap->b, [this](double m) { return formatUnit(m, Unit::Meter, lang(), 1); });
  else view->showGap("", "", [](double) { return std::string(); });
  camera(dt);
  host.around(focus.x, focus.z);
  drawBar();
}

void TheoryMode::stopped() {
  const theory::LessonDef& l = lesson();
  if (run->phase == "ask" && run->current()) {
    const int n = run->step + 1;
    speak(tr(theory::theoryKeys::text(l.id, n), lang()) + " " + tr(theory::theoryKeys::ask(l.id, n, *run->current()), lang()));
  } else if (run->phase == "end") {
    const bool all = run->right == static_cast<int>(l.steps.size());
    if (all && std::find(done.begin(), done.end(), l.id) == done.end()) {
      done.push_back(l.id);
      saveDone(done);
    }
    speak(score());
  }
}

std::string TheoryMode::score() const { return tr("theory.score", lang(), {{"right", run->right}, {"count", static_cast<int>(lesson().steps.size())}}); }

void TheoryMode::answer(const std::string& id) {
  if (!run || run->phase != "ask") return;
  const int n = run->step + 1;
  const std::optional<bool> ok = run->answer(id);
  if (!ok) return;
  speak(tr(*ok ? "theory.right" : "theory.wrong", lang()) + " " + tr(theory::theoryKeys::why(lesson().id, n), lang()));
  drawBar();
}

void TheoryMode::tap(double cx, double cy) {
  if (!run) return;
  const theory::StepDef* step = run->current();
  if (run->phase != "ask" || !step) return;
  std::vector<std::string> ids;
  for (const std::string& a : step->answers)
    if (std::any_of(lesson().actors.begin(), lesson().actors.end(), [&a](const theory::ActorDef& x) { return x.id == a; })) ids.push_back(a);
  if (ids.empty()) return;
  const dom::Rect rect = host.app->canvas.getBoundingClientRect();
  const std::optional<std::string> id = view->pick(ids, *host.app->camera, cx - rect.left, cy - rect.top, rect.width, rect.height);
  if (id) answer(*id);
}

// ------------------------------------------------------------------- the bar

void TheoryMode::drawBar() {
  const theory::LessonDef& l = lesson();
  const std::string ph = phase == "loading" || !run ? "loading" : run->phase == "end" ? "end" : run->phase;
  const std::string key = std::to_string(index) + "|" + ph + "|" + std::to_string(run ? run->step : 0) + "|" + (run && run->picked ? *run->picked : "") + "|" + lang();
  if (key == shown) return;
  shown = key;
  const theory::StepDef* step = run ? run->current() : nullptr;
  const int n = (run ? run->step : 0) + 1;
  const std::string& lg = lang();
  TheoryBar bar;
  bar.n = index + 1;
  bar.count = static_cast<int>(theory::THEORY().size());
  bar.title = tr(theory::theoryKeys::title(l.id), lg);
  bar.phase = ph;
  // (playing: the introduction – after a question, the reason just given, to read while it plays on)
  bar.text = ph == "play" ? tr(run && run->step > 0 ? theory::theoryKeys::why(l.id, run->step) : theory::theoryKeys::intro(l.id), lg) : step ? tr(theory::theoryKeys::text(l.id, n), lg) : "";
  bar.ask = step ? tr(theory::theoryKeys::ask(l.id, n, *step), lg) : "";
  if (step)
    for (const std::string& a : step->answers) bar.answers.push_back(TheoryAnswer{.id = a, .label = tr(theory::answerKey(l, n, a), lg)});
  bar.picked = run ? run->picked : std::nullopt;
  bar.right = step ? step->right : "";
  bar.why = step ? tr(theory::theoryKeys::why(l.id, n), lg) : "";
  bar.ref = step ? step->ref : "";
  bar.score = run ? score() : "";
  bar.hasNext = index < static_cast<int>(theory::THEORY().size()) - 1;
  panel->show(bar, TheoryBarActions{
                       .answer = [this](const std::string& id) { answer(id); },
                       .goOn =
                           [this] {
                             silence();
                             if (run) run->goOn();
                             drawBar();
                           },
                       .again = [this] { start(index); },
                       .next = [this] { start(index + 1); },
                       .list = [this] { showList(); },
                   });
}

// ----------------------------------------------------------------- the camera

void TheoryMode::resetCamera() {
  const theory::LessonView& v = lesson().view;
  cam.yaw = frameOf.yawToWorld(v.yaw);
  cam.pitch = v.pitch;
  cam.dist = v.dist;
  cam.panX = 0;
  cam.panZ = 0;
  cam.ready = false;
  (void)host.input->takeCam();
}

void TheoryMode::camera(double dt) {
  App& app = *host.app;
  Cam& c = cam;
  const Input::Cam& t = host.input->takeCam();
  if (t.reset) resetCamera();
  c.yaw += t.yaw;
  c.pitch = std::max(-1.45, std::min(-0.12, c.pitch + t.pitch));
  c.dist = std::max(8.0, std::min(130.0, c.dist * std::exp(-t.dolly)));
  // (moving: in the directions the camera looks, by parts of its distance)
  const double sy = std::sin(c.yaw);
  const double cy = std::cos(c.yaw);
  const double right = t.right * c.dist;
  const double fwd = t.fwd * c.dist;
  c.panX += cy * right - sy * fwd;
  c.panZ += -sy * right - cy * fwd;
  const double pan = std::hypot(c.panX, c.panZ);
  if (pan > 80) {
    c.panX *= 80 / pan;
    c.panZ *= 80 / pan;
  }
  // what it looks at: a point of the place, or a vehicle it follows (a little ahead of it)
  const theory::LessonView& v = lesson().view;
  if (const std::string* id = std::get_if<std::string>(&v.at)) {
    if (view->where(*id, target)) {
      const double h = view->heading(*id).value_or(0);
      target.x -= std::sin(h) * v.ahead.value_or(0);
      target.z -= std::cos(h) * v.ahead.value_or(0);
    }
  } else {
    const std::array<double, 2>& at = std::get<std::array<double, 2>>(v.at);
    const auto [x, z] = frameOf.toWorld(at[0], at[1]);
    const double y = app.tiles->heightAt(x, z);
    target.set(x, std::isnan(y) ? focus.y : y + 1, z);
  }
  target.x += c.panX;
  target.z += c.panZ;
  if (!c.ready) {
    focus.copy(target);
    c.ready = true;
  } else focus.lerp(target, 1 - std::exp(-dt * 5));
  Camera& camObj = *app.camera;
  const double cp = std::cos(c.pitch);
  camObj.position.set(focus.x + sy * cp * c.dist, focus.y - std::sin(c.pitch) * c.dist, focus.z + cy * cp * c.dist);
  const double g = app.tiles->heightAt(camObj.position.x, camObj.position.z);
  if (!std::isnan(g)) camObj.position.y = std::max(camObj.position.y, g + 1.5);
  camObj.fov = 55;
  camObj.near = 0.3;
  camObj.lookAt(focus);
  // (a little further down: what it looks at stands above the middle of the picture, clear of the bar at its foot)
  camObj.rotateX(-0.09);
  camObj.updateProjectionMatrix();
}

void TheoryMode::overview(double now) {
  App& app = *host.app;
  const auto [cx, cz] = frameOf.toWorld(0, 10);
  const double gy = app.tiles->heightAt(cx, cz);
  const double base = std::isnan(gy) ? focus.y : gy;
  focus.set(cx, base + 1, cz);
  const double a = now * 0.04;
  Camera& camObj = *app.camera;
  camObj.position.set(cx + std::cos(a) * 110, base + 70, cz + std::sin(a) * 110);
  camObj.fov = 55;
  camObj.near = 0.5;
  camObj.lookAt(focus);
  camObj.updateProjectionMatrix();
}

// ---------------------------------------------------------------------- sky

void TheoryMode::ownSky(const std::optional<theory::LessonSky>& sky) {
  Sky& s = host.app->sky;
  if (!sky) {
    if (!skyKept) return;
    s.setSun(skyKept->el, skyKept->az);
    s.fog->near = skyKept->near;
    s.fog->far = skyKept->far;
    skyKept = std::nullopt;
    return;
  }
  if (!skyKept) {
    // (where the sun stands now: the light shines from it onto its target)
    Vector3 d = s.sun->position;
    d.sub(s.sun->target->position).normalize();
    skyKept = SkyKept{.el = (std::asin(d.y) * 180) / kPi, .az = (std::atan2(d.x, -d.z) * 180) / kPi, .near = s.fog->near, .far = s.fog->far};
  }
  const SkyKept& k = *skyKept;
  s.setSun(sky->sun.value_or(k.el), sky->sun ? 250 : k.az);
  if (sky->fog) {
    // (fog: as far as one sees, the world is gone; its colour a pale grey)
    s.fog->near = 2;
    s.fog->far = *sky->fog * 1.25;
    s.fog->color = Color(0xb9bdc0u);
  } else {
    s.fog->near = k.near;
    s.fog->far = k.far;
  }
}

// -------------------------------------------------------------------- voice

void TheoryMode::speak(const std::string& text) {
  const Settings& s = *host.settings;
  if (!s.voice || text.empty()) return;
  speech.say(text, lang(), s);
}

void TheoryMode::silence() {
  if (host.settings->voice) speech.stop();
}

}  // namespace fs
