// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The log of driving faults: every fault of every drive, kept (the browser's
 * localStorage in the original) so that it is still there in the next
 * session.  The player can read it at any time and open the rule each fault
 * refers to.
 *
 * Origin: src/game/faultlog.ts (MaurerAnton/driving-simulator @ ff37c5d, v0.7.0).
 */
#pragma once

#include <string>
#include <vector>

#include "sim/rules.h"

namespace fs {

struct FaultEntry {
  /** when it happened (ms since 1970) */
  double t = 0;
  /** catalogue key of the fault, see src/i18n/violations.ts */
  std::string id;
  Severity severity = Severity::Minor;
  /** values for the title, e.g. { limit: 30, speed: 47 } */
  std::vector<std::pair<std::string, std::string>> data;
  /** the street it happened in ("" if unknown) */
  std::string street;
  /** 'free', 'exam' or the id of the lesson */
  std::string mode = "free";
  double x = 0, z = 0;
};

/**
 * What is needed of a store (the original: localStorage; tests and the native
 * build bring their own or none).  All methods must not throw.
 */
class KeyValueStore {
 public:
  virtual ~KeyValueStore() = default;
  /** "" when the key is not there */
  virtual std::string getItem(const std::string& key) = 0;
  virtual void setItem(const std::string& key, const std::string& value) = 0;
  virtual void removeItem(const std::string& key) = 0;
};

/** older entries make room for new ones */
constexpr size_t FAULT_LOG_MAX = 400;

class FaultLog {
 public:
  /** null: the log lives as long as the page (a browser without storage) */
  explicit FaultLog(KeyValueStore* store = nullptr);

  /** oldest first */
  const std::vector<FaultEntry>& entries() const { return entries_; }
  void add(const FaultEntry& e);
  void clear();

 private:
  std::vector<FaultEntry> entries_;
  KeyValueStore* store_;
  void save();
};

}  // namespace fs
