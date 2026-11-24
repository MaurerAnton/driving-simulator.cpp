// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Street-level imagery helpers: distances, search boxes, ranking.
 *
 * Origin: src/reality/types.ts (MaurerAnton/driving-simulator @ 578e397,
 * v0.4.0).  The maths mirrors the JavaScript originals.
 */
#include "reality/types.h"

#include <algorithm>
#include <cmath>

namespace fs {

namespace {
constexpr double M_PER_DEG = 111320;
constexpr double PI_R = 3.14159265358979323846;
}  // namespace

std::string sourceName(ImageSource s) {
  switch (s) {
    case ImageSource::Panoramax: return "Panoramax";
    case ImageSource::KartaView: return "KartaView";
    case ImageSource::Mapillary: return "Mapillary";
    case ImageSource::Local: return "";
  }
  return "";
}

double metresBetween(double lat1, double lon1, double lat2, double lon2) {
  double dy = (lat2 - lat1) * M_PER_DEG;
  double dx = (lon2 - lon1) * M_PER_DEG * std::cos(((lat1 + lat2) / 2) * (PI_R / 180));
  return std::hypot(dx, dy);
}

std::array<double, 4> areaBox(const SearchArea& a) {
  double dLat = a.radius / M_PER_DEG;
  double dLon = a.radius / (M_PER_DEG * std::cos(a.lat * (PI_R / 180)));
  return {a.lon - dLon, a.lat - dLat, a.lon + dLon, a.lat + dLat};
}

double headingDiff(double a, double b) {
  double d = std::fabs(a - b);
  d = std::fmod(d, 360.0);
  return d > 180 ? 360 - d : d;
}

std::vector<std::pair<StreetImage, double>> rankImages(const std::vector<StreetImage>& images,
                                                        double lat, double lon, double heading) {
  struct Ranked {
    StreetImage image;
    double dist;
    double score;
  };
  std::vector<Ranked> out;
  for (const auto& image : images) {
    if (!image.located) continue;
    double dist = metresBetween(lat, lon, image.lat, image.lon);
    double score = dist;
    if (image.pano) {
      score -= 6;
    } else if (std::isnan(image.heading) || std::isnan(heading)) {
      score += 9;
    } else {
      score += std::min(headingDiff(image.heading, heading), 120.0) * 0.22;
    }
    out.push_back({image, dist, score});
  }
  std::stable_sort(out.begin(), out.end(),
                   [](const Ranked& p, const Ranked& q) { return p.score < q.score; });
  std::vector<std::pair<StreetImage, double>> res;
  for (const auto& r : out) res.push_back({r.image, r.dist});
  return res;
}

}  // namespace fs
