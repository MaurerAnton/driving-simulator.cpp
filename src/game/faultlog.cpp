// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/faultlog.ts @ v1.7.7
#include "game/faultlog.h"

#include "core/js.h"
#include "core/jsonwrite.h"
#include "platform/json.h"

namespace fs {

namespace {

const char* const KEY = "fahrsim-baunatal.faults.v1";

bool severityWord(const std::string& s) { return s == "hint" || s == "minor" || s == "major" || s == "fail"; }

/** `typeof v === 'object' && v !== null` */
bool isObjectLike(const Json& v) { return v.isObject() || v.isArray(); }

bool valid(const Json& e) {
  if (!isObjectLike(e)) return false;
  return e.at("t").isNumber() && e.at("id").isString() && e.at("severity").isString() && severityWord(e.at("severity").str) && isObjectLike(e.at("data"));
}

/** `Number(v) || 0` of a member that may be anything (or not there) */
double numberOr0(const Json& v) {
  double n = JS_NAN;
  if (v.isNumber()) n = v.number;
  else if (v.isString()) n = jsNumber(v.str);
  else if (v.kind == Json::Kind::Bool) n = v.boolean ? 1 : 0;
  return n == n ? n : 0;
}

}  // namespace

FaultLog::FaultLog(KeyValueStore* storeIn) : store(storeIn) {
  // (no storage, or something else was stored there: start empty)
  if (!store) return;
  const std::optional<std::string> raw = store->getItem(KEY);
  if (!raw || raw->empty()) return;
  Json list;
  if (!Json::parse(*raw, list) || !list.isArray()) return;
  for (const Json& e : list.array) {
    if (!valid(e)) continue;
    FaultEntry f;
    f.t = e.at("t").number;
    f.id = e.at("id").str;
    f.severity = e.at("severity").str;
    if (e.at("data").isObject()) {
      for (const auto& [name, v] : e.at("data").object) {
        if (v.isNumber()) f.data.emplace_back(name, v.number);
        else if (v.isString()) f.data.emplace_back(name, v.str);
      }
    }
    f.street = e.at("street").isString() ? e.at("street").str : "";
    f.mode = e.at("mode").isString() ? e.at("mode").str : "free";
    f.x = numberOr0(e.at("x"));
    f.z = numberOr0(e.at("z"));
    entries.push_back(std::move(f));
  }
}

void FaultLog::add(const FaultEntry& e) {
  entries.push_back(e);
  if (entries.size() > FAULT_LOG_MAX) entries.erase(entries.begin(), entries.begin() + static_cast<std::ptrdiff_t>(entries.size() - FAULT_LOG_MAX));
  save();
}

void FaultLog::clear() {
  entries.clear();
  if (store) store->removeItem(KEY);
}

void FaultLog::save() {
  if (!store) return;
  JsonOut o;
  o.beginArray();
  for (const FaultEntry& e : entries) {
    o.beginObject();
    o.key("t");
    o.number(e.t);
    o.key("id");
    o.string(e.id);
    o.key("severity");
    o.string(e.severity);
    o.key("data");
    o.beginObject();
    for (const auto& [name, v] : e.data) {
      o.key(name);
      if (const double* n = std::get_if<double>(&v)) o.number(*n);
      else o.string(std::get<std::string>(v));
    }
    o.endObject();
    o.key("street");
    o.string(e.street);
    o.key("mode");
    o.string(e.mode);
    o.key("x");
    o.number(e.x);
    o.key("z");
    o.number(e.z);
    o.endObject();
  }
  o.endArray();
  // (private window or storage full: the log lives as long as the page)
  store->setItem(KEY, o.text());
}

}  // namespace fs
