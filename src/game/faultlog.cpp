// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The log of driving faults (origin: src/game/faultlog.ts @ ff37c5d, v0.7.0).
 * The entries are written out as JSON like the original's JSON.stringify.
 */
#include "game/faultlog.h"

#include <cmath>
#include <cstdlib>
#include <cstdio>

#include "platform/json.h"

namespace fs {

namespace {
const char* kKey = "fahrsim-baunatal.faults.v1";

const char* severityName(Severity s) {
  switch (s) {
    case Severity::Fail: return "fail";
    case Severity::Major: return "major";
    case Severity::Minor: return "minor";
    case Severity::Hint: break;
  }
  return "hint";
}

Severity severityFrom(const std::string& s) {
  if (s == "fail") return Severity::Fail;
  if (s == "major") return Severity::Major;
  if (s == "minor") return Severity::Minor;
  return Severity::Hint;
}

bool validSeverity(const std::string& s) {
  return s == "hint" || s == "minor" || s == "major" || s == "fail";
}
}  // namespace

FaultLog::FaultLog(KeyValueStore* store) : store_(store) {
  try {
    if (!store_) return;
    const std::string raw = store_->getItem(kKey);
    if (raw.empty()) return;
    Json list;
    if (!Json::parse(raw, list) || !list.isArray()) return;
    for (const Json& e : list.array) {
      if (!e.isObject()) continue;
      const Json& t = e.at("t");
      const Json& id = e.at("id");
      const Json& sev = e.at("severity");
      const Json& data = e.at("data");
      if (!t.isNumber() || !id.isString() || !sev.isString() || !validSeverity(sev.str) ||
          !data.isObject())
        continue;
      FaultEntry f;
      f.t = t.number;
      f.id = id.str;
      f.severity = severityFrom(sev.str);
      for (const auto& kv : data.object)
        f.data.push_back({kv.first, kv.second.asStr()});
      const Json& street = e.at("street");
      const Json& mode = e.at("mode");
      f.street = street.isString() ? street.str : "";
      f.mode = mode.isString() ? mode.str : "free";
      f.x = e.at("x").asNum();
      f.z = e.at("z").asNum();
      if (std::isnan(f.x)) f.x = 0;
      if (std::isnan(f.z)) f.z = 0;
      entries_.push_back(f);
    }
  } catch (...) {
    /* no storage, or something else was stored there: start empty */
  }
}

void FaultLog::add(const FaultEntry& e) {
  entries_.push_back(e);
  if (entries_.size() > FAULT_LOG_MAX)
    entries_.erase(entries_.begin(),
                   entries_.begin() + static_cast<long>(entries_.size() - FAULT_LOG_MAX));
  save();
}

void FaultLog::clear() {
  entries_.clear();
  try {
    if (store_) store_->removeItem(kKey);
  } catch (...) {
    /* ignore */
  }
}

void FaultLog::save() {
  if (!store_) return;
  try {
    // {t, id, severity, data, street, mode, x, z} as the original writes them
    std::string out = "[";
    for (size_t i = 0; i < entries_.size(); i++) {
      const FaultEntry& e = entries_[i];
      if (i) out += ",";
      char t[32];
      std::snprintf(t, sizeof t, "%.0f", e.t);
      out += "{\"t\":" + std::string(t) + ",\"id\":\"" + e.id + "\",\"severity\":\"" +
             severityName(e.severity) + "\",\"data\":{";
      for (size_t k = 0; k < e.data.size(); k++) {
        if (k) out += ",";
        out += "\"" + e.data[k].first + "\":";
        const std::string& v = e.data[k].second;
        // a number is written as a number (the original stores numbers)
        char* end = nullptr;
        std::strtod(v.c_str(), &end);
        out += (end && *end == '\0' && !v.empty()) ? v : ("\"" + v + "\"");
      }
      out += "},\"street\":\"" + e.street + "\",\"mode\":\"" + e.mode + "\",\"x\":" +
             std::to_string(e.x) + ",\"z\":" + std::to_string(e.z) + "}";
    }
    out += "]";
    store_->setItem(kKey, out);
  } catch (...) {
    /* private window or storage full: the log lives as long as the page */
  }
}

}  // namespace fs
