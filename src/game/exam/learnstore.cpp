// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/exam/learnstore.ts @ v1.7.7
#include "game/exam/learnstore.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

#include "core/js.h"
#include "core/jsonwrite.h"

namespace fs {

namespace {

const Json& nothing() {
  static const auto* j = new Json(Json::makeObject());
  return *j;
}

/** `v && typeof v === 'object'` (an array is one too) */
bool isObjectLike(const Json& v) { return v.isObject() || v.isArray(); }

/** obj(v): the object, or {} (an array: {} too – its members are not read) */
const Json& obj(const Json& v) { return v.isObject() ? v : nothing(); }

/** num(v): a finite number, else 0 */
double num(const Json& v) { return v.isNumber() && std::isfinite(v.number) ? v.number : 0; }

template <class T, class F>
Record<T> record(const Json& v, F fix) {
  Record<T> out;
  for (const auto& [k, x] : obj(v).object)
    if (isObjectLike(x)) out.set(k, fix(obj(x)));
  return out;
}

SignStat fixSign(const Json& x) { return SignStat{std::max(0.0, std::min(5.0, jsRound(num(x.at("box"))))), num(x.at("due")), num(x.at("right")), num(x.at("wrong")), num(x.at("last"))}; }
DangerStat fixDanger(const Json& x) { return DangerStat{num(x.at("right")), num(x.at("wrong")), x.at("ok").isBool() && x.at("ok").boolean, num(x.at("last"))}; }
LessonRecord fixLesson(const Json& x) { return LessonRecord{num(x.at("tries")), num(x.at("passes")), std::max(0.0, std::min(3.0, num(x.at("stars")))), num(x.at("last")), num(x.at("passedAt"))}; }
ExamRecord fixExam(const Json& x) {
  ExamRecord e{num(x.at("t")), num(x.at("points")), num(x.at("max")), num(x.at("errors")), x.at("passed").isBool() && x.at("passed").boolean, {}, num(x.at("secs"))};
  if (x.at("wrong").isArray())
    for (const Json& w : x.at("wrong").array)
      if (w.isString()) e.wrong.push_back(w.str);
  return e;
}

void sortExams(std::vector<ExamRecord>& exams) {
  std::stable_sort(exams.begin(), exams.end(), [](const ExamRecord& a, const ExamRecord& b) { return b.t < a.t; });
  if (exams.size() > EXAMS_KEPT) exams.resize(EXAMS_KEPT);
}

}  // namespace

LearnData cleanLearn(const Json& v) {
  const Json& o = obj(v);
  LearnData d;
  if (o.at("exams").isArray())
    for (const Json& e : o.at("exams").array)
      if (isObjectLike(e)) d.exams.push_back(fixExam(obj(e)));
  d.signs = record<SignStat>(o.at("signs"), fixSign);
  d.danger = record<DangerStat>(o.at("danger"), fixDanger);
  d.lessons = record<LessonRecord>(o.at("lessons"), fixLesson);
  d.drills = record<LessonRecord>(o.at("drills"), fixLesson);
  return d;
}

std::string learnText(const LearnData& d) {
  JsonOut o;
  o.beginObject();
  o.key("exams");
  o.beginArray();
  for (const ExamRecord& e : d.exams) {
    o.beginObject();
    o.key("t");
    o.number(e.t);
    o.key("points");
    o.number(e.points);
    o.key("max");
    o.number(e.max);
    o.key("errors");
    o.number(e.errors);
    o.key("passed");
    o.boolean(e.passed);
    o.key("wrong");
    o.beginArray();
    for (const std::string& w : e.wrong) o.string(w);
    o.endArray();
    o.key("secs");
    o.number(e.secs);
    o.endObject();
  }
  o.endArray();
  o.key("signs");
  o.beginObject();
  for (const auto& [k, s] : d.signs) {
    o.key(k);
    o.beginObject();
    o.key("box");
    o.number(s.box);
    o.key("due");
    o.number(s.due);
    o.key("right");
    o.number(s.right);
    o.key("wrong");
    o.number(s.wrong);
    o.key("last");
    o.number(s.last);
    o.endObject();
  }
  o.endObject();
  o.key("danger");
  o.beginObject();
  for (const auto& [k, s] : d.danger) {
    o.key(k);
    o.beginObject();
    o.key("right");
    o.number(s.right);
    o.key("wrong");
    o.number(s.wrong);
    o.key("ok");
    o.boolean(s.ok);
    o.key("last");
    o.number(s.last);
    o.endObject();
  }
  o.endObject();
  const auto lessons = [&o](const char* name, const Record<LessonRecord>& list) {
    o.key(name);
    o.beginObject();
    for (const auto& [k, r] : list) {
      o.key(k);
      o.beginObject();
      o.key("tries");
      o.number(r.tries);
      o.key("passes");
      o.number(r.passes);
      o.key("stars");
      o.number(r.stars);
      o.key("last");
      o.number(r.last);
      o.key("passedAt");
      o.number(r.passedAt);
      o.endObject();
    }
    o.endObject();
  };
  lessons("lessons", d.lessons);
  lessons("drills", d.drills);
  o.endObject();
  return o.text();
}

Json learnJson(const LearnData& d) {
  Json j;
  Json::parse(learnText(d), j);
  return j;
}

LearnData loadLearn(LearnStore* store) {
  if (!store) return emptyLearn();
  const std::optional<std::string> raw = store->getItem(LEARN_KEY);
  if (!raw || raw->empty()) return emptyLearn();
  Json v;
  if (!Json::parse(*raw, v)) return emptyLearn();
  return cleanLearn(v);
}

void saveLearn(const LearnData& d, LearnStore* store) {
  // (private window, storage full: kept as long as the page)
  if (store) store->setItem(LEARN_KEY, learnText(d));
}

LearnData updateLearn(const std::function<void(LearnData& d)>& change, LearnStore* store) {
  LearnData d = loadLearn(store);
  change(d);
  saveLearn(d, store);
  return d;
}

void addExam(LearnData& d, const ExamRecord& e) {
  std::vector<ExamRecord> list = {e};
  for (const ExamRecord& x : d.exams)
    if (x.t != e.t) list.push_back(x);
  sortExams(list);
  d.exams = std::move(list);
}

void noteTry(Record<LessonRecord>& list, const std::string& id, bool passed, double stars, double now) {
  const LessonRecord* found = list.find(id);
  const LessonRecord r = found ? *found : LessonRecord{};
  list.set(id, LessonRecord{r.tries + 1, r.passes + (passed ? 1 : 0), passed ? std::max(r.stars, std::max(0.0, std::min(3.0, jsRound(stars)))) : r.stars, now, passed ? now : r.passedAt});
}

int lessonStars(bool passed, double faultPoints) {
  if (!passed) return 0;
  return faultPoints <= 0 ? 3 : faultPoints <= 4 ? 2 : 1;
}

void noteLessonEnd(const std::string& id, bool passed, double faultPoints, const std::vector<DrillTry>& drills, double now, LearnStore* store) {
  if (now != now) now = plat::dateMs();
  updateLearn(
      [&](LearnData& d) {
        double sum = 0;
        for (const DrillTry& r : drills) sum += r.stars;
        const double stars = !drills.empty() ? jsRound(sum / static_cast<double>(drills.size())) : lessonStars(passed, faultPoints);
        noteTry(d.lessons, id, passed, stars, now);
        for (const DrillTry& r : drills) noteTry(d.drills, r.id, !r.failed, r.stars, now);
      },
      store);
}

// ------------------------------------------------------------------ merging two copies

namespace {

/** of two items, the one answered (tried) more often – on a tie the later */
template <class T, class W, class L>
Record<T> mergeBy(const Record<T>& a, const Record<T>& b, W weight, L last) {
  Record<T> out = a;
  for (const auto& [k, v] : b) {
    const T* mine = out.find(k);
    if (!mine || weight(v) > weight(*mine) || (weight(v) == weight(*mine) && last(v) > last(*mine))) out.set(k, v);
  }
  return out;
}

Record<LessonRecord> mergeLessons(const Record<LessonRecord>& a, const Record<LessonRecord>& b) {
  Record<LessonRecord> out = a;
  for (const auto& [k, v] : b) {
    const LessonRecord* m = out.find(k);
    out.set(k, m ? LessonRecord{std::max(m->tries, v.tries), std::max(m->passes, v.passes), std::max(m->stars, v.stars), std::max(m->last, v.last), std::max(m->passedAt, v.passedAt)} : v);
  }
  return out;
}

}  // namespace

LearnData mergeLearn(const Json& local, const Json& remote) {
  const LearnData l = cleanLearn(local);
  const LearnData r = cleanLearn(remote);
  LearnData out;
  std::unordered_set<double> seen;
  for (const std::vector<ExamRecord>* list : {&l.exams, &r.exams})
    for (const ExamRecord& e : *list)
      if (seen.insert(e.t).second) out.exams.push_back(e);
  sortExams(out.exams);
  out.signs = mergeBy(l.signs, r.signs, [](const SignStat& s) { return s.right + s.wrong; }, [](const SignStat& s) { return s.last; });
  out.danger = mergeBy(l.danger, r.danger, [](const DangerStat& s) { return s.right + s.wrong; }, [](const DangerStat& s) { return s.last; });
  out.lessons = mergeLessons(l.lessons, r.lessons);
  out.drills = mergeLessons(l.drills, r.drills);
  return out;
}

}  // namespace fs
