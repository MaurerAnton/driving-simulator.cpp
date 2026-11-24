// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/notices.ts @ v1.7.7
/**
 * Notices of the instructor (faults, hints).  They are shown one at a time in
 * the block that normally carries the street name: of all that are current,
 * the gravest – and among equally grave ones the newest.
 */
#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace fs {

/** 'fail' | 'major' | 'minor' | 'hint' */
using NoticeSeverity = std::string;

struct Notice {
  std::string title;
  std::string why;
  /** severity and rule, e.g. "schwerer Fehler · § 37 StVO" */
  std::string meta;
  NoticeSeverity severity;
  /** milliseconds (any clock, as long as it is the same one) */
  double at = 0;
  double until = 0;
};

/** Serious faults are red, the rest yellow ('red' | 'yellow'). */
std::string noticeColour(const Notice& n);

class NoticeBoard {
 public:
  /** `seconds`: how long it stays current. The same notice again replaces the old one. */
  void add(const std::string& title, const std::string& why, const std::string& meta, const NoticeSeverity& severity, double seconds, double now);

  /**
   * The one to show at `now`, or null.  (A notice is one object from when it is added until
   * it runs out, as in the original: whoever shows it tells by the pointer whether it is still
   * the same one – and holds it meanwhile, so that no later notice can be mistaken for it.)
   */
  std::shared_ptr<const Notice> current(double now);

  void clear();

  size_t count() const { return list.size(); }

 private:
  std::vector<std::shared_ptr<Notice>> list;
};

}  // namespace fs
