// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/stopboards.ts @ v1.7.7
#include "sim/stopboards.h"

#include <algorithm>
#include <cmath>

#include "core/js.h"
#include "world/types.h"

namespace fs {

bool servedAt(const Timetable& tt, const BoardPlace& place) {
  return !tt.placeCalls.empty() ? tt.placeCalls.count(place.id) > 0 : tt.calls.count(place.name) > 0;
}

std::optional<std::vector<BoardRow>> boardRows(const Timetable& tt, const BoardPlace& place, const ServiceTime& at, int n, double horizon) {
  if (!servedAt(tt, place)) return std::nullopt;
  const std::vector<Departure> list = !tt.placeCalls.empty() ? departures(tt, StopKey(place.id), at, n, horizon) : departures(tt, StopKey(place.name), at, n, horizon);
  std::vector<BoardRow> out;
  for (const Departure& d : list) {
    const int m = static_cast<int>(std::floor(d.wait / 60));
    BoardRow r;
    r.line = d.ref;
    r.destination = d.to;
    if (m < 60) r.minutes = std::max(0, m);
    else r.at = clockText(d.time);
    r.kind = d.kind;
    r.colour = d.colour;
    out.push_back(std::move(r));
  }
  return out;
}

std::string clockText(double s) {
  const int64_t m = static_cast<int64_t>(std::floor(s / 60));
  const int64_t hm = static_cast<int64_t>(std::floor(static_cast<double>(m) / 60));
  const std::string h = padStart(std::to_string(((hm % 24) + 24) % 24), 2, '0');
  return h + ":" + padStart(std::to_string(((m % 60) + 60) % 60), 2, '0');
}

DisplaySize displayAt(int poleFlags, int placeKind, bool served) {
  if (poleFlags & STOP_FLAG_BOARD) return "board";
  if (!served) return "";
  if ((poleFlags & STOP_FLAG_SHELTER) || (placeKind & (STOP_FLAG_TRAM | STOP_FLAG_RAIL))) return "board";
  return "small";
}

}  // namespace fs
