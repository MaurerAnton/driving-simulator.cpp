// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Asking the free street-level imagery services for pictures around a place,
 * and parsing what they answer.
 *
 * The HTTP call itself belongs to the platform (HttpBackend), so the same
 * code runs in the browser (no cookies, no referrer) and in tests.
 *
 * Origin: src/reality/providers.ts (MaurerAnton/driving-simulator @ 578e397,
 * v0.4.0).
 */
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "reality/types.h"

namespace fs {

/** What the platform needs to perform one request. */
class HttpBackend {
 public:
  virtual ~HttpBackend() = default;
  /** GET `url` as text; `ok` says whether the status was 2xx. */
  virtual std::string get(const std::string& url, bool& ok, int& status) = 0;
  /**
   * GET `url` and hand the answer to `done` (the aerial photographs are
   * fetched while the map is drawn: the page fetches in the background and
   * calls AerialCache::poll when its promises settle).  The default answers
   * right away, as a synchronous backend does.
   */
  virtual void order(const std::string& url, const std::function<void(bool, std::string)>& done) {
    bool ok = false;
    int status = 0;
    done(true, get(url, ok, status));
  }
};

std::string panoramaxUrl(const SearchArea& a, int limit = 60);
std::vector<StreetImage> parsePanoramax(const std::string& json);

std::string kartaviewUrl(const SearchArea& a, int limit = 60);
std::vector<StreetImage> parseKartaView(const std::string& json);

std::string mapillaryUrl(const SearchArea& a, const std::string& token, int limit = 60);
std::vector<StreetImage> parseMapillary(const std::string& json);

/** What a service said when it refused (shown to the player). */
std::string serviceError(const std::string& json, int status);

/**
 * Ask one service for pictures around a place.  Throws std::runtime_error
 * with a readable message when the service refuses.
 */
/**
 * Ask one service for pictures around a place.  Throws std::runtime_error
 * with a readable message when the service refuses.
 * `http` may be null in a dry run (the caller then reports the error itself).
 */
std::vector<StreetImage> searchImages(ImageSource source, const SearchArea& area,
                                       const std::string& token, HttpBackend* http);

struct PlaceLink {
  std::string id;
  std::string label;
  std::string url;
};

/** The same place on the web sites of the imagery services. */
std::vector<PlaceLink> placeLinks(double lat, double lon, double heading);

}  // namespace fs
