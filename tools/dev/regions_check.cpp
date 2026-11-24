// SPDX-License-Identifier: AGPL-3.0-only
// LINK: game render engine world core platform geo
/**
 * Opens every region there is, one after the other, as the game does – its index
 * and road network from the map-assets of the original repository, the tiles
 * around a place out of the parts, one frame drawn – and says for each whether
 * it worked.  (1.7.7 was only ever tried in Hesse; the regions of other
 * countries are opened here for the first time.)
 *
 *   tools/t.sh regions_check [--only ID[,ID…]] [--from N] [--count N] [--radius M]
 *                            [--pictures DIR] [--size WxH] [--timeout SECONDS]
 *
 * --only: just these regions; --from / --count: a stretch of the index (to go through
 * it in pieces); --radius: how far around the place tiles are loaded (default 250 m:
 * a handful of tiles, a few hundred kilobytes a region); --pictures: a PNG of each
 * region into that folder.  The files come over the network (curl) and the indexes
 * of the parts from web/parts of this repository.
 *
 * One line a region; the last line counts them.  The exit code is the number of
 * regions that failed (at most 100).
 */
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

#include "core/js.h"
#include "game/app.h"
#include "platform/glhost.h"
#include "platform/native/native.h"
#include "platform/plat.h"
#include "world/regions.h"

using namespace fs;

static double now() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

namespace {

struct Result {
  bool ok = false;
  std::string why;
  double openMs = 0, tilesMs = 0;
  size_t roads = 0, junctions = 0, tiles = 0;
  int calls = 0, triangles = 0;
  double x = 0, z = 0;
};

/** The middle of the tile nearest to (x, z) that the map has (the middle of a region's box may be sea). */
bool nearestTile(const TileGrid& g, double& x, double& z) {
  if (!g.has || g.size <= 0) return false;
  const int cx = static_cast<int>(std::floor((x - g.x0) / g.size)), cz = static_cast<int>(std::floor((z - g.z0) / g.size));
  for (int r = 0; r < 400; r++) {
    for (int dz = -r; dz <= r; dz++) {
      for (int dx = -r; dx <= r; dx++) {
        if (std::max(std::abs(dx), std::abs(dz)) != r) continue;
        if (!g.has(cx + dx, cz + dz)) continue;
        x = g.x0 + (cx + dx + 0.5) * g.size;
        z = g.z0 + (cz + dz + 0.5) * g.size;
        return true;
      }
    }
  }
  return false;
}

Result check(const Region& region, double radius, double timeoutMs, const std::string& picture, int width, int height) {
  Result r;
  App app(dom::el("canvas"));
  double t0 = now();
  try {
    plat::native::wait(app.loadWorld(nullptr, region));
  } catch (const std::exception& e) {
    r.why = std::string("the map did not open: ") + e.what();
    return r;
  }
  r.openMs = now() - t0;
  if (app.regionId != region.id) {
    r.why = "the indexes of its parts could not be read (web/parts)";
    return r;
  }
  r.x = (region.box[0] + region.box[2]) / 2;
  r.z = (region.box[1] + region.box[3]) / 2;
  if (!nearestTile(app.map->tiles, r.x, r.z)) {
    r.why = "the map names no tile";
    return r;
  }
  app.tiles->viewRadius = radius;
  app.sky.setSun(52, 215);
  t0 = now();
  bool settled = false;
  while (now() - t0 < timeoutMs) {
    app.map->update(r.x, r.z);
    app.tiles->update(r.x, r.z);
    if (plat::native::pump() == 0 && app.tiles->settled() && app.map->ready(r.x, r.z)) {
      settled = true;
      break;
    }
  }
  r.tilesMs = now() - t0;
  r.tiles = app.tiles->tiles.size();
  r.roads = app.world->allEdges.size();
  r.junctions = app.world->allNodes.size();
  if (r.tiles == 0) {
    r.why = settled ? "no tile came (none of its parts holds the tiles the map names here)" : "no tile came in time";
    return r;
  }
  if (!settled) {
    r.why = "not everything around the place came in time";
    return r;
  }
  double ground = app.tiles->heightAt(r.x, r.z);
  if (std::isnan(ground)) {
    r.why = "no ground under the place (the tile there did not load)";
    return r;
  }
  Camera& cam = *app.camera;
  cam.position.set(r.x, ground + 70, r.z + 90);
  cam.rotation.set(-35 * 3.14159265358979323846 / 180, 0, 0, Euler::YXZ);
  app.render(Vector3(r.x, ground, r.z));
  r.calls = app.renderer.info.calls;
  r.triangles = app.renderer.info.triangles;
  if (r.calls == 0 || r.triangles == 0) {
    r.why = "nothing was drawn";
    return r;
  }
  if (!picture.empty() && !plat::gl::savePng(picture, app.renderer.bufferWidth(), app.renderer.bufferHeight())) {
    r.why = "the picture could not be written to " + picture;
    return r;
  }
  if (r.roads == 0) {
    r.why = "no road near the place";
    return r;
  }
  r.ok = true;
  return r;
}

}  // namespace

