// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/exam/signs.ts @ v1.7.7
#include "game/exam/signs.h"

#include <algorithm>
#include <cmath>

#include "core/js.h"
#include "i18n/exam/index.h"

namespace fs {

namespace {

/** the digits at `at` (none: '') */
std::string digitsAt(const std::string& s, size_t at) {
  size_t end = at;
  while (end < s.size() && s[end] >= '0' && s[end] <= '9') end++;
  return s.substr(at, end - at);
}

/** /^(\d+)(?:\.(\d+))?(?:-(\d+))?/ */
double signOrder(const std::string& code) {
  const std::string a = digitsAt(code, 0);
  if (a.empty()) return 1e9;
  size_t at = a.size();
  std::string b;
  std::string c;
  if (at < code.size() && code[at] == '.') {
    b = digitsAt(code, at + 1);
    if (!b.empty()) at += 1 + b.size();
  }
  if (at < code.size() && code[at] == '-') c = digitsAt(code, at + 1);
  return jsNumber(a) * 1e4 + (b.empty() ? 0 : jsNumber(b)) * 1e3 + (c.empty() ? 0 : jsNumber(c));
}

}  // namespace

namespace detail {

const std::vector<std::string>& trainerSigns() {
  static const auto* list = [] {
    // (detail::examEn(), not EXAM_EN: this may be made before that reference is set)
    auto* l = new std::vector<std::string>(detail::examEn().signs.keys());
    std::stable_sort(l->begin(), l->end(), [](const std::string& a, const std::string& b) { return signOrder(a) < signOrder(b); });
    return l;
  }();
  return *list;
}

}  // namespace detail

std::string signFamily(const std::string& code) {
  const std::string d = digitsAt(code, 0);
  const double n = d.empty() ? 0 : jsNumber(d);
  if (n >= 1000) return "panel";
  if (n < 200) return "danger";
  if (n == 274 || n == 275 || n == 278 || n == 279) return "speed";
  if (n < 300) return "rule";
  return "info";
}

std::vector<std::string> signChoices(const std::string& code, const Rand& rand, const std::function<std::string(const std::string&)>& label, const std::vector<std::string>& codes, size_t n) {
  std::unordered_set<std::string> seen = {label(code)};
  std::vector<std::string> out = {code};
  const std::string fam = signFamily(code);
  std::vector<std::string> sameList;
  std::vector<std::string> otherList;
  for (const std::string& c : codes)
    if (c != code && signFamily(c) == fam) sameList.push_back(c);
  const std::vector<std::string> same = shuffle(sameList, rand);
  for (const std::string& c : codes)
    if (c != code && signFamily(c) != fam) otherList.push_back(c);
  const std::vector<std::string> other = shuffle(otherList, rand);
  std::vector<std::string> all = same;
  all.insert(all.end(), other.begin(), other.end());
  for (const std::string& c : all) {
    if (out.size() >= n) break;
    const std::string l = label(c);
    if (seen.count(l)) continue;
    seen.insert(l);
    out.push_back(c);
  }
  return shuffle(out, rand);
}

SignStat review(const SignStat* s, bool right, double now) {
  const double box = right ? std::min<double>(TOP_BOX, (s ? s->box : 0) + 1) : 0;
  return SignStat{box, now + BOX_WAIT[static_cast<size_t>(box)], (s ? s->right : 0) + (right ? 1 : 0), (s ? s->wrong : 0) + (right ? 0 : 1), now};
}

std::string nextSign(const Record<SignStat>& stats, double now, const std::string& avoid, const std::vector<std::string>& codes) {
  std::vector<std::string> pool;
  if (codes.size() > 1) {
    for (const std::string& c : codes)
      if (c != avoid) pool.push_back(c);
  } else pool = codes;
  std::vector<std::string> due;
  for (const std::string& c : pool)
    if (const SignStat* s = stats.find(c); s && s->due <= now) due.push_back(c);
  if (!due.empty()) {
    std::stable_sort(due.begin(), due.end(), [&stats](const std::string& a, const std::string& b) {
      const SignStat& x = *stats.find(a);
      const SignStat& y = *stats.find(b);
      return x.box != y.box ? x.box < y.box : x.due < y.due;
    });
    return due[0];
  }
  for (const std::string& c : pool)
    if (!stats.find(c)) return c;
  std::string best = pool.empty() ? avoid : pool[0];
  for (const std::string& c : pool) {
    const SignStat* s = stats.find(c);
    const SignStat* b = stats.find(best);
    if ((s ? s->due : 0) < (b ? b->due : JS_INF)) best = c;
  }
  return best;
}

SignProgress signProgress(const Record<SignStat>& stats, double now, const std::vector<std::string>& codes) {
  SignProgress p;
  for (const std::string& c : codes) {
    const SignStat* s = stats.find(c);
    if (!s) continue;
    p.seen++;
    if (s->box >= LEARNED_BOX) p.learned++;
    if (s->due <= now) p.due++;
  }
  p.total = static_cast<int>(codes.size());
  return p;
}

}  // namespace fs
