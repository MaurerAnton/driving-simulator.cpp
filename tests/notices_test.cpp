// SPDX-License-Identifier: AGPL-3.0-only
// LINK: ui i18n core platform
// Origin: tests/game/notices.test.ts @ v1.7.7
/** Which notice of the instructor is shown in the street-name block. */
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

#include "ui/notices.h"

using namespace fs;

static std::string titleAt(NoticeBoard& b, double now) {
  auto n = b.current(now);
  return n ? n->title : std::string("<null>");
}

int main() {
  {
    // of several notices the gravest shows, the newest among equals
    NoticeBoard b;
    assert(b.current(0) == nullptr);
    b.add("Too fast", "why", "minor · § 3", "minor", 8, 1000);
    assert(titleAt(b, 1000) == "Too fast");
    b.add("Hint", "", "", "hint", 5.5, 1500);
    assert(titleAt(b, 1500) == "Too fast");  // a hint does not push a fault aside
    b.add("Red light", "why", "fail · § 37", "fail", 8, 2000);
    assert(titleAt(b, 2000) == "Red light");  // the serious fault takes over
    b.add("No indicator", "", "", "minor", 8, 2500);
    assert(titleAt(b, 2500) == "Red light");  // a later minor one does not hide it
    b.add("Crash", "", "", "fail", 8, 3000);
    assert(titleAt(b, 3000) == "Crash");  // equally grave: the newest
    // as they run out, the next gravest that is still current comes back
    assert(titleAt(b, 10500) == "Crash");
    assert(b.current(11200) == nullptr);  // all over after their time
    assert(b.count() == 0);
  }
  {
    // a notice runs out; the same one again starts afresh instead of piling up
    NoticeBoard b;
    b.add("Too fast", "", "", "minor", 8, 0);
    b.add("Red light", "", "", "major", 2, 0);
    assert(titleAt(b, 1000) == "Red light");
    assert(titleAt(b, 2500) == "Too fast");  // the minor one is still current when the major one has run out
    b.add("Too fast", "", "", "minor", 8, 7000);
    assert(b.count() == 1);
    assert(titleAt(b, 12000) == "Too fast");  // renewed: counts from the second time
    assert(b.current(15001) == nullptr);
    b.add("x", "", "", "hint", 5, 0);
    b.clear();
    assert(b.current(1) == nullptr);
  }
  {
    // red for serious faults, yellow for the rest
    auto n = [](const std::string& severity) { return Notice{"", "", "", severity, 0, 1}; };
    const std::vector<std::string> got = {noticeColour(n("fail")), noticeColour(n("major")), noticeColour(n("minor")), noticeColour(n("hint"))};
    assert((got == std::vector<std::string>{"red", "red", "yellow", "yellow"}));
  }
  std::puts("notices_test: ok");
  return 0;
}
