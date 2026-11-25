// SPDX-License-Identifier: AGPL-3.0-only
// LINK: world sim game core platform
// Origin: tests/world/signs-extra.test.ts @ v1.7.7
/**
 * The signs a map was built without (signs.json.gz, tools/build-signs.ts):
 * the times on their plates and when they hold, and the rules the game takes
 * from them by its clock.  (The original's cases of the map build –
 * tools/mapgen/signs.ts and townsigns.ts: sign codes, directions, town signs –
 * belong to the tools that write the file, not to the game.)
 */
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "core/date.h"
#include "sim/signtime.h"
#include "tests/vehicle_harness.h"
#include "world/extrasigns.h"

using namespace fs;
using fs::harness::ok;
using fs::harness::test;
using Lines = std::vector<std::string>;

namespace {

/** a moment: weekday (0 Sunday … 6 Saturday), "hh:mm", holiday today / yesterday */
SignTime at(int day, const std::string& hm, bool holiday = false, bool holidayBefore = false) {
  const auto parts = split(hm, ":");
  return SignTime{day, jsNumber(parts[0]) * 60 + jsNumber(parts[1]), holiday, holidayBefore};
}
TimeWindow win(const std::string& s) {
  const std::optional<TimeWindow> w = parseTimeWindow(s);
  ok(w.has_value(), "\"" + s + "\" is read");
  return *w;
}
const int MON = 1;
const int FRI = 5;
const int SAT = 6;
const int SUN = 0;

void nightTime() {
  test("night-time signs: \"22:00-06:00\" holds from ten in the evening until six in the morning, every day");
  const TimeWindow w = win("22:00-06:00");
  FS_OK(holds(w, at(MON, "21:59")) == false);
  FS_OK(holds(w, at(MON, "22:00")) == true);
  FS_OK(holds(w, at(MON, "23:30")) == true);
  ok(holds(w, at(SUN, "03:00")) == true, "into the morning, the day after");
  FS_OK(holds(w, at(SUN, "06:00")) == false);
  FS_OK(holds(w, at(SAT, "12:00")) == false);
  // as the plates and OpenStreetMap's sign codes write it, too
  FS_OK(win("22-6") == w);
  FS_OK(win("(22:00 - 06:00)") == w);
  FS_OK(windowLines(w) == Lines{"22–6 h"});
}

void schoolTimes() {
  test("school times: \"Mo-Fr 07:00-17:00\" holds on working days only – also on a public holiday that is a Monday");
  const TimeWindow w = win("Mo-Fr 07:00-17:00");
  FS_OK(holds(w, at(MON, "07:00")) == true);
  FS_OK(holds(w, at(FRI, "16:59")) == true);
  FS_OK(holds(w, at(FRI, "17:00")) == false);
  FS_OK(holds(w, at(SAT, "10:00")) == false);
  FS_OK(holds(w, at(SUN, "10:00")) == false);
  ok(holds(w, at(MON, "10:00", true)) == true, "opening_hours: Mo-Fr names weekdays, holidays are not excepted");
  FS_OK(windowLines(w) == (Lines{"Mo–Fr", "7–17 h"}));
  FS_OK(windowText(w) == "Mo–Fr 7–17 h");
  FS_OK(hasDays(w) == true);
  FS_OK(panelCodeFor(w) == "1042-33");
  FS_OK(panelCodeFor(win("22:00-06:00")) == "1040-30");
  // the forms mappers write it in
  for (const char* s : {"Mo-Fr: 07:00-17:00", "Mo-Fr 7:00-17:00", "(Mo-Fr 07:00-17:00)", "mo-fr 07:00-17:00"}) ok(win(s) == w, s);
}

void severalRules() {
  test("several rules, holidays, \"off\", weekdays apart and hours beyond midnight");
  const TimeWindow w = win("Mo-Fr 06:00-10:00, 19:00-21:00; Sa 06:00-10:00");
  FS_OK(holds(w, at(MON, "20:00")) == true);
  FS_OK(holds(w, at(SAT, "09:00")) == true);
  FS_OK(holds(w, at(SAT, "20:00")) == false);
  FS_OK(windowLines(w) == (Lines{"Mo–Fr 6–10 h, 19–21 h", "Sa 6–10 h"}));
  const TimeWindow off = win("Mo-Sa 06:00-12:00; PH off");
  FS_OK(holds(off, at(SAT, "08:00")) == true);
  ok(holds(off, at(SAT, "08:00", true)) == false, "not on a public holiday");
  const TimeWindow sundays = win("Su,PH");
  FS_OK(holds(sundays, at(SUN, "15:00")) == true);
  FS_OK(holds(sundays, at(MON, "15:00", true)) == true);
  FS_OK(holds(sundays, at(MON, "15:00")) == false);
  FS_OK(sundays.spans[0].days == ((1 << 0) | PH));
  const TimeWindow apart = win("Tu,Fr 16:00-43:00; Sa 12:00-18:00");
  FS_OK(holds(apart, at(2, "17:00")) == true);
  ok(holds(apart, at(3, "18:30")) == true, "43:00 is seven in the evening of the next day");
  FS_OK(holds(apart, at(3, "19:30")) == false);
  ok(holds(apart, at(4, "17:00")) == false, "Thursday is not named");
  // the panel of "werktags": Monday to Saturday, not on holidays
  const TimeWindow work = win("werktags 18-19");
  FS_OK(windowLines(work) == (Lines{"werktags", "18–19 h"}));
  FS_OK(holds(work, at(SAT, "18:30")) == true);
  FS_OK(holds(work, at(SAT, "18:30", true)) == false);
  // written back the one way, read again the same
  for (const TimeWindow& x : {w, off, sundays, apart, work, win("22:00-06:00"), win("07:30-16:00")}) ok(win(windowSpec(x)) == x, windowSpec(x));
  FS_OK(windowSpec(win("22-6")) == "22:00-06:00");
  FS_OK(windowLines(win("07:30-16:00")) == Lines{"7.30–16 h"});
}

void notGuessed() {
  test("what is not a plain time is not guessed at");
  for (const char* s : {"May Su[-1] 09:30-18:00", "2026 Mar 21-2026 Jun 14", "sunset-sunrise", "Apr-Jun", "weight>7.5", "destination", "", "Mo-Fr 25:00-26:00"}) {
    ok(!parseTimeWindow(s).has_value(), s);
  }
}

void conditionals() {
  test("conditional tags: the alternatives with a time (or \"wet\") are taken, the others left out");
  {
    const auto c = parseConditional("30 @ (22:00-06:00); 50 @ (06:00-22:00)");
    FS_OK(c.size() == 2 && c[0].value == "30" && !c[0].wet && windowSpec(c[0].when) == "22:00-06:00" && c[1].value == "50" && windowSpec(c[1].when) == "06:00-22:00");
  }
  {
    const auto c = parseConditional("80 @ wet");
    FS_OK(c.size() == 1 && c[0].value == "80" && c[0].wet);
  }
  FS_OK(parseConditional("none @ (destination)").empty());
  FS_OK(parseConditional("30 @ (weight > 7.5 and 22:00-06:00)").empty());
  FS_OK(parseConditional("no @ (2026 Mar 21-2026 Jun 14)").empty());
  {
    const auto c = parseConditional("delivery @ (Mo-Fr 06:00-12:00; Sa 06:00-10:00)");
    FS_OK(c.size() == 1 && c[0].value == "delivery");
  }
}

void gameClock() {
  test("the game's clock as the signs see it: the time in Kassel, summer time and public holidays included");
  // 2026-11-16 21:30 UTC is 22:30 in Kassel (winter time), a Monday
  SignTime t = signTimeOf(utcMs(2026, 10, 16, 21, 30));
  FS_OK(t.day == 1);
  FS_OK(t.min == 22 * 60 + 30);
  FS_OK(t.holiday == false);
  // 2026-10-03 (German Unity Day, a Saturday) 10:00 UTC is 12:00 in Kassel (summer time)
  t = signTimeOf(utcMs(2026, 9, 3, 10, 0));
  FS_OK(t.day == 6);
  FS_OK(t.min == 12 * 60);
  FS_OK(t.holiday == true);
  t = signTimeOf(utcMs(2026, 9, 4, 10, 0));
  FS_OK(t.holidayBefore == true);
}

TimedRuleJson rule(int e, int d, const char* k, std::optional<double> v, const char* w) {
  TimedRuleJson r;
  r.e = e;
  r.d = d;
  r.k = k;
  r.v = v;
  r.w = w;
  return r;
}

void timedRules() {
  test("the rules of signs with a time hold by the game's clock: a limit at night, a road shut, a limit for lorries");
  double now = utcMs(2026, 10, 16, 21, 30);  // Monday 22:30 in Kassel
  TimedRules rules(
      {
          rule(7, 0, "max", 30, "22:00-06:00"),
          rule(8, 1, "max", 30, "Mo-Fr 07:00-17:00"),
          rule(9, 0, "hgvMax", 60, "22:00-06:00"),
          rule(9, 0, "hgvShut", std::nullopt, "22:00-06:00"),
          rule(10, -1, "shut", std::nullopt, "20:00-06:00"),
          rule(11, 0, "max", 80, "wet"),
          rule(12, 0, "max", 30, "May Su[-1] 09:30-18:00"),
      },
      [&] { return now; });
  using L = TimedRules::Limit;
  FS_OK(rules.limit(7, 1, false) == (L{30, "22–6 h"}));
  FS_OK(rules.limit(7, -1, false) == (L{30, "22–6 h"}));
  ok(!rules.limit(8, 1, false), "school hours are over");
  ok(!rules.limit(9, 1, false), "the limit for lorries is not for a car");
  FS_OK(rules.limit(9, 1, true) == (L{60, "22–6 h"}));
  FS_OK(rules.shut(9, 1, false) == "");
  FS_OK(rules.shut(9, 1, true) == "22–6 h");
  ok(rules.shut(10, 1, false) == "", "only against the course of road 10");
  FS_OK(rules.shut(10, -1, false) == "20–6 h");
  ok(!rules.limit(11, 1, false), "dry");
  rules.wet = [] { return true; };
  FS_OK(rules.limit(11, 1, false) == (L{80, "bei Nässe"}));
  ok(!rules.limit(12, 1, false), "a time it cannot read never holds");
  FS_OK(!rules.limit(99, 1, false));
  // the next morning at nine: the night is over, the school's time has begun
  now = utcMs(2026, 10, 17, 8, 0);
  FS_OK(!rules.limit(7, 1, false));
  FS_OK(rules.limit(8, 1, false) == (L{30, "Mo–Fr 7–17 h"}));
  ok(!rules.limit(8, -1, false), "only along the course of road 8");
  ok(rules.roads() == 5, "the rule it cannot read is left out");
}

void posts() {
  test("the posts of signs.json.gz go into their tile: added – or in place of the plates of the post that stands there");
  ExtraSignsJson json;
  FS_OK(jsonParse(R"({"v":1,"map":"x","tiles":{"1_2":[
      {"x":10,"y":0,"z":10,"yaw":0,"s":["274-30","1040-30:22:00-06:00"],"h":2.1,"r":1},
      {"x":50,"y":0,"z":50,"yaw":1,"s":["310:Altenbauna|Stadt Baunatal"],"h":2.1,"big":1}]},"rules":[]})",
                  json));
  const ExtraSigns extra(json);
  std::vector<SignPost> signs{SignPost{10.2, 0, 9.9, 0, {"274-30"}, 2.1, 0}};
  extra.mergeInto("1_2", signs);
  FS_OK(signs.size() == 2);
  ok(signs[0].s == (Lines{"274-30", "1040-30:22:00-06:00"}), "the plate with the time under the speed limit");
  ok(signs[0].x == 10.2, "the post stays where it stands");
  FS_OK(signs[1].s == Lines{"310:Altenbauna|Stadt Baunatal"});
  FS_OK(signs[1].big == 1);
  // a tile loaded again gets them again – as copies
  std::vector<SignPost> again;
  extra.mergeInto("1_2", again);
  ok(again.size() == 2, "without the post to replace, the plates come as a post of their own");
  again[1].s.push_back("x");
  std::vector<SignPost> third;
  extra.mergeInto("1_2", third);
  FS_OK(third[1].s == Lines{"310:Altenbauna|Stadt Baunatal"});
  extra.mergeInto("9_9", third);
  FS_OK(third.size() == 2);
  FS_OK(extra.posts() == 2);
}

}  // namespace

int main() {
  std::printf("signs-extra\n");
  nightTime();
  schoolTimes();
  severalRules();
  notGuessed();
  conditionals();
  gameClock();
  timedRules();
  posts();
  std::printf("ok\n");
  return 0;
}
