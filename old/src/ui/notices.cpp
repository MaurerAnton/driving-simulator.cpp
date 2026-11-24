// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Notices of the instructor (faults, hints), one at a time in the street-name
 * block: the gravest current one wins, ties go to the newest.
 *
 * Origin: src/ui/notices.ts (MaurerAnton/driving-simulator @ 1c08919, v0.5.0).
 */
#include "ui/notices.h"

#include <algorithm>

namespace fs {

namespace {
int rank(NoticeSeverity s) {
  switch (s) {
    case NoticeSeverity::Fail: return 3;
    case NoticeSeverity::Major: return 2;
    case NoticeSeverity::Minor: return 1;
    case NoticeSeverity::Hint: break;
  }
  return 0;
}
}  // namespace

const char* noticeColour(NoticeSeverity s) { return rank(s) >= 2 ? "red" : "yellow"; }

void NoticeBoard::add(const std::string& title, const std::string& why, const std::string& meta,
                      NoticeSeverity severity, double seconds, double now) {
  for (size_t i = 0; i < list_.size(); i++)
    if (list_[i].title == title) {
      list_.erase(list_.begin() + static_cast<long>(i));
      break;
    }
  Notice n;
  n.title = title;
  n.why = why;
  n.meta = meta;
  n.severity = severity;
  n.at = now;
  n.until = now + seconds * 1000;
  list_.push_back(n);
}

const Notice* NoticeBoard::current(double now) {
  for (size_t i = list_.size(); i-- > 0;)
    if (list_[i].until <= now) list_.erase(list_.begin() + static_cast<long>(i));
  const Notice* best = nullptr;
  for (const Notice& n : list_) {
    if (!best || rank(n.severity) > rank(best->severity) ||
        (rank(n.severity) == rank(best->severity) && n.at >= best->at))
      best = &n;
  }
  return best;
}

}  // namespace fs
