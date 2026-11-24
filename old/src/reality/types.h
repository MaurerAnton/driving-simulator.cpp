// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Street-level imagery: what a picture is, where it was taken and which
 * service it came from, plus the search helpers the panel uses.
 *
 * Origin: src/reality/types.ts (MaurerAnton/driving-simulator @ 578e397,
 * v0.4.0).
 */
#pragma once

#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace fs {

enum class ImageSource { Panoramax, KartaView, Mapillary, Local };

struct StreetImage {
  ImageSource source = ImageSource::Local;
  std::string id;
  double lat = 0, lon = 0;
  /** compass direction of the middle of the picture, degrees clockwise from north; NaN if unknown */
  double heading = NAN;
  /** full 360° equirectangular panorama (looked around in), not a flat photograph */
  bool pano = false;
  /** when it was taken, milliseconds since 1970; NaN if unknown */
  double time = NAN;
  /** who took it ('' if the service does not say) */
  std::string author;
  std::string licence;
  /** small preview */
  std::string thumb;
  /** picture in a size that suits a screen */
  std::string url;
  /** largest version on offer (used for panoramas, where every pixel counts) */
  std::string full;
  /** page of the picture at its source, '' for own files */
  std::string page;
  /** file name of an own picture */
  std::string title;
  /** false: an own picture without any position – it can be pinned to where the car is */
  bool located = false;
};

struct SearchArea {
  double lat = 0, lon = 0;
  /** metres */
  double radius = 0;
};

/** The service's display name. */
std::string sourceName(ImageSource s);

/** Great-circle distance in metres (the original's flat approximation). */
double metresBetween(double lat1, double lon1, double lat2, double lon2);

/** The bounding box of a search area as [minLat, minLon, maxLat, maxLon]. */
std::array<double, 4> areaBox(const SearchArea& a);

/** Smallest angle between two headings, in degrees. */
double headingDiff(double a, double b);

/** The images ranked by how well they face the requested direction. */
std::vector<std::pair<StreetImage, double>> rankImages(const std::vector<StreetImage>& images,
                                                        double lat, double lon, double heading);

}  // namespace fs
