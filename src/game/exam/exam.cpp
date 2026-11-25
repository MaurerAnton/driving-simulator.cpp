// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/exam/exam.ts @ v1.7.7
#include "game/exam/exam.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <set>
#include <unordered_set>

#include "core/js.h"

namespace fs {

Rand exam::seeded(uint32_t seed) {
  // (the arithmetic of Math.imul, `^` and `>>>` is that of 32-bit unsigned numbers)
  auto a = std::make_shared<uint32_t>(seed);
  return [a]() {
    *a = *a + 0x6d2b79f5u;
    uint32_t t = *a;
    t = (t ^ (t >> 15)) * (t | 1u);
    t ^= t + (t ^ (t >> 7)) * (t | 61u);
    return static_cast<double>(t ^ (t >> 14)) / 4294967296.0;
  };
}

namespace {

bool isClass(const ExamQuestion& q) { return std::find(CLASS_TOPICS.begin(), CLASS_TOPICS.end(), q.topic) != CLASS_TOPICS.end(); }

/** `n` questions spread over the topics as evenly as can be: one of each topic in turn, the topics in a random order. */
std::vector<const ExamQuestion*> spread(const std::vector<const ExamQuestion*>& pool, int n, const Rand& rand) {
  // (a Map in the order the topics were put in; a topic emptied is deleted from it)
  std::vector<std::pair<ExamTopic, std::vector<const ExamQuestion*>>> byTopic;
  for (const ExamTopic& t : shuffle(EXAM_TOPICS, rand)) {
    std::vector<const ExamQuestion*> qs;
    for (const ExamQuestion* q : pool)
      if (q->topic == t) qs.push_back(q);
    if (!qs.empty()) byTopic.emplace_back(t, shuffle(qs, rand));
  }
  std::vector<const ExamQuestion*> out;
  while (static_cast<int>(out.size()) < n && !byTopic.empty()) {
    for (size_t i = 0; i < byTopic.size();) {
      if (static_cast<int>(out.size()) >= n) break;
      std::vector<const ExamQuestion*>& qs = byTopic[i].second;
      const ExamQuestion* q = qs.back();
      qs.pop_back();
      out.push_back(q);
      if (qs.empty()) byTopic.erase(byTopic.begin() + static_cast<std::ptrdiff_t>(i));
      else i++;
    }
  }
  return out;
}

}  // namespace

std::vector<const ExamQuestion*> pickExam(const std::vector<ExamQuestion>& bank, const Rand& rand, int target) {
  std::vector<const ExamQuestion*> basicPool;
  std::vector<const ExamQuestion*> classPool;
  for (const ExamQuestion& q : bank) (isClass(q) ? classPool : basicPool).push_back(&q);
  std::vector<const ExamQuestion*> chosen = spread(basicPool, EXAM_BASIC, rand);
  for (const ExamQuestion* q : spread(classPool, EXAM_CLASS, rand)) chosen.push_back(q);
  std::unordered_set<std::string> used;
  int sum = 0;
  for (const ExamQuestion* q : chosen) {
    used.insert(q->id);
    sum += q->points;
  }
  for (int tries = 0; tries < 2000 && sum != target; tries++) {
    const int diff = target - sum;
    const size_t i = static_cast<size_t>(std::floor(rand() * static_cast<double>(chosen.size())));
    const ExamQuestion* q = chosen[i];
    const std::vector<const ExamQuestion*>& pool = isClass(*q) ? classPool : basicPool;
    auto fits = [&](const ExamQuestion* c) {
      const int d = c->points - q->points;
      return !used.count(c->id) && d != 0 && jsSign(d) == jsSign(diff) && std::abs(d) <= std::abs(diff);
    };
    std::vector<const ExamQuestion*> same;
    for (const ExamQuestion* c : pool)
      if (c->topic == q->topic && fits(c)) same.push_back(c);
    // (another topic only while this one keeps a question in the exam)
    const bool keeps = std::count_if(chosen.begin(), chosen.end(), [q](const ExamQuestion* c) { return c->topic == q->topic; }) > 1;
    std::vector<const ExamQuestion*> any;
    if (!same.empty() || !keeps) any = same;
    else
      for (const ExamQuestion* c : pool)
        if (fits(c)) any.push_back(c);
    if (any.empty()) continue;
    const ExamQuestion* c = any[static_cast<size_t>(std::floor(rand() * static_cast<double>(any.size())))];
    used.erase(q->id);
    used.insert(c->id);
    chosen[i] = c;
    sum += c->points - q->points;
  }
  return chosen;
}

namespace {

bool isDigit(char c) { return c >= '0' && c <= '9'; }

/** `s.replace(/\s+/g, '')` – the white space of JavaScript as UTF-8 */
std::string withoutSpace(const std::string& s) {
  std::string out;
  for (size_t i = 0; i < s.size();) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c == ' ' || (c >= 0x09 && c <= 0x0d)) {
      i++;
      continue;
    }
    // U+00A0, U+1680, U+2000–U+200A, U+2028, U+2029, U+202F, U+205F, U+3000, U+FEFF
    if (c == 0xc2 && i + 1 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0xa0) {
      i += 2;
      continue;
    }
    if (i + 2 < s.size()) {
      const unsigned c1 = static_cast<unsigned char>(s[i + 1]);
      const unsigned c2 = static_cast<unsigned char>(s[i + 2]);
      const unsigned cp = ((c & 0x0f) << 12) | ((c1 & 0x3f) << 6) | (c2 & 0x3f);
      if ((c & 0xf0) == 0xe0 && (cp == 0x1680 || (cp >= 0x2000 && cp <= 0x200a) || cp == 0x2028 || cp == 0x2029 || cp == 0x202f || cp == 0x205f || cp == 0x3000 || cp == 0xfeff)) {
        i += 3;
        continue;
      }
    }
    out += s[i++];
  }
  return out;
}

}  // namespace

