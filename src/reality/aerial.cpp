// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Aerial photographs under the map (origin: src/reality/aerial.ts @ ff37c5d,
 * v0.7.0): the tile grid in UTM metres, the WMS address of one picture, and
 * a small cache that fetches the pictures the map asks for and decodes them
 * into pixels the map can draw.
 */
#include "reality/aerial.h"

#include <algorithm>
#include <cmath>

#include "platform/jpegdec.h"

namespace fs {

const AerialSourceInfo AERIAL_SOURCE;

double aerialTileSize(int level) {
  return AERIAL_TILE_PX * AERIAL_FINEST * std::pow(2.0, level);
}

int aerialLevel(double pxPerMetre) {
  // metres per pixel on the screen
  double want = 1 / std::max(pxPerMetre, 1e-6);
  int level = static_cast<int>(std::floor(std::log2(want / AERIAL_FINEST) + 0.35));
  return std::min(AERIAL_MAX_LEVEL, std::max(0, level));
}

std::vector<AerialTile> aerialTiles(int level, double minE, double minN, double maxE, double maxN) {
  double size = aerialTileSize(level);
  std::vector<AerialTile> out;
  for (int row = static_cast<int>(std::floor(minN / size)); row * size < maxN; row++) {
    for (int col = static_cast<int>(std::floor(minE / size)); col * size < maxE; col++) {
      AerialTile t;
      t.level = level;
      t.col = col;
      t.row = row;
      t.minE = col * size;
      t.minN = row * size;
      t.size = size;
      out.push_back(t);
    }
  }
  return out;
}

namespace {
/** "6184.40" as the URL carries it (two decimals, no exponent) */
std::string bboxNum(double v) {
  char buf[40];
  std::snprintf(buf, sizeof buf, "%.2f", std::round(v * 100.0) / 100.0);
  // (trailing zeros trimmed the way String(6184.4) writes it)
  std::string s(buf);
  if (s.find('.') != std::string::npos) {
    while (s.back() == '0') s.pop_back();
    if (s.back() == '.') s.pop_back();
  }
  return s;
}
}  // namespace

std::string aerialUrl(const AerialTile& t) {
  std::string bbox = bboxNum(t.minE) + "," + bboxNum(t.minN) + "," + bboxNum(t.minE + t.size) +
                     "," + bboxNum(t.minN + t.size);
  return std::string(AERIAL_SOURCE.wms) + "?SERVICE=WMS&VERSION=1.3.0&REQUEST=GetMap&LAYERS=" +
         AERIAL_SOURCE.layer + "&STYLES=&CRS=EPSG%3A25832&BBOX=" + bbox +
         "&WIDTH=" + std::to_string(AERIAL_TILE_PX) + "&HEIGHT=" + std::to_string(AERIAL_TILE_PX) +
         "&FORMAT=image%2Fjpeg";
}

std::string aerialKey(const AerialTile& t) {
  return std::to_string(t.level) + "/" + std::to_string(t.col) + "/" + std::to_string(t.row);
}

AerialCache::AerialCache(const std::function<void()>& onLoad, HttpBackend* http, int max)
    : onLoad_(onLoad), max_(max), http_(http) {}

const Canvas2D* AerialCache::get(const AerialTile& t) {
  std::string key = aerialKey(t);
  auto it = tiles_.find(key);
  if (it != tiles_.end()) {
    it->second.used = ++tick_;
    return it->second.ready ? it->second.img.get() : nullptr;
  }
  for (const AerialTile& q : queue_)
    if (aerialKey(q) == key) return nullptr;
  queue_.push_back(t);
  pump();
  return nullptr;
}

void AerialCache::beginFrame() { queue_.clear(); }

void AerialCache::poll() {
  // the platform has answers waiting: decode them, then say so
  bool any = false;
  for (auto& kv : tiles_) {
    Entry& e = kv.second;
    if (e.ready || e.failed || e.bytes.empty()) continue;
    int w = 0, h = 0;
    std::vector<uint8_t> pixels;
    if (decodeJpeg(e.bytes, pixels, w, h)) {
      e.img = std::make_unique<Canvas2D>(w, h);
      e.img->pixels() = std::move(pixels);
      e.ready = true;
      any = true;
    } else {
      e.failed = true;
    }
    e.bytes.clear();
    e.bytes.shrink_to_fit();
  }
  trim();
  if (any && onLoad_) onLoad_();
}

void AerialCache::pump() {
  // a few at a time: the server is a public service
  int started = 0;
  while (started < 4 && !queue_.empty()) {
    AerialTile t = queue_.front();
    queue_.erase(queue_.begin());
    std::string key = aerialKey(t);
    if (tiles_.count(key)) continue;
    Entry entry;
    entry.used = ++tick_;
    tiles_[key] = std::move(entry);
    started++;
    if (!http_) {
      tiles_[key].failed = true;
      continue;
    }
    // the platform fetches in the background; poll() decodes the answer
    http_->order(aerialUrl(t), [this, key](bool ok, std::string body) {
      auto it = tiles_.find(key);
      if (it == tiles_.end()) return;
      if (!ok || body.empty()) {
        it->second.failed = true;
        return;
      }
      it->second.bytes.assign(body.begin(), body.end());
    });
  }
  trim();
}

void AerialCache::trim() {
  if (static_cast<int>(tiles_.size()) <= max_) return;
  // drop the least recently used that is settled (ready or failed)
  std::vector<std::pair<int, std::string>> old;
  for (auto& kv : tiles_)
    if (kv.second.ready || kv.second.failed) old.push_back({kv.second.used, kv.first});
  std::sort(old.begin(), old.end());
  int excess = static_cast<int>(tiles_.size()) - max_;
  for (int i = 0; i < excess && i < static_cast<int>(old.size()); i++)
    tiles_.erase(old[static_cast<size_t>(i)].second);
}

}  // namespace fs
