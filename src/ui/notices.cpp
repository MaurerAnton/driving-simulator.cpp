// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/notices.ts @ v1.7.7
#include "ui/notices.h"

namespace fs {

/** RANK: fail 3, major 2, minor 1, hint 0 */
static int rankOf(const NoticeSeverity& severity) {
  return severity == "fail" ? 3 : severity == "major" ? 2 : severity == "minor" ? 1 : 0;
}

std::string noticeColour(const Notice& n) { return rankOf(n.severity) >= 2 ? "red" : "yellow"; }

void NoticeBoard::add(const std::string& title, const std::string& why, const std::string& meta, const NoticeSeverity& severity, double seconds, double now) {
  for (size_t i = 0; i < list.size(); i++) {
    if (list[i]->title == title) {
      list.erase(list.begin() + static_cast<std::ptrdiff_t>(i));
      break;
    }
  }
  list.push_back(std::make_shared<Notice>(Notice{title, why, meta, severity, now, now + seconds * 1000}));
}

std::shared_ptr<const Notice> NoticeBoard::current(double now) {
  for (size_t i = list.size(); i-- > 0;)
    if (list[i]->until <= now) list.erase(list.begin() + static_cast<std::ptrdiff_t>(i));
  std::shared_ptr<const Notice> best;
  for (const auto& n : list) {
    if (!best || rankOf(n->severity) > rankOf(best->severity) || (rankOf(n->severity) == rankOf(best->severity) && n->at >= best->at)) best = n;
  }
  return best;
}

void NoticeBoard::clear() { list.clear(); }

}  // namespace fs
