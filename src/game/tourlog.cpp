// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/tourlog.ts @ v1.7.7
#include "game/tourlog.h"

#include "core/jsonwrite.h"
#include "platform/json.h"
#include "platform/plat.h"

namespace fs {

namespace {

const char* const KEY = "fahrsim.tours";
/** how many are kept (the newest) */
const size_t KEEP = 40;

/** A tour as it was stored: what is missing, or of another kind than it should be, is what the original reads as nothing. */
TourRecord recordOf(const Json& t) {
  TourRecord r;
  r.date = t.at("date").str;
  const auto num = [&t](const char* name) {
    const Json& v = t.at(name);
    return v.kind == Json::Kind::Number ? v.number : JS_NAN;
  };
  r.planned = num("planned");
  r.driven = num("driven");
  r.fresh = num("fresh");
  const Json& done = t.at("done");
  r.done = done.kind == Json::Kind::Bool && done.boolean;
  for (const Json& v : t.at("line").array) r.line.push_back(v.kind == Json::Kind::Number ? v.number : JS_NAN);
  const Json& p = t.at("prefs");
  const auto flag = [&p](const char* name, bool& out) {
    const Json& v = p.at(name);
    if (v.kind == Json::Kind::Bool) out = v.boolean;
  };
  flag("noMotorway", r.prefs.noMotorway);
  flag("noExpress", r.prefs.noExpress);
  flag("noMain", r.prefs.noMain);
  if (p.at("mostly").kind == Json::Kind::String) r.prefs.mostly = p.at("mostly").str;
  return r;
}

void write(JsonOut& o, const TourRecord& t) {
  o.beginObject();
  o.key("date");
  o.string(t.date);
  o.key("planned");
  o.number(t.planned);
  o.key("driven");
  o.number(t.driven);
  o.key("fresh");
  o.number(t.fresh);
  o.key("done");
  o.boolean(t.done);
  o.key("line");
  o.beginArray();
  for (double v : t.line) o.number(v);
  o.endArray();
  o.key("prefs");
  o.beginObject();
  o.key("noMotorway");
  o.boolean(t.prefs.noMotorway);
  o.key("noExpress");
  o.boolean(t.prefs.noExpress);
  o.key("noMain");
  o.boolean(t.prefs.noMain);
  o.key("mostly");
  o.string(t.prefs.mostly);
  o.endObject();
  o.endObject();
}

}  // namespace

std::vector<TourRecord> loadTours() {
  std::vector<TourRecord> out;
  const std::optional<std::string> raw = plat::localStorage().getItem(KEY);
  if (!raw || raw->empty()) return out;
  Json list;
  if (!Json::parse(*raw, list) || !list.isArray()) return out;
  for (const Json& t : list.array) {
    if (t.isObject() && t.at("date").isString() && t.at("line").isArray()) out.push_back(recordOf(t));
  }
  return out;
}

void saveTour(const TourRecord& t) {
  std::vector<TourRecord> list = loadTours();
  list.insert(list.begin(), t);
  if (list.size() > KEEP) list.resize(KEEP);
  JsonOut o;
  o.beginArray();
  for (const TourRecord& r : list) write(o, r);
  o.endArray();
  // (private browsing, a full storage: the tour is not kept)
  plat::localStorage().setItem(KEY, o.text());
}

}  // namespace fs
