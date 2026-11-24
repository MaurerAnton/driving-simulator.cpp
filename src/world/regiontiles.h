// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The tiles of a regional map, fetched one at a time.
 *
 * A region's tiles are kept in the original repository in "parts": tar files of
 * 20–100 MB with one gzipped tile per entry (map-assets/N.tar).  The 1.7.7 game
 * downloads a whole part before it can show a region.  Here only the tiles that
 * are needed are fetched, each with an HTTP range request for just its bytes:
 * `parts/N.json` on the game's own site (written by tools/build-part-index.mjs)
 * says where each tile lies in its part.
 *
 * (New in 2.0: there is no file of the original that this translates.)
 */
#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "core/task.h"
#include "platform/plat.h"

namespace fs {

class RegionTiles {
 public:
  /**
   * `partsBase`: where the indexes are (the site's "parts/"); `assetsBase`: where
   * the parts are (MAP_ASSETS_URL of world/regions.h).
   */
  RegionTiles(std::string partsBase, std::string assetsBase);

  /** Read the indexes of these parts (a region's `tiles`).  False: none of them could be read. */
  Task<bool> open(std::vector<std::string> partFiles);
  /** Is there a tile with this key ("412_398")? */
  bool has(const std::string& key) const { return where_.count(key) > 0; }
  size_t count() const { return where_.size(); }
  /**
   * The tile's bytes as they are stored (gzipped) – status 404 when there is no
   * such tile, 0 when the network failed.
   */
  Task<plat::Response> fetch(std::string key);
  /** how many bytes have been fetched so far (the loading screens show it) */
  double bytesFetched = 0;

 private:
  struct Where {
    int part = 0;
    double offset = 0, size = 0;
  };
  std::string partsBase_, assetsBase_;
  std::vector<std::string> files_;
  std::unordered_map<std::string, Where> where_;
};

}  // namespace fs
