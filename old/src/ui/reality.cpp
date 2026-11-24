// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The reality view: real photographs of where the car is.
 *
 * Origin: src/ui/reality.ts (MaurerAnton/driving-simulator @ 578e397, v0.4.0).
 * The panel structure, the source rows, the ranking and the caption follow the
 * original; the DOM calls become HudNode widgets and the HTTP goes through
 * HttpBackend.
 */
#include "ui/reality.h"

#include <chrono>

#include "i18n/index.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace fs {

namespace {
const ImageSource REMOTE[3] = {ImageSource::Panoramax, ImageSource::KartaView,
                               ImageSource::Mapillary};
const char* const REMOTE_NAME[3] = {"panoramax", "kartaview", "mapillary"};

std::unique_ptr<HudNode> node(const std::string& tag, const std::string& cls = "",
                              const std::string& text = "") {
  auto n = std::make_unique<HudNode>();
  n->tag = tag;
  n->cls = cls;
  n->text = text;
  return n;
}
}  // namespace

RealityPanel::RealityPanel(HudHost& host, RealityHost& realityHost, HttpBackend* http)
    : host_(host), game_(realityHost), http_(http) {}

std::string RealityPanel::t(const std::string& key) const {
  return fs::tr(key, game_.settings().lang);
}

std::string RealityPanel::t(const std::string& key,
                            const std::vector<std::pair<std::string, std::string>>& p) const {
  return fs::tr(key, game_.settings().lang, p);
}

void RealityPanel::build() {
  auto o = node("div", "overlay reality");
  o->id = "reality";

  auto head = node("header", "rv-head");
  auto title = node("div", "rv-title");
  title->add(node("b", "", t("reality")));
  title->add(node("span", "", t("realitySub")));
  char coords[128];
  std::snprintf(coords, sizeof coords, "%s%.5f, %.5f",
                place_.name.empty() ? "" : (place_.name + " \u00b7 ").c_str(), place_.lat,
                place_.lon);
  auto where = node("div", "rv-where", coords);
  auto closeBtn = node("button", "btn", t("realityClose") + " (Esc)");
  closeBtn->click = [this]() { game_.close(); };
  head->add(std::move(title));
  head->add(std::move(where));
  head->add(std::move(closeBtn));

  auto view = node("div", "rv-view");
  view->id = "rvView";
  view->add(node("img", "rv-photo"));
  view->add(node("div", "rv-msg"));
  view->add(node("div", "rv-cap"));

  auto side = node("aside", "rv-side");
  side->add(node("h2", "", t("realityPlace")));
  std::string coordsText;
  {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.6f, %.6f", place_.lat, place_.lon);
    coordsText = buf;
  }
  auto coordRow = node("div", "rv-coords");
  coordRow->add(node("code", "", coordsText));
  auto copy = node("button", "btn ghost", t("realityCopy"));
  copyBtn_ = copy.get();
  copy->click = [this]() {
    // (the clipboard needs a secure page and the player's click; the button
    // says what happened either way – and the plain word again a moment later)
    copied_ = true;
    copiedAt_ = std::chrono::duration<double, std::milli>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count();
    if (copyBtn_) copyBtn_->text = t("realityCopied");
  };
  coordRow->add(std::move(copy));
  side->add(std::move(coordRow));

  auto links = node("div", "rv-links");
  for (const auto& l : placeLinks(place_.lat, place_.lon, place_.heading)) {
    auto a = node("a", "", l.label + " \xe2\x86\x97");
    a->html = "<a href=\"" + l.url + "\" target=\"_blank\" rel=\"noopener noreferrer\">" +
              l.label + " \xe2\x86\x97</a>";
    links->add(std::move(a));
  }
  side->add(std::move(links));
  auto aerialRow = node("div", "buttons");
  auto aerial = node("button", "btn ghost", t("realityAerial"));
  aerial->click = [this]() { game_.openAerial(); };
  aerialRow->add(std::move(aerial));
  side->add(std::move(aerialRow));

  side->add(node("h2", "", t("realitySources")));
  for (ImageSource s : REMOTE) {
    auto row = node("div", "rv-source");
    auto label = node("label");
    std::string name = sourceName(s);
    label->add(node("b", "", name));
    label->add(node("small", "", t("realityFree")));
    label->add(node("em", "", ""));
    row->add(std::move(label));
    side->add(std::move(row));
  }
  side->add(node("p", "rv-note", t("realityPrivacy")));

  side->add(node("h2", "", t("realityOwn")));
  side->add(node("p", "rv-note", t("realityOwnText")));
  auto pick = node("div", "buttons");
  pick->add(node("button", "btn ghost", t("realityPickFiles")));
  pick->add(node("button", "btn ghost", t("realityPickFolder")));
  side->add(std::move(pick));
  side->add(node("p", "rv-note", note_));

  side->add(node("h2", "", t("realityNearby")));
  side->add(node("div", "rv-thumbs"));

  auto main = node("div", "rv-main");
  main->add(std::move(view));
  main->add(std::move(side));
  o->add(std::move(head));
  o->add(std::move(main));
  root_ = std::move(o);
}

const std::vector<StreetImage>& RealityPanel::all() const {
  static std::vector<StreetImage> out;
  out = own_;
  for (ImageSource s : REMOTE) {
    auto it = found_.find(s);
    if (it != found_.end()) out.insert(out.end(), it->second.begin(), it->second.end());
  }
  return out;
}

