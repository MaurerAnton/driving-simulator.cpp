// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Notices of the instructor (faults, hints).  They are shown one at a time in
 * the block that normally carries the street name: of all that are current,
 * the gravest – and among equally grave ones the newest.
 *
 * Origin: src/ui/notices.ts (MaurerAnton/driving-simulator @ 1c08919, v0.5.0).
 */
#pragma once

#include <string>
#include <vector>

namespace fs {

enum class NoticeSeverity { Fail, Major, Minor, Hint };

/** Serious faults are red, the rest yellow. */
const char* noticeColour(NoticeSeverity s);

struct Notice {
  std::string title;
  std::string why;
  /** severity and rule, e.g. "schwerer Fehler · § 37 StVO" */
  std::string meta;
  NoticeSeverity severity = NoticeSeverity::Hint;
  /** milliseconds (any clock, as long as it is the same one) */
  double at = 0;
  double until = 0;
};

class NoticeBoard {
 public:
  /** `seconds`: how long it stays current. The same notice again replaces the old one. */
  void add(const std::string& title, const std::string& why, const std::string& meta,
           NoticeSeverity severity, double seconds, double now);

  /** The one to show at `now`, or null. */
  const Notice* current(double now);

  void clear() { list_.clear(); }

  size_t count() const { return list_.size(); }

 private:
  std::vector<Notice> list_;
};

}  // namespace fs
