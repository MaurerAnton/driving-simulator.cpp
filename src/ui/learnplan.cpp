// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/learnplan.ts @ v1.7.7
#include "ui/learnplan.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <vector>

#include "core/js.h"
#include "game/theory.h"
#include "game/theorystage.h"
#include "render/signart.h"

namespace fs {

namespace {
using theory::ActorRun;
using theory::Kerb;
using theory::LessonDef;
using theory::LightPlace;
using theory::LightState;
using theory::PropPlace;
using theory::SignPlace;
using theory::StepDef;
using theory::Surface;
using theory::SurfaceKind;
using TScene = theory::Scene;
using TPose = theory::Pose;

constexpr double PI_ = 3.14159265358979323846;

std::map<std::string, std::shared_ptr<TScene>>& scenes() {
  static auto* m = new std::map<std::string, std::shared_ptr<TScene>>();
  return *m;
}

TScene* sceneOf(const std::string& id) {
  auto it = scenes().find(id);
  if (it == scenes().end()) {
    const LessonDef* l = theory::theoryLesson(id);
    if (!l) return nullptr;
    it = scenes().emplace(id, theory::prepare(*l)).first;
  }
  return it->second.get();
}

std::string GROUND(const SurfaceKind& kind) {
  if (kind == "asphalt") return "#4f555c";
  if (kind == "walk") return "#a7a399";
  if (kind == "island") return "#8b9a74";
  if (kind == "apron") return "#6f7377";
  if (kind == "paint") return "#eeeeea";
  if (kind == "red") return "#a8483f";
  return "#8a8f94";  // steel
}

const std::map<std::string, std::string>& KIND_COLOUR() {
  static const auto* m = new std::map<std::string, std::string>{{"van", "#e8e8e8"}, {"bus", "#d8a417"}, {"truck", "#5d6f80"}, {"tram", "#c8303a"}, {"bike", "#2f8a4a"}, {"person", "#3a6fb0"}, {"child", "#e08a2a"}, {"deer", "#8a5a32"}, {"boar", "#4a3a30"}};
  return *m;
}

std::string LAMP(LightState s) {
  switch (s) {
    case 'R': return "#e53935";
    case 'Y': return "#ffb300";
    case 'G': return "#43a047";
    case 'A': return "#ffb300";
    default: return "#30363d";
  }
}

std::string hex(int c) {
  char b[16];
  std::snprintf(b, sizeof b, "#%06x", c);
  return b;
}

/** A sign's picture: loading (with the drawings waiting for it), or come (`img`: 0 when it failed). */
struct SignImage {
  bool complete = false;
  Image img;
  std::vector<std::function<void()>> onload;
};

std::map<std::string, std::shared_ptr<SignImage>>& images() {
  static auto* m = new std::map<std::string, std::shared_ptr<SignImage>>();
  return *m;
}

Task<void> loadSign(std::shared_ptr<SignImage> s, std::string url) {
  s->img = co_await loadImage(url);
  s->complete = true;
  std::vector<std::function<void()>> w;
  w.swap(s->onload);
  // (an image that failed fires no load: nothing is drawn again)
  if (s->img)
    for (const auto& f : w) f();
}
}  // namespace

std::optional<double> sceneTime(const std::string& id, int step, double off) {
  const LessonDef* l = theory::theoryLesson(id);
  if (!l || step < 0 || step >= static_cast<int>(l->steps.size())) return std::nullopt;
  const StepDef& st = l->steps[static_cast<size_t>(step)];
  return std::max(0.0, std::min(l->end, st.at + off));
}

bool drawPlan(const Canvas& canvas, const std::string& id, double t, const std::string& you, const std::string& signBase) {
  TScene* scene = sceneOf(id);
  const Ctx2D g = canvas ? canvas.getContext2D() : Ctx2D();
  if (!scene || !g) return false;
  const double W = canvas.width();
  const double H = canvas.height();
  TPose pose;
  // ---- what to show: the learner and whoever is near
  const ActorRun* me = nullptr;
  for (const ActorRun& a : scene->actors)
    if (a.def->kind == "me") {
      me = &a;
      break;
    }
  std::optional<TPose> mp;
  if (me) {
    TPose p;
    mp = theory::poseAt(*me, t, p);
  }
  const double cx0 = mp ? mp->x : (scene->stage.bounds[0] + scene->stage.bounds[2]) / 2;
  const double cz0 = mp ? mp->z : (scene->stage.bounds[1] + scene->stage.bounds[3]) / 2;
  double x0 = cx0 - 14;
  double x1 = cx0 + 14;
  double z0 = cz0 - 22;
  double z1 = cz0 + 10;
  for (const ActorRun& a : scene->actors) {
    const TPose& p = theory::poseAt(a, t, pose);
    if (!p.visible || std::hypot(p.x - cx0, p.z - cz0) > 75) continue;
    x0 = std::min(x0, p.x - 8);
    x1 = std::max(x1, p.x + 8);
    z0 = std::min(z0, p.z - 8);
    z1 = std::max(z1, p.z + 8);
  }
  const double scale = std::min(W / (x1 - x0), H / (z1 - z0));
  const double cx = (x0 + x1) / 2;
  const double cz = (z0 + z1) / 2;
  const auto sx = [&](double x) { return (x - cx) * scale + W / 2; };
  const auto sy = [&](double z) { return (z - cz) * scale + H / 2; };
  const auto ring = [&](const std::vector<double>& pts) {
    for (size_t i = 0; i + 1 < pts.size(); i += 2) {
      if (i == 0) g.moveTo(sx(pts[i]), sy(pts[i + 1]));
      else g.lineTo(sx(pts[i]), sy(pts[i + 1]));
    }
    g.closePath();
  };
  const auto line = [&](const std::vector<double>& pts) {
    g.beginPath();
    for (size_t i = 0; i + 1 < pts.size(); i += 2) {
      if (i == 0) g.moveTo(sx(pts[i]), sy(pts[i + 1]));
      else g.lineTo(sx(pts[i]), sy(pts[i + 1]));
    }
    g.stroke();
  };

  // ---- the ground
  g.fillStyle("#5b7346");
  g.fillRect(0, 0, W, H);
  for (const Surface& s : scene->stage.surfaces) {
    g.beginPath();
    ring(s.ring);
    for (const std::vector<double>& h : s.holes) ring(h);
    g.fillStyle(GROUND(s.kind));
    g.fillEvenOdd();
  }
  g.lineWidth(std::max(1.0, scale * 0.12));
  g.strokeStyle("rgba(30, 30, 30, 0.55)");
  for (const Kerb& k : scene->stage.kerbs) line(k.line);
  g.strokeStyle("#c9ccd0");
  g.lineWidth(std::max(1.0, scale * 0.2));
  for (const std::vector<double>& gd : scene->stage.guards) line(gd);
  for (const PropPlace& p : scene->stage.props) {
    if ((p.from && t < *p.from) || (p.to && t > *p.to)) continue;
    if (p.kind == "tree") {
      g.fillStyle("#2f5a2a");
      g.beginPath();
      g.arc(sx(p.x), sy(p.z), scale * 2.2 * p.size.value_or(1), 0, PI_ * 2);
      g.fill();
    } else if (p.kind == "tunnel" && p.x1 && p.z1) {
      g.fillStyle("rgba(20, 20, 24, 0.55)");
      g.fillRect(std::min(sx(p.x), sx(*p.x1)), std::min(sy(p.z), sy(*p.z1)), std::abs(sx(*p.x1) - sx(p.x)), std::abs(sy(*p.z1) - sy(p.z)));
    } else {
      g.fillStyle(p.kind == "triangle" || p.kind == "cone" || p.kind == "barrier" ? "#e2412f" : p.kind == "heap" ? "#7a5a3a" : "#e8e8e8");
      const double r = scale * (p.kind == "barrier" ? 1.2 : p.kind == "heap" ? 1.6 : 0.45);
      g.fillRect(sx(p.x) - r, sy(p.z) - r * 0.6, r * 2, r * 1.2);
    }
  }

  // ---- the vehicles and people
  for (const ActorRun& a : scene->actors) {
    const TPose& p = theory::poseAt(a, t, pose);
    if (!p.visible) continue;
    const bool isMe = a.def->kind == "me";
    const bool small = a.def->kind == "person" || a.def->kind == "child";
    const auto kc = KIND_COLOUR().find(a.def->kind);
    g.fillStyle(isMe ? "#ffffff" : a.def->colour ? hex(*a.def->colour) : kc != KIND_COLOUR().end() ? kc->second : "#c0c4c8");
    if (small) {
      g.beginPath();
      g.arc(sx(p.x), sy(p.z), std::max(3.0, scale * 0.45), 0, PI_ * 2);
      g.fill();
      g.strokeStyle("#111");
      g.lineWidth(1);
      g.stroke();
      continue;
    }
    const std::vector<double> c = theory::corners(a, p);
    g.beginPath();
    ring(c);
    g.fill();
    g.lineWidth(isMe ? 3 : 1);
    g.strokeStyle(isMe ? "#ffcc00" : "rgba(0, 0, 0, 0.7)");
    g.stroke();
    // its nose: a darker band at the front
    g.strokeStyle("rgba(0, 0, 0, 0.55)");
    g.lineWidth(std::max(1.5, scale * 0.25));
    g.beginPath();
    g.moveTo(sx(c[0]), sy(c[1]));
    g.lineTo(sx(c[2]), sy(c[3]));
    g.stroke();
    // the indicators: amber dots at the corners of the side shown (both: hazard lights)
    if (p.blink != 0) {
      g.fillStyle("#ffa000");
      const std::vector<int> dots = p.blink == 2 ? std::vector<int>{0, 1, 2, 3} : p.blink < 0 ? std::vector<int>{0, 3} : std::vector<int>{1, 2};
      for (const int i : dots) {
        g.beginPath();
        g.arc(sx(c[static_cast<size_t>(i * 2)]), sy(c[static_cast<size_t>(i * 2 + 1)]), std::max(2.5, scale * 0.35), 0, PI_ * 2);
        g.fill();
      }
    }
    if (isMe) {
      g.font("bold " + std::to_string(static_cast<int>(jsRound(std::max(12.0, std::min(18.0, scale * 1.4))))) + "px system-ui, sans-serif");
      g.textAlign("center");
      g.textBaseline("top");
      const double by = std::max(sy(c[5]), sy(c[7])) + 4;
      g.lineWidth(3);
      g.strokeStyle("rgba(0, 0, 0, 0.75)");
      g.strokeText(you, sx(p.x), std::min(H - 18, by));
      g.fillStyle("#ffcc00");
      g.fillText(you, sx(p.x), std::min(H - 18, by));
    }
  }

  // ---- the traffic lights and the signs
  for (const LightPlace& l : scene->stage.lights) {
    const double r = std::max(3.0, scale * 0.6);
    g.fillStyle("#111");
    g.fillRect(sx(l.x) - r - 1, sy(l.z) - r - 1, 2 * r + 2, 2 * r + 2);
    g.fillStyle(LAMP(theory::lightAt(*scene, l.id, t)));
    g.beginPath();
    g.arc(sx(l.x), sy(l.z), r, 0, PI_ * 2);
    g.fill();
  }
  const double size = std::max(14.0, std::min(30.0, scale * 2.6));
  for (const SignPlace& s : scene->stage.signs) {
    for (size_t i = 0; i < s.codes.size(); i++) {
      const std::string& code = s.codes[i];
      const double x = sx(s.x) + 4;
      const double y = sy(s.z) - size - 2 + static_cast<double>(i) * (size + 1);
      if (x < -size || x > W || y < -size || y > H) continue;
      if (artAspect(code) && !images().count(code)) {
        const double a = artAspect(code).value_or(1);
        drawSignArt(g, code, x, y, a >= 1 ? size : size * a, a >= 1 ? size / a : size);
        continue;
      }
      std::shared_ptr<SignImage>& img = images()[code];
      if (!img) {
        img = std::make_shared<SignImage>();
        loadSign(img, signBase + "svg/" + encodeURIComponent(code) + ".svg").detach();
      }
      if (img->complete && img->img && img->img.width > 0) {
        const double a = static_cast<double>(img->img.width) / img->img.height;
        g.drawImage(img->img, x, y, a >= 1 ? size : size * a, a >= 1 ? size / a : size);
      } else if (!img->complete) {
        img->onload.push_back([canvas, id, t, you, signBase] { drawPlan(canvas, id, t, you, signBase); });
      }
    }
  }
  return true;
}

}  // namespace fs
