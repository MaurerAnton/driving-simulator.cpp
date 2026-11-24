// SPDX-License-Identifier: AGPL-3.0-only
/** Which notice of the instructor is shown in the street-name block. */
#include "../src/ui/notices.h"
#include <cassert>
#include <cstdio>

using namespace fs;

int main() {
  // of several notices the gravest shows, the newest among equals
  {
    NoticeBoard b;
    assert(b.current(0) == nullptr);
    b.add("Too fast", "why", "minor · § 3", NoticeSeverity::Minor, 8, 1000);
    assert(b.current(1000) && b.current(1000)->title == "Too fast");
    b.add("Hint", "", "", NoticeSeverity::Hint, 5.5, 1500);
    assert(b.current(1500) && b.current(1500)->title == "Too fast");  // a hint does not push a fault aside
    b.add("Red light", "why", "fail · § 37", NoticeSeverity::Fail, 8, 2000);
    assert(b.current(2000) && b.current(2000)->title == "Red light");  // the serious fault takes over
    b.add("No indicator", "", "", NoticeSeverity::Minor, 8, 2500);
    assert(b.current(2500) && b.current(2500)->title == "Red light");  // a later minor one does not hide it
    b.add("Crash", "", "", NoticeSeverity::Fail, 8, 3000);
    assert(b.current(3000) && b.current(3000)->title == "Crash");  // equally grave: the newest
    // as they run out, the next gravest that is still current comes back
    assert(b.current(10500) && b.current(10500)->title == "Crash");
    assert(b.current(11200) == nullptr);  // all over after their time
    assert(b.count() == 0);
  }

  // a notice runs out; the same one again starts afresh instead of piling up
  {
    NoticeBoard b;
    b.add("Too fast", "", "", NoticeSeverity::Minor, 8, 0);
    b.add("Red light", "", "", NoticeSeverity::Major, 2, 0);
    assert(b.current(1000) && b.current(1000)->title == "Red light");
    assert(b.current(2500) && b.current(2500)->title == "Too fast");  // the minor one is still current
    b.add("Too fast", "", "", NoticeSeverity::Minor, 8, 7000);
    assert(b.count() == 1);
    assert(b.current(12000) && b.current(12000)->title == "Too fast");  // renewed: counts from the second time
    assert(b.current(15001) == nullptr);
    b.add("x", "", "", NoticeSeverity::Hint, 5, 0);
    b.clear();
    assert(b.current(1) == nullptr);
  }

  // red for serious faults, yellow for the rest
  auto colour = [](NoticeSeverity s) { return std::string(noticeColour(s)); };
  assert(colour(NoticeSeverity::Fail) == "red");
  assert(colour(NoticeSeverity::Major) == "red");
  assert(colour(NoticeSeverity::Minor) == "yellow");
  assert(colour(NoticeSeverity::Hint) == "yellow");

  std::printf("notices: all checks passed\n");
  return 0;
}
