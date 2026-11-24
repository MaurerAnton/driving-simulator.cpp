// SPDX-License-Identifier: AGPL-3.0-only
// LINK: world core platform geo
/**
 * How long reading the chunks of the road network takes: every chunk of a map
 * (the files w/<key>.json.gz) is inflated and read into its records, as the game reads them
 * while it runs – and, to compare, read once more into a whole document in
 * memory (the way of the first draft).
 *
 *   tools/t.sh world_jsonbench [--map /home/user/driving-simulator/public/map] [--dom 1]
 */
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include "platform/native/native.h"
#include "platform/plat.h"
#include "world/grid.h"
#include "world/loader.h"
#include "world/mapfiles.h"
#include "world/worldjson.h"

using namespace fs;

namespace {

double nowMs() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

size_t countValues(const Json& j) {
  size_t n = 1;
  for (const Json& e : j.array) n += countValues(e);
  for (const auto& kv : j.object) n += countValues(kv.second);
  return n;
}

Task<void> run(std::string dir, bool dom) {
  std::shared_ptr<MapFiles> files = diskFiles(dir);
  std::optional<MapIndexJson> index = co_await readJson<MapIndexJson>(files, "map.json");
  if (!index) throw std::runtime_error("no map.json in " + dir);
  const GridSet chunks = GridSet::from(index->chunks);
  double bytes = 0, readMs = 0, domMs = 0, worst = 0, worstBytes = 0;
  size_t nodes = 0, edges = 0, signals = 0, points = 0, n = 0, values = 0;
  std::string worstKey;
  for (int cz = chunks.z0; cz < chunks.z0 + chunks.h; cz++) {
    for (int cx = chunks.x0; cx < chunks.x0 + chunks.w; cx++) {
      if (!chunks.has(cx, cz)) continue;
      const std::string key = std::to_string(cx) + "_" + std::to_string(cz);
      std::optional<Bytes> buf = co_await files->bytes("w/" + key + ".json.gz");
      if (!buf) continue;
      n++;
      bytes += static_cast<double>(buf->size());
      const std::string_view text = jsonTextOf(*buf);
      const double t0 = nowMs();
      WorldChunkJson chunk;
      if (!jsonParse(text, chunk)) throw std::runtime_error("chunk " + key + " is not JSON");
      const double dt = nowMs() - t0;
      readMs += dt;
      if (dt > worst) {
        worst = dt;
        worstKey = key;
        worstBytes = static_cast<double>(buf->size());
      }
      nodes += chunk.nodes.size();
      edges += chunk.edges.size();
      signals += chunk.signals.size();
      for (const auto& e : chunk.edges) points += e.second.q ? e.second.q->size() / 3 : e.second.p.size() / 3;
      if (dom) {
        const double t1 = nowMs();
        Json doc;
        if (!Json::parse(text, doc)) throw std::runtime_error("chunk " + key + " is not JSON");
        domMs += nowMs() - t1;
        values += countValues(doc);
      }
    }
  }
  std::printf("%zu chunks, %.1f MB of JSON: %zu junctions, %zu roads (%zu points), %zu signal controllers\n", n, bytes / 1e6, nodes, edges, points, signals);
  std::printf("read into records: %.0f ms (%.0f MB/s); the slowest chunk %s (%.2f MB): %.1f ms\n", readMs, bytes / 1e6 / (readMs / 1000), worstKey.c_str(), worstBytes / 1e6, worst);
  if (dom) std::printf("read into a document in memory: %.0f ms (%.0f MB/s), %zu values of %zu bytes each\n", domMs, bytes / 1e6 / (domMs / 1000), values, sizeof(Json));
}

}  // namespace

int main(int argc, char** argv) {
  std::string dir = "/home/user/driving-simulator/public/map";
  bool dom = false;
  for (int i = 1; i + 1 < argc; i++) {
    if (std::string(argv[i]) == "--map") dir = argv[i + 1];
    if (std::string(argv[i]) == "--dom") dom = std::string(argv[i + 1]) == "1";
  }
  try {
    plat::native::wait(run(dir, dom));
  } catch (const std::exception& e) {
    std::fprintf(stderr, "world_jsonbench: %s\n", e.what());
    return 1;
  }
  return 0;
}
