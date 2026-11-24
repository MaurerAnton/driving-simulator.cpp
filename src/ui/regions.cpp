// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/regions.ts @ v1.7.7
#include "ui/regions.h"

#include <memory>
#include <vector>

#include "platform/json.h"
#include "platform/plat.h"

namespace fs {

namespace {

using dom::El;
using dom::el;

Task<void> thenDraw(Task<std::optional<RegionIndex>> asked, std::function<void(const RegionIndex*)> draw) {
  const std::optional<RegionIndex> index = co_await std::move(asked);
  draw(index ? &*index : nullptr);
}

}  // namespace

Task<std::optional<RegionIndex>> loadRegionIndex(bool fetchRepo, std::string siteBase, FetchFn fetchFn) {
  std::vector<std::string> urls;
  if (fetchRepo) {
    urls.push_back("https://raw.githubusercontent.com/MaurerAnton/driving-simulator/main/map-assets/regions.json");
  }
  urls.push_back(siteBase + "regions.json");
  for (const std::string& url : urls) {
    plat::Request req;
    req.url = url;
    req.cache = "no-cache";
    // (offline, or a repository without regions: try the next)
    const plat::Response res = co_await (fetchFn ? fetchFn(std::move(req)) : plat::fetch(std::move(req)));
    if (!res.ok()) continue;
    Json json;
    if (!Json::parse(res.text(), json)) continue;
    std::optional<RegionIndex> index = parseRegions(json);
    if (index) co_return index;
  }
  co_return std::nullopt;
}

const Region* findRegion(const RegionIndex* index, const std::string& id) {
  if (!index || id.empty()) return nullptr;
  for (const Region& r : index->regions)
    if (r.id == id) return &r;
  return nullptr;
}

dom::El regionSettings(Settings& settings, std::function<std::string(const std::string& key, const TrParams& vars)> t) {
  Settings* s = &settings;
  El box = el("div");
  box.append(el("h2", "", t("region.title", {})));
  El row = el("div", "row");
  El sel = el("select");
  sel.style("maxWidth", "100%");
  row.append(sel);
  El status = el("p", "note");
  El reloadRow = el("div", "row");
  El reload = el("button", "btn chip", t("region.reload", {}));
  reload.onclick([] { plat::reload(); });
  reloadRow.append(reload);
  reloadRow.hidden(true);
  El help = el("p", "note");
  help.textContent(t("region.help", {}));
  box.append(row, status, reloadRow, help);

  /** the region loaded now (the page was loaded with it) */
  const std::string inUse = s->region;

  std::function<void(const RegionIndex*)> draw = [s, t, sel, status, reloadRow, inUse](const RegionIndex* index) {
    sel.replaceChildren();
    El site = el("option", "", t("region.site", {}));
    site.value("");
    sel.append(site);
    if (index)
      for (const Region& r : index->regions) {
        El o = el("option", "", r.name);
        o.value(r.id);
        sel.append(o);
      }
    // a region chosen that the index no longer names: back to the site's map
    if (!s->region.empty() && !findRegion(index, s->region)) s->region = "";
    sel.value(s->region);
    if (!index) status.textContent(t("region.none", {}));
    else if (s->region.empty()) status.textContent(t("region.siteNote", {}));
    else {
      const Region* r = findRegion(index, s->region);
      status.textContent(t("region.note", {{"name", r ? r->name : s->region}}));
    }
    reloadRow.hidden(s->region == inUse);
  };

  sel.onchange([s, t, sel, status, reloadRow, inUse] {
    s->region = sel.value();
    saveSettings(*s);
    reloadRow.hidden(s->region == inUse);
    status.textContent(!s->region.empty() ? t("region.note", {{"name", s->region}}) : t("region.siteNote", {}));
  });

  thenDraw(loadRegionIndex(true, "map-assets/"), draw).detach();
  return box;
}

}  // namespace fs
