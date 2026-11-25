// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/drillplay.ts @ v1.7.7
#include "game/drillplay.h"

#include <cmath>

#include "core/js.h"
#include "i18n/index.h"
#include "render/drillyard.h"
#include "render/yard.h"

namespace fs {

namespace {

/** '★★☆' */
std::string stars(int n) {
  std::string s;
  for (int i = 0; i < n; i++) s += "★";
  for (int i = 0; i < 3 - n; i++) s += "☆";
  return s;
}

/** `[a, b].filter(Boolean).join(' ')` */
std::string joinSome(const std::string& a, const std::string& b) { return a.empty() ? b : b.empty() ? a : a + " " + b; }

}  // namespace

DrillRun::DrillRun(std::vector<DrillId> ids_, const YardFrame& frame, const CarShape& shape, YardScene& yardScene, DrillScene& scene, std::function<std::string()> lang, std::function<double()> rand)
    : ids(std::move(ids_)), frame_(frame), shape_(shape), yardScene_(yardScene), scene_(scene), lang_(std::move(lang)), rand_(std::move(rand)) {
  judge = std::make_unique<DrillJudge>(ids[0], shape_, rand_);
}

Pose DrillRun::start() const { return frame_.poseToWorld(layout().start); }

bool DrillRun::paved(double x, double z) const {
  const auto [u, w] = frame_.toLocal(x, z);
  return offArea(layout(), u, w, 1.2) == 0;
}

double DrillRun::raise(double x, double z) const {
  const std::optional<Hill>& h = layout().hill;
  if (!h) return 0;
  const auto [u, w] = frame_.toLocal(x, z);
  return hillRise(*h, u, w);
}

void DrillRun::lay(const std::function<double(double, double)>& heightAt) {
  if (laid_ || std::abs(now_ - laidTry_) < 0.7) return;
  laidTry_ = now_;
  const auto withRamp = [this, &heightAt](double x, double z) { return heightAt(x, z) + raise(x, z); };
  const bool a = yardScene_.show(frame_, layout(), withRamp);
  const bool b = scene_.show(frame_, layout(), withRamp);
  laid_ = a && b;
}

const DrillEvents& DrillRun::update(double dt, const DrillDrive& d, const std::function<double(double, double)>& heightAt) {
  now_ += dt;
  DrillEvents& ev = ev_;
  ev.faults.clear();
  ev.remark = "";
  ev.signal = false;
  ev.stop = false;
  ev.next = "";
  const std::string lang = lang_();
  lay(heightAt);
  if (overAt_ >= 0) {
    if (now_ >= overAt_) ev.next = "over";
    return ev;
  }
  DrillJudge& j = *judge;
  const DrillStatus& st = j.update({.pose = frame_.poseToLocal({d.x, d.z, d.yaw}), .speed = d.speed, .handbrake = d.handbrake, .brake = d.brake, .engine = d.engine}, dt);
  // (the first try: a fault that ends the task is a serious one – the task is repeated)
  for (const DrillFault& f : st.faults) {
    DrillFault g = f;
    if (f.severity == "fail" && !second) g.severity = "major";
    ev.faults.push_back(g);
  }
  if (!st.remark.empty()) ev.remark = tr(st.remark, lang, st.remarkData);
  ev.signal = st.signal;
  ev.stop = st.stop;
  if (st.state == "run") return ev;
  if (st.state == "failed" && !second) {
    second = true;
    next(ids[idx_]);
    ev.next = "again";
    ev.remark = joinSome(ev.remark, tr("drill.again", lang));
    return ev;
  }
  const DrillResult r = j.result();
  results.push_back(r);
  ev.remark = joinSome(ev.remark, r.failed ? tr("drill.failed", lang, {{"name", tr(r.text, lang)}}) : tr("drill.done", lang, {{"name", tr(r.text, lang)}, {"stars", stars(r.stars)}, {"score", r.score}}));
  if (r.failed) failed = true;
  if (!r.failed && idx_ + 1 < ids.size()) {
    idx_++;
    second = false;
    next(ids[idx_]);
    ev.next = "next";
    return ev;
  }
  // (a moment to read the last remark, then the result)
  overAt_ = now_ + 3;
  return ev;
}

void DrillRun::next(const DrillId& id) {
  judge = std::make_unique<DrillJudge>(id, shape_, rand_);
  laid_ = false;
  laidTry_ = -10;
}

std::string DrillRun::line() const {
  const std::string lang = lang_();
  const DrillLayout& lay = layout();
  const DrillStatus& st = judge->status;
  const std::string& step = lay.steps[size_t(std::min(st.step, int(lay.steps.size()) - 1))];
  return std::to_string(st.step + 1) + "/" + std::to_string(lay.steps.size()) + " · " + tr("drill.step." + step, lang);
}

std::string DrillRun::sub() const {
  const std::string lang = lang_();
  const DrillJudge& j = *judge;
  const DrillStatus& st = j.status;
  std::string out = tr("drill." + j.id + ".name", lang);
  const auto push = [&out](const std::string& s) { out += " · " + s; };
  if (ids.size() > 1) push(tr("drill.sub.task", lang, {{"n", int(idx_ + 1)}, {"max", int(ids.size())}}));
  if (second) push(tr("drill.sub.second", lang));
  if (j.layout.moves >= 0) push(tr("drill.sub.corrections", lang, {{"n", st.corrections}, {"max", MAX_CORRECTIONS}}));
  if (std::isfinite(st.kerb) && st.kerb < 3) push(tr("drill.sub.kerb", lang, {{"d", jsRound(st.kerb * 100)}}));
  if (j.id == "hill" && st.step > 0) push(tr("drill.sub.rollback", lang, {{"d", jsRound(st.rollback * 100)}}));
  return out;
}

std::optional<dom::El> DrillRun::resultNode() const {
  if (results.empty()) return std::nullopt;
  const std::string lang = lang_();
  dom::El box = dom::el("div", "crane-results");
  dom::El head = dom::el("p", "crane-total");
  double sum = 0;
  int got = 0;
  int done = 0;
  for (const DrillResult& r : results) {
    sum += r.score;
    got += r.stars;
    if (!r.failed) done++;
  }
  const double total = jsRound(sum / double(results.size()));
  head.textContent(tr("drillResult.total", lang, {{"done", done}, {"max", int(ids.size())}, {"score", total}, {"stars", got}, {"all", 3 * int(ids.size())}}));
  box.append(head);
  for (size_t i = 0; i < results.size(); i++) {
    const DrillResult& r = results[i];
    dom::El row = dom::el("div", std::string("fault ") + (r.stars >= 2 ? "hint" : r.stars == 1 ? "minor" : "major"));
    dom::El b = dom::el("b");
    b.textContent(std::to_string(i + 1) + " · " + tr(r.text, lang) + "  " + stars(r.stars));
    dom::El small = dom::el("small");
    const int min = int(std::floor(r.time / 60));
    const int sec = int(jsRound(std::fmod(r.time, 60)));
    const std::string secs = sec < 10 ? "0" + std::to_string(sec) : std::to_string(sec);
    const std::string line = r.failed ? tr("drillResult.failed", lang) : tr("drillResult.line", lang, {{"score", r.score}, {"time", std::to_string(min) + ":" + secs}, {"minor", r.minor}, {"major", r.major}});
    small.textContent(line + " · " + tr(r.measure, lang, r.measureData));
    row.append(b);
    row.append(small);
    box.append(row);
  }
  return box;
}

void DrillRun::hide() {
  yardScene_.hide();
  scene_.hide();
}

}  // namespace fs
