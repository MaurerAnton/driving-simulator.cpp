// SPDX-License-Identifier: AGPL-3.0-only
// Origin: tools/mapgen/mapfiles.ts @ v1.7.7
#include "world/mapfiles.h"

#ifndef __EMSCRIPTEN__

#include <fstream>

namespace fs {

namespace {

class DiskFiles : public MapFiles {
 public:
  explicit DiskFiles(std::string dir) : dir_(std::move(dir)) {}

  Task<std::optional<Bytes>> bytes(std::string path) override {
    std::optional<Bytes> data = read(resolve(path));
    Resolver<std::optional<Bytes>> r;
    plat::soon([r, data = std::move(data)]() mutable { r.resolve(std::move(data)); });
    return r.task();
  }

 private:
  std::string dir_;

  /** `resolve(dir, path)`: the path in the folder (a path from the root stays what it is) */
  std::string resolve(const std::string& path) const {
    if (!path.empty() && path[0] == '/') return path;
    if (dir_.empty() || dir_.back() == '/') return dir_ + path;
    return dir_ + "/" + path;
  }

  static std::optional<Bytes> read(const std::string& file) {
    std::ifstream f(file, std::ios::binary);
    if (!f) return std::nullopt;
    f.seekg(0, std::ios::end);
    const std::streamoff n = f.tellg();
    if (n < 0) return std::nullopt;
    f.seekg(0);
    Bytes raw(static_cast<size_t>(n));
    if (n > 0) f.read(reinterpret_cast<char*>(raw.data()), n);
    if (!f && n > 0) return std::nullopt;
    if (raw.size() > 1 && raw[0] == 0x1f && raw[1] == 0x8b) {
      Bytes plain = plat::gunzip(raw);
      // (a gzip file that cannot be inflated: the original's gunzipSync throws – here the file is none)
      if (plain.empty() && raw.size() > 20) return std::nullopt;
      return plain;
    }
    return raw;
  }
};

}  // namespace

std::shared_ptr<MapFiles> diskFiles(std::string dir) { return std::make_shared<DiskFiles>(std::move(dir)); }

}  // namespace fs

#endif