int main(int argc, char** argv) {
  std::set<std::string> only;
  int from = 0, count = 1 << 30, width = 960, height = 540;
  double radius = 250, timeout = 90;
  std::string pictures, root = ".";
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
    if (a == "--only") {
      for (const std::string& id : split(next(), ",")) only.insert(id);
    } else if (a == "--from") from = std::atoi(next());
    else if (a == "--count") count = std::atoi(next());
    else if (a == "--radius") radius = std::atof(next());
    else if (a == "--timeout") timeout = std::atof(next());
    else if (a == "--pictures") pictures = next();
    else if (a == "--size") std::sscanf(next(), "%dx%d", &width, &height);
    else if (a == "--root") root = next();
    else {
      std::fprintf(stderr, "unknown argument %s\n", a.c_str());
      return 101;
    }
  }
  if (!pictures.empty() && pictures.back() != '/') pictures += '/';
  // the indexes of the parts come from this repository (the site serves them at parts/); everything else over the network
  plat::native::mount(plat::locationBase() + "parts/", root + "/web/parts/");
  plat::native::useNetwork(true);
  plat::native::setViewport(width, height, 1);
  if (!plat::gl::createContext("view", width, height)) {
    std::fprintf(stderr, "no OpenGL ES here\n");
    return 101;
  }
  std::optional<RegionIndex> index = plat::native::wait(loadRegionIndex(plat::locationBase()));
  if (!index || index->regions.empty()) {
    std::fprintf(stderr, "the regions' index could not be read (%sregions.json)\n", MAP_ASSETS_URL);
    return 101;
  }
  std::printf("%zu regions in the index\n", index->regions.size());
  int done = 0, failed = 0, n = 0;
  for (const Region& region : index->regions) {
    const int at = n++;
    if (!only.empty() ? only.count(region.id) == 0 : (at < from || at >= from + count)) continue;
    Result r = check(region, radius, timeout * 1000, pictures.empty() ? "" : pictures + region.id + ".png", width, height);
    done++;
    if (!r.ok) failed++;
    std::printf("%-4s %3d %-28s %-30s open %5.1f s, %2zu tiles %5.1f s, %6zu roads, %5zu junctions, %3d calls, %7d triangles, at %.0f,%.0f%s%s\n", r.ok ? "ok" : "FAIL", at,
                region.id.c_str(), region.name.c_str(), r.openMs / 1000, r.tiles, r.tilesMs / 1000, r.roads, r.junctions, r.calls, r.triangles, r.x, r.z, r.ok ? "" : "  – ",
                r.why.c_str());
    std::fflush(stdout);
  }
  std::printf("%d regions opened, %d failed\n", done, failed);
  return std::min(failed, 100);
}