double typedNumber(const std::string& typed) {
  const std::string s0 = withoutSpace(typed);
  // .replace(/(\d)[.](\d{3})(?!\d)/g, '$1$2'): a point between thousands goes
  std::string s;
  for (size_t i = 0; i < s0.size();) {
    if (isDigit(s0[i]) && i + 4 < s0.size() && s0[i + 1] == '.' && isDigit(s0[i + 2]) && isDigit(s0[i + 3]) && isDigit(s0[i + 4]) && !(i + 5 < s0.size() && isDigit(s0[i + 5]))) {
      s += s0[i];
      s += s0.substr(i + 2, 3);
      i += 5;
    } else s += s0[i++];
  }
  // /-?\d+(?:[.,]\d+)?/
  for (size_t i = 0; i < s.size(); i++) {
    size_t at = i;
    if (s[at] == '-') at++;
    if (at >= s.size() || !isDigit(s[at])) continue;
    size_t end = at;
    while (end < s.size() && isDigit(s[end])) end++;
    if (end + 1 < s.size() && (s[end] == '.' || s[end] == ',') && isDigit(s[end + 1])) {
      end++;
      while (end < s.size() && isDigit(s[end])) end++;
    }
    std::string m = s.substr(i, end - i);
    const size_t comma = m.find(',');
    if (comma != std::string::npos) m[comma] = '.';
    return jsNumber(m);
  }
  return JS_NAN;
}

bool isRight(const ExamQuestion& q, const ExamAnswer* a) {
  if (!a) return false;
  if (q.number) return std::holds_alternative<std::string>(*a) && std::abs(typedNumber(std::get<std::string>(*a)) - q.number->value) < 1e-9;
  if (std::holds_alternative<std::string>(*a)) return false;
  const std::vector<int>& ticked = std::get<std::vector<int>>(*a);
  const std::set<int> got(ticked.begin(), ticked.end());
  const std::set<int> want(q.correct.begin(), q.correct.end());
  return got.size() == q.correct.size() && got == want;
}

ExamScore scoreExam(const std::vector<const ExamQuestion*>& qs, const ExamAnswers& answers) {
  ExamScore s;
  for (const ExamQuestion* q : qs) {
    s.max += q->points;
    if (isRight(*q, answerOf(answers, q->id))) s.points += q->points;
    else {
      s.wrong.push_back(q->id);
      if (q->points == 5) s.wrongFive++;
    }
  }
  s.errors = s.max - s.points;
  s.passed = s.errors <= EXAM_MAX_ERRORS && s.wrongFive < 2;
  return s;
}

double timeLeft(double start, double now, double minutes) { return std::max(0.0, start + minutes * 60'000 - now); }

std::string clockText(double ms) {
  const double s = std::max(0.0, std::ceil(ms / 1000));
  return jsStr(std::floor(s / 60)) + ":" + padStart(jsStr(std::fmod(s, 60)), 2, '0');
}

bool answered(const ExamAnswer* a) {
  if (!a) return false;
  if (std::holds_alternative<std::string>(*a)) return trim(std::get<std::string>(*a)) != "";
  return !std::get<std::vector<int>>(*a).empty();
}

}  // namespace fs
