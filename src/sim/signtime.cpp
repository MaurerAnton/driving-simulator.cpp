// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/signtime.ts @ v1.7.7
#include "sim/signtime.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <regex>
#include <set>

#include "core/date.h"
#include "core/js.h"
#include "sim/timetable.h"

namespace fs {

namespace {

// ---- the signs ------------------------------------------------------------------------------

constexpr int ALL_DAYS = 0x7f;
/** minutes of a day */
constexpr double DAY = 24 * 60;

const std::map<std::string, int>& dayNames() {
  static auto* names = new std::map<std::string, int>{{"su", 0}, {"mo", 1}, {"tu", 2}, {"we", 3}, {"th", 4}, {"fr", 5}, {"sa", 6}, {"so", 0}, {"di", 2}, {"mi", 3}, {"do", 4}};
  return *names;
}

/** "Mo-Fr", "Sa,Su", "Tu,Fr", "PH", "Mo-Su,PH" → a day set; none if it is not one. */
std::optional<int> parseDays(const std::string& text) {
  static const std::regex re(R"(^([a-z]{2})(?:\s*-\s*([a-z]{2}))?$)");
  int days = 0;
  for (const std::string& part : split(text, ",")) {
    const std::string p = toLowerAscii(trim(part));
    if (p.empty()) continue;
    if (p == "ph") {
      days |= PH;
      continue;
    }
    std::smatch m;
    if (!std::regex_match(p, m, re)) return std::nullopt;
    const auto& names = dayNames();
    const auto ia = names.find(m[1].str());
    const auto ib = m[2].matched ? names.find(m[2].str()) : ia;
    if (ia == names.end() || ib == names.end()) return std::nullopt;
    const int a = ia->second;
    const int b = ib->second;
    // (Monday first: "Fr-Mo" runs over the weekend)
    const int ma = (a + 6) % 7;
    const int mb = (b + 6) % 7;
    for (int k = ma;; k = (k + 1) % 7) {
      days |= 1 << ((k + 1) % 7);
      if (k == mb) break;
    }
  }
  if (days == 0) return std::nullopt;
  return days;
}

/** "07:00", "7:30", "7", "7.30", "24:00", "43:00" → minutes (none if it is not a time). */
std::optional<double> parseClock(const std::string& text) {
  static const std::regex re(R"(^(\d{1,2})(?:[:.](\d{2}))?$)");
  const std::string t = trim(text);
  std::smatch m;
  if (!std::regex_match(t, m, re)) return std::nullopt;
  const double h = jsNumber(m[1].str());
  const double min = m[2].matched ? jsNumber(m[2].str()) : 0;
  if (h > 48 || min > 59) return std::nullopt;
  return h * 60 + min;
}

/** "07:00-17:00", "22-6", "16:00-43:00" → [from, to] in minutes after the midnight it begins after (`to` beyond 1440: into the next day); none if not a span. */
std::optional<std::pair<double, double>> parseHours(const std::string& text) {
  static const std::regex re(R"(^([\d:.]+)\s*(?:-|–)\s*([\d:.]+)$)");
  const std::string t = trim(text);
  std::smatch m;
  if (!std::regex_match(t, m, re)) return std::nullopt;
  const std::optional<double> a = parseClock(m[1].str());
  const std::optional<double> b = parseClock(m[2].str());
  if (!a || !b || *a >= DAY || *b == *a) return std::nullopt;
  // (an end before the beginning is on the next day; 43:00 is 19:00 of the next day as well)
  return std::pair<double, double>{*a, *b > *a ? *b : *b + DAY};
}

/** Is day `day` (a holiday or not) one of `days`?  A public holiday counts as itself when PH is named, as its weekday otherwise. */
bool onDay(int days, int day, bool holiday) {
  if (holiday && (days & PH) != 0) return true;
  return (days & (1 << day)) != 0;
}

const char* const DE_DAYS[] = {"So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"};

/** A day set as the panels write it: "Mo–Fr", "Di, Do, Fr", "Sa, So", "werktags" … ('' for every day). */
std::string daysText(int days, int off) {
  const int week = days & ALL_DAYS;
  if (week == 0x7e && (off & PH) != 0) return "werktags";
  if (week == ALL_DAYS && (days & PH) == 0) return "";
  std::vector<std::string> parts;
  // runs of days, Monday first
  const int order[] = {1, 2, 3, 4, 5, 6, 0};
  int i = 0;
  while (i < 7) {
    if ((week & (1 << order[i])) == 0) {
      i++;
      continue;
    }
    int j = i;
    while (j + 1 < 7 && (week & (1 << order[j + 1])) != 0) j++;
    if (j - i >= 2) parts.push_back(std::string(DE_DAYS[order[i]]) + "–" + DE_DAYS[order[j]]);
    else
      for (int k = i; k <= j; k++) parts.push_back(DE_DAYS[order[k]]);
    i = j + 1;
  }
  if (days & PH) parts.push_back("feiertags");
  return join(parts, ", ");
}

/** 450 → "7.30", 1320 → "22" */
std::string clockText(double min) {
  int h = static_cast<int>(std::floor(min / 60)) % 24;
  if (h == 0) h = min >= 24 * 60 ? 24 : 0;
  const double m = jsRound(std::fmod(min, 60));
  return m == 0 ? std::to_string(h) : std::to_string(h) + "." + padStart(jsStr(m), 2, '0');
}

/** non-empty parts joined (`[a, b].filter(Boolean).join(sep)`) */
std::string joinFilled(const std::vector<std::string>& parts, const std::string& sep) {
  std::vector<std::string> kept;
  for (const std::string& p : parts)
    if (!p.empty()) kept.push_back(p);
  return join(kept, sep);
}

struct Group {
  int days = 0;
  std::vector<std::string> hours;
};

Group& groupOf(std::vector<Group>& groups, int days) {
  for (Group& g : groups)
    if (g.days == days) return g;
  groups.push_back(Group{days, {}});
  return groups.back();
}

}  // namespace

SignTime signTimeOf(double ms) {
  const BerlinClock c = berlinClock(ms);
  const DateFields before = utcFields(utcMs(c.y, c.m - 1, c.d) - 86400000);
  SignTime t;
  t.day = c.weekday;
  t.min = c.s / 60;
  t.holiday = isHoliday(c.y, c.m, c.d);
  t.holidayBefore = isHoliday(before.year, before.month + 1, before.date);
  return t;
}

std::optional<TimeWindow> parseTimeWindow(const std::string& raw) {
  static const std::regex werktags(R"(\bwerktags\b)", std::regex::icase);
  static const std::regex dayItem(R"(^((?:Mo|Tu|We|Th|Fr|Sa|Su|PH|So|Di|Mi|Do)(?:\s*-\s*(?:Mo|Tu|We|Th|Fr|Sa|Su|So|Di|Mi|Do))?)\b\s*(.*)$)",
                                  std::regex::icase);
  static const std::regex colon(R"(^:\s*)");
  static const std::regex wd(R"(\bWD\b)");
  static const std::regex offWord(R"(^off$)", std::regex::icase);
  std::string text = trim(raw);
  while (startsWith(text, "(") && endsWith(text, ")")) text = trim(text.substr(1, text.size() - 2));
  if (text.empty()) return std::nullopt;
  // "werktags": Monday to Saturday, not on public holidays
  text = std::regex_replace(text, werktags, "Mo-Sa WD");
  TimeWindow w;
  // Rules are parted by ";" – or by a "," after which a weekday follows hours ("Mo-Fr 07:00-17:00, Sa 08:00-12:00");
  // a "," between weekdays ("Tu,Fr 16:00-18:00") or between hours ("06:00-10:00, 19:00-21:00") stays in its rule.
  struct Rule {
    std::vector<std::string> days;
    std::vector<std::string> rest;
  };
  std::vector<Rule> rules;
  for (const std::string& part : split(text, ";")) {
    int cur = -1;
    for (const std::string& item : split(part, ",")) {
      const std::string t = trim(item);
      if (t.empty()) continue;
      std::smatch m;
      if (std::regex_match(t, m, dayItem)) {
        if (cur < 0 || !rules[cur].rest.empty()) {
          rules.push_back(Rule{});
          cur = static_cast<int>(rules.size()) - 1;
        }
        rules[cur].days.push_back(m[1].str());
        // ("Mo-Fr: 07:00-17:00")
        const std::string rest = trim(std::regex_replace(m[2].str(), colon, "", std::regex_constants::format_first_only));
        if (!rest.empty()) rules[cur].rest.push_back(rest);
      } else {
        if (cur < 0) {
          rules.push_back(Rule{});
          cur = static_cast<int>(rules.size()) - 1;
        }
        rules[cur].rest.push_back(t);
      }
    }
  }
  for (const Rule& r : rules) {
    int days = ALL_DAYS;
    if (!r.days.empty()) {
      const std::optional<int> d = parseDays(join(r.days, ","));
      if (!d) return std::nullopt;
      days = *d;
    }
    bool workdays = false;
    bool off = false;
    std::vector<std::string> hours;
    for (std::string h : r.rest) {
      if (std::regex_search(h, wd)) {
        workdays = true;
        h = trim(std::regex_replace(h, wd, "", std::regex_constants::format_first_only));
      }
      if (std::regex_match(h, offWord)) {
        off = true;
        continue;
      }
      if (!h.empty()) hours.push_back(h);
    }
    if (off) {
      if (!hours.empty()) return std::nullopt;
      w.off |= days;
      continue;
    }
    if (workdays) w.off |= PH;
    if (hours.empty()) {
      w.spans.push_back(TimeSpan{days, 0, DAY});
      continue;
    }
    for (const std::string& h : hours) {
      const auto span = parseHours(h);
      if (!span) return std::nullopt;
      w.spans.push_back(TimeSpan{days, span->first, span->second});
    }
  }
  if (w.spans.empty()) return std::nullopt;
  return w;
}

bool holds(const TimeWindow& w, const SignTime& t) {
  const int yesterday = (t.day + 6) % 7;
  for (const TimeSpan& s : w.spans) {
    // begun today – or begun yesterday and running over midnight into this morning
    if (t.min >= s.from && t.min < s.to && onDay(s.days, t.day, t.holiday) && !onDay(w.off, t.day, t.holiday)) return true;
    if (s.to > DAY && t.min < s.to - DAY && onDay(s.days, yesterday, t.holidayBefore) && !onDay(w.off, yesterday, t.holidayBefore)) return true;
  }
  return false;
}

std::vector<std::string> windowLines(const TimeWindow& w) {
  std::vector<Group> groups;
  for (const TimeSpan& s : w.spans) {
    const std::string hours = s.from == 0 && s.to == DAY ? "" : clockText(s.from) + "–" + clockText(s.to > DAY ? s.to - DAY : s.to) + " h";
    Group& g = groupOf(groups, s.days);
    if (!hours.empty() && std::find(g.hours.begin(), g.hours.end(), hours) == g.hours.end()) g.hours.push_back(hours);
  }
  if (groups.size() == 1) {
    const std::string d = daysText(groups[0].days, w.off);
    const std::string h = join(groups[0].hours, ", ");
    std::vector<std::string> out;
    if (!d.empty()) out.push_back(d);
    if (!h.empty()) out.push_back(h);
    return out;
  }
  std::vector<std::string> out;
  for (const Group& g : groups) out.push_back(joinFilled({daysText(g.days, w.off), join(g.hours, ", ")}, " "));
  return out;
}

std::string windowText(const TimeWindow& w) { return join(windowLines(w), " "); }

bool hasDays(const TimeWindow& w) {
  for (const TimeSpan& s : w.spans)
    if ((s.days & ALL_DAYS) != ALL_DAYS || (s.days & PH) != 0) return true;
  return w.off != 0;
}

std::string panelCodeFor(const TimeWindow& w) { return hasDays(w) ? "1042-33" : "1040-30"; }

std::string windowSpec(const TimeWindow& w) {
  static const char* const en[] = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};
  const auto hm = [](double m) {
    return padStart(jsStr(std::floor(m / 60)), 2, '0') + ":" + padStart(jsStr(std::fmod(m, 60)), 2, '0');
  };
  const auto dayList = [](int days) {
    const int week = days & ALL_DAYS;
    std::vector<std::string> parts;
    if (week != ALL_DAYS || (days & PH) != 0) {
      const int order[] = {1, 2, 3, 4, 5, 6, 0};
      int i = 0;
      while (i < 7) {
        if ((week & (1 << order[i])) == 0) {
          i++;
          continue;
        }
        int j = i;
        while (j + 1 < 7 && (week & (1 << order[j + 1])) != 0) j++;
        parts.push_back(j > i ? std::string(en[order[i]]) + "-" + en[order[j]] : std::string(en[order[i]]));
        i = j + 1;
      }
      if (days & PH) parts.push_back("PH");
    }
    return join(parts, ",");
  };
  std::vector<Group> groups;
  for (const TimeSpan& s : w.spans) {
    Group& g = groupOf(groups, s.days);
    // (an end on the next day as a time of that day where that is clear – "22:00-06:00" –, else beyond 24:00)
    const double end = s.to > DAY && s.to - DAY <= s.from ? s.to - DAY : s.to;
    if (!(s.from == 0 && s.to == DAY)) g.hours.push_back(hm(s.from) + "-" + hm(end));
  }
  std::vector<std::string> rules;
  for (const Group& g : groups) rules.push_back(joinFilled({dayList(g.days), join(g.hours, ",")}, " "));
  if (w.off) rules.push_back(dayList(w.off) + " off");
  return join(rules, "; ");
}