void RealityPanel::tick() {
  if (!copied_ || !copyBtn_) return;
  double now = std::chrono::duration<double, std::milli>(
                   std::chrono::system_clock::now().time_since_epoch())
                   .count();
  if (now - copiedAt_ < 1600) return;
  copied_ = false;
  copyBtn_->text = t("realityCopy");
}

void RealityPanel::open(const RealityPlace& place) {
  close();
  place_ = place;
  found_.clear();
  status_.clear();
  current_ = nullptr;
  radius_ = NEAR_RADIUS;
  note_.clear();
  picked_ = false;
  openedAt_ = 0;
  build();
  host_.setRoot(root_.get());
  for (ImageSource s : REMOTE) {
    const Settings::Reality& r = game_.settings().reality;
    bool on = (s == ImageSource::Panoramax && r.panoramax) ||
              (s == ImageSource::KartaView && r.kartaview) ||
              (s == ImageSource::Mapillary && r.mapillary);
    if (on) search(s);
  }
  refresh();
}

void RealityPanel::close() {
  pano_.close();
  root_.reset();
  current_ = nullptr;
}

void RealityPanel::render(WebGLRenderer& renderer, double left, double top, double width,
                          double height) {
  if (!root_ || !pano_.active()) return;
  pano_.render(renderer, left, top, width, height);
}

void RealityPanel::importFiles(
    const std::vector<std::pair<std::string, std::vector<uint8_t>>>& files) {
  std::vector<ListEntry> lists;
  for (const auto& f : files) {
    if (hasExt(f.first, LIST_FILE) && f.second.size() < 30000000)
      for (const auto& e : parseList(std::string(f.second.begin(), f.second.end())))
        lists.push_back(e);
  }
  int n = 0, placed = 0;
  for (const auto& f : files) {
    if (!hasExt(f.first, IMAGE_FILE)) continue;
    if (std::any_of(own_.begin(), own_.end(),
                    [&](const StreetImage& i) { return i.id == f.first; }))
      continue;
    ImageMeta meta = readImageMeta(f.second);
    LocalSource src;
    src.name = f.first;
    src.meta = meta;
    src.url = f.first;
    StreetImage img = describeLocal(src, lists, [this](double la, double lo) {
      return game_.plausible(la, lo);
    });
    own_.push_back(img);
    n++;
    if (img.located) placed++;
  }
  if (n > 0) {
    note_ = t("realityImported", {{"n", std::to_string(n)}, {"p", std::to_string(placed)}});
    current_ = nullptr;
    picked_ = false;
  }
  refresh();
}

void RealityPanel::search(ImageSource source) {
  const Settings::Reality& r = game_.settings().reality;
  if (source == ImageSource::Mapillary && r.mapillaryToken.empty()) {
    status_[source] = t("realityNeedToken");
    found_.erase(source);
    refresh();
    return;
  }
  status_[source] = t("realityLoading");
  refresh();

  // no cookies, no referrer: the service learns the place asked for and the IP
  std::vector<StreetImage> list;
  bool anyError = false;
  std::string errorMsg;
  for (double radius : {NEAR_RADIUS, FAR_RADIUS}) {
    char key[128];
    std::snprintf(key, sizeof key, "%s:%.4f:%.4f:%g", sourceName(source).c_str(), place_.lat,
                  place_.lon, radius);
    auto it = cache_.find(key);
    if (it == cache_.end()) {
      try {
        SearchArea area{place_.lat, place_.lon, radius};
        auto got = searchImages(source, area, r.mapillaryToken, http_);
        cache_[key] = got;
        it = cache_.find(key);
      } catch (const std::exception& e) {
        anyError = true;
        errorMsg = e.what();
        break;
      }
    }
    list = it->second;
    if (radius == FAR_RADIUS) radius_ = FAR_RADIUS;
    if (!list.empty()) break;
  }
  if (anyError) {
    found_.erase(source);
    std::string msg = errorMsg == "token" ? t("realityNeedToken") : errorMsg.substr(0, 90);
    status_[source] = t("realityError", {{"e", msg}});
  } else {
    found_[source] = list;
    status_[source] = t("realityCount", {{"n", std::to_string(list.size())}});
  }
  refresh();
}

void RealityPanel::refresh() {
  if (!root_) return;
  std::vector<std::pair<StreetImage, double>> ranked =
      rankImages(all(), place_.lat, place_.lon, place_.heading);
  ranked.erase(std::remove_if(ranked.begin(), ranked.end(),
                              [](const auto& r) { return r.second > FAR_RADIUS * 1.5; }),
               ranked.end());
  const StreetImage* best = ranked.empty() ? nullptr : &ranked.front().first;
  if (best && (!current_ || !picked_)) select(*best);
  else if (!current_) showEmpty();
}

void RealityPanel::select(const StreetImage& image) {
  current_ = const_cast<StreetImage*>(&image);
  setCaption(image);
  if (image.pano) {
    // look the way the car faces
    if (std::isfinite(image.heading))
      pano_.yaw = (place_.heading - image.heading) * 0.017453292519943295;
    else
      pano_.yaw = 0;
  } else {
    pano_.close();
  }
}

void RealityPanel::showEmpty() {
  pano_.close();
  current_ = nullptr;
}

void RealityPanel::setCaption(const StreetImage& image) {
  (void)image;  // the platform renders the caption from current()
}

std::string RealityPanel::debugInfo() const {
  char buf[256];
  std::snprintf(buf, sizeof buf, "{\"open\":%s,\"own\":%zu,\"current\":%s,\"panoActive\":%s}",
                root_ ? "true" : "false", own_.size(),
                current_ ? "true" : "false", pano_.active() ? "true" : "false");
  return buf;
}

}  // namespace fs
