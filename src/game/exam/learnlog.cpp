// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/exam/learnlog.ts @ v1.7.7
#include "game/exam/learnlog.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unordered_map>
#include <unordered_set>

#include "core/date.h"
#include "core/js.h"
#include "platform/plat.h"

namespace fs {

namespace {

/**
 * `Date.parse(text)` of the moments the tours are written with (`new Date().toISOString()`:
 * "2026-11-01T10:00:00.000Z"), also without the milliseconds, with an offset, or in the
 * reader's own time; a date alone is UTC – NaN for anything else.
 */
double dateParse(const std::string& text) {
  int year = 0, month = 0, day = 0, hours = 0, minutes = 0, seconds = 0, used = 0;
  if (std::sscanf(text.c_str(), "%4d-%2d-%2d%n", &year, &month, &day, &used) < 3 || used != 10) return JS_NAN;
  if (month < 1 || month > 12 || day < 1 || day > 31) return JS_NAN;
  if (text.size() == 10) return utcMs(year, month - 1, day);
  size_t at = 10;
  if (text[at] != 'T' || std::sscanf(text.c_str() + at, "T%2d:%2d%n", &hours, &minutes, &used) < 2 || used != 6) return JS_NAN;
  at += 6;
  if (at < text.size() && text[at] == ':') {
    if (std::sscanf(text.c_str() + at, ":%2d%n", &seconds, &used) < 1 || used != 3) return JS_NAN;
    at += 3;
  }
  int ms = 0;
  if (at < text.size() && text[at] == '.') {
    size_t end = at + 1;
    double frac = 0, scale = 100;
    while (end < text.size() && text[end] >= '0' && text[end] <= '9') {
      frac += (text[end] - '0') * scale;
      scale /= 10;
      end++;
    }
    if (end == at + 1) return JS_NAN;
    ms = static_cast<int>(std::floor(frac));
    at = end;
  }
  if (hours > 24 || minutes > 59 || seconds > 59) return JS_NAN;
  const double utc = utcMs(year, month - 1, day, hours, minutes, seconds, ms);
  if (at == text.size()) return utc + plat::timezoneOffsetMin(utc + plat::timezoneOffsetMin(utc) * 60000) * 60000;
  if (text[at] == 'Z' && at + 1 == text.size()) return utc;
  int oh = 0, om = 0;
  if ((text[at] == '+' || text[at] == '-') && std::sscanf(text.c_str() + at + 1, "%2d:%2d%n", &oh, &om, &used) == 2 && used == 5 && at + 6 == text.size())
    return utc - (text[at] == '+' ? 1 : -1) * (oh * 60 + om) * 60000.0;
  return JS_NAN;
}

LogRow lessonRow(const std::string& id, const LessonRecord* r, double lastFault) {
  if (r && r->passes > 0) return LogRow{id, "passed", r->stars, r->passedAt != 0 ? r->passedAt : r->last, r->tries};
  if ((r && r->tries > 0) || lastFault > 0) return LogRow{id, "tried", std::nullopt, std::max(r ? r->last : 0, lastFault), std::max(r ? r->tries : 0, lastFault > 0 ? 1.0 : 0.0)};
  return LogRow{id, "none", std::nullopt, 0, 0};
}

LogSection section(const std::string& id, std::vector<LogRow> rows) {
  LogSection s{id, std::move(rows), 0, 0};
  s.passed = static_cast<int>(std::count_if(s.rows.begin(), s.rows.end(), [](const LogRow& r) { return r.state == "passed"; }));
  s.total = static_cast<int>(s.rows.size());
  return s;
}

}  // namespace

LearnLog learnLog(const LogInput& o) {
  std::unordered_map<std::string, double> lastFault;
  for (const LogFault& f : o.faults) {
    auto it = lastFault.find(f.mode);
    lastFault[f.mode] = std::max(it == lastFault.end() ? 0 : it->second, f.t);
  }
  const auto faultOf = [&lastFault](const std::string& id) {
    auto it = lastFault.find(id);
    return it == lastFault.end() ? 0.0 : it->second;
  };
  std::vector<LogRow> lessonRows;
  for (const std::string& id : o.lessons) lessonRows.push_back(lessonRow(id, o.learn.lessons.find(id), faultOf(id)));
  std::vector<LogRow> drillRows;
  for (const std::string& id : o.drills) drillRows.push_back(lessonRow(id, o.learn.drills.find(id), 0));
  const std::unordered_set<std::string> done(o.theoryDone.begin(), o.theoryDone.end());
  std::vector<LogRow> theoryRows;
  for (const std::string& id : o.theory) theoryRows.push_back(done.count(id) ? LogRow{id, "passed", std::nullopt, 0, 1} : LogRow{id, "none", std::nullopt, 0, 0});
  std::vector<LogRow> dangerRows;
  for (const std::string& id : o.danger) {
    const DangerStat* s = o.learn.danger.find(id);
    if (!s) dangerRows.push_back(LogRow{id, "none", std::nullopt, 0, 0});
    else dangerRows.push_back(LogRow{id, s->ok ? "passed" : "tried", std::nullopt, s->last, s->right + s->wrong});
  }
  const std::vector<ExamRecord>& ex = o.learn.exams;
  std::vector<LogRow> examRows;
  for (const ExamRecord& e : ex) examRows.push_back(LogRow{jsStr(e.t), e.passed ? "passed" : "tried", std::nullopt, e.t, 1});
  double best = 0;
  double max = 0;
  for (const ExamRecord& e : ex) {
    if (e.points / std::max(1.0, e.max) > best / std::max(1.0, max) || max == 0) {
      best = e.points;
      max = e.max;
    }
  }
  LearnLog log;
  log.sections = {section("lessons", std::move(lessonRows)), section("drills", std::move(drillRows)), section("theory", std::move(theoryRows)), section("danger", std::move(dangerRows)),
                  section("exams", std::move(examRows))};
  log.exams.taken = static_cast<int>(ex.size());
  log.exams.passed = static_cast<int>(std::count_if(ex.begin(), ex.end(), [](const ExamRecord& e) { return e.passed; }));
  log.exams.best = best;
  log.exams.max = max;
  for (const ExamRecord& e : ex) log.exams.last = std::max(log.exams.last, e.t);
  log.signs = signProgress(o.learn.signs, o.now);
  log.tours.driven = static_cast<int>(o.tours.size());
  bool any = false;
  for (const LogTour& t : o.tours) {
    if (t.done) log.tours.done++;
    const double ms = dateParse(t.date);
    if (std::isfinite(ms)) {
      log.tours.last = any ? std::max(log.tours.last, ms) : ms;
      any = true;
    }
  }
  return log;
}

}  // namespace fs