std::vector<Conditional> parseConditional(const std::string& tag) {
  static const std::regex bracket(R"(^\(([\s\S]*)\)$)");
  static const std::regex wetWord(R"(^wet$)", std::regex::icase);
  std::vector<Conditional> out;
  // the alternatives are parted by ";" outside brackets
  std::vector<std::string> parts;
  int depth = 0;
  std::string cur;
  for (const char ch : tag) {
    if (ch == '(') depth++;
    else if (ch == ')') depth = std::max(0, depth - 1);
    if (ch == ';' && depth == 0) {
      parts.push_back(cur);
      cur.clear();
    } else cur += ch;
  }
  parts.push_back(cur);
  for (const std::string& p : parts) {
    const size_t at = p.find('@');
    if (at == std::string::npos) continue;
    const std::string value = trim(p.substr(0, at));
    const std::string cond = trim(std::regex_replace(trim(p.substr(at + 1)), bracket, "$1"));
    if (value.empty() || cond.empty()) continue;
    if (std::regex_match(cond, wetWord)) {
      out.push_back(Conditional{value, {}, true});
      continue;
    }
    const std::optional<TimeWindow> w = parseTimeWindow(cond);
    if (w) out.push_back(Conditional{value, *w, false});
  }
  return out;
}

}  // namespace fs
