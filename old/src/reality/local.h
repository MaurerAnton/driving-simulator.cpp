// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The player's own pictures: reading a file name or a side-car list for the
 * position, heading and time, and turning a folder into street images.
 *
 * Origin: src/reality/local.ts (MaurerAnton/driving-simulator @ 578e397,
 * v0.4.0).
 */
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "reality/imagemeta.h"
#include "reality/types.h"

namespace fs {

/** What a file name says about where the picture was taken. */
struct NameInfo {
  /** name without folder, extension and heading suffix – what lists refer to */
  std::string id;
  double lat = NAN, lon = NAN;
  double heading = NAN;
};

/** Read "<lat>,<lon>" (or "<lat>_<lon>") and a heading out of a file name. */
NameInfo parseFileName(const std::string& name,
                       const std::function<bool(double, double)>& plausible =
                           [](double, double) { return true; });

/** One record of a side-car list (JSON, GeoJSON or CSV). */
struct ListEntry {
  /** file name or panorama id, lower case, without extension */
  std::string key;
  double lat = NAN, lon = NAN, heading = NAN, time = NAN;
};

/** Parse a JSON/GeoJSON/CSV side-car list of pictures. */
std::vector<ListEntry> parseList(const std::string& text);

/** The record that belongs to `name`, if the list has one. */
const ListEntry* findInList(const std::vector<ListEntry>& list, const std::string& name,
                            const NameInfo& info);

/** What one own file contributes. */
struct LocalSource {
  /** file name with folder, as the player sees it */
  std::string name;
  ImageMeta meta;
  std::string url;
  std::string thumb;   // empty: use the picture itself
  bool hasThumb = false;
};

/** Describe one own picture as a street image. */
StreetImage describeLocal(const LocalSource& src, const std::vector<ListEntry>& list,
                          const std::function<bool(double, double)>& plausible =
                              [](double, double) { return true; });

/** Which files in a folder are pictures / side-car lists (regex sources). */
extern const char* const IMAGE_FILE;
extern const char* const LIST_FILE;
/** Does `name` end in a picture / side-car list extension? */
bool hasExt(const std::string& name, const char* pattern);

/** Pictures from a pre-built index (the platform reads public/panos). */
std::vector<StreetImage> imagesFromIndex(const std::string& json, const std::string& baseUrl);

}  // namespace fs
