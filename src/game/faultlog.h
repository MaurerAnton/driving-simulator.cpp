// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/faultlog.ts @ v1.7.7
/**
 * The log of driving faults: every fault of every drive, kept in the browser
 * (localStorage) so that it is still there in the next session.  The player
 * can read it at any time and open the rule each fault refers to.
 */
#pragma once

#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "platform/plat.h"

namespace fs {

/** A value for a fault's title: a number or a text. */
using FaultValue = std::variant<double, std::string>;

struct FaultEntry {
  /** when it happened (ms since 1970) */
  double t = 0;
  /** catalogue key of the fault, see src/i18n/violations.ts */
  std::string id;
  /** (sim/rules.h Severity: 'hint' | 'minor' | 'major' | 'fail') */
  std::string severity;
  /** values for the title, e.g. { limit: 30, speed: 47 } – in the order they were given */
  std::vector<std::pair<std::string, FaultValue>> data;
  /** the street it happened in ('' if unknown) */
  std::string street;
  /** 'free', 'exam' or the id of the lesson */
  std::string mode;
  double x = 0;
  double z = 0;

  bool operator==(const FaultEntry&) const = default;
};

/** What is needed of localStorage (tests bring their own). */
using KeyValueStore = plat::Storage;

/** older entries make room for new ones */
inline constexpr size_t FAULT_LOG_MAX = 400;

class FaultLog {
 public:
  /** oldest first */
  std::vector<FaultEntry> entries;

  /** (`store`: where it is kept – null: nowhere, the log lives as long as the page) */
  explicit FaultLog(KeyValueStore* store = &plat::localStorage());

  void add(const FaultEntry& e);

  void clear();

 private:
  KeyValueStore* store;

  void save();
};

}  // namespace fs
