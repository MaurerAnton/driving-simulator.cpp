// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/world/seggrid.ts @ v1.7.7
/**
 * The grid of road segments `World` looks things up in: like `Grid2` (a
 * uniform spatial hash over the ground plane, src/core/geom.h) for numbers –
 * the segment codes, edgeId * 4096 + segment index – but with the bounding
 * box of each segment kept beside its code in the cell.  `surfaceAt` tests
 * that box before it fetches the edge at all: most of the ~20 segments of a
 * 40 m cell lie far from the point asked about, and the edges are kept by
 * their number in the whole map (a sparse array, slow to look into).
 *
 * The codes of a cell keep the order `Grid2` would give them (appended;
 * taken out by moving the last one into the gap), so whoever walks a cell
 * meets the segments in the same order as before.
 */
#pragma once

#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace fs {

/** One cell: its codes, and four numbers per code – minX, minZ, maxX, maxZ of that segment's box. */
struct SegCell {
  std::vector<int64_t> codes;
  std::vector<double> boxes;
};

class SegGrid {
 public:
  double cell;

  explicit SegGrid(double cell_) : cell(cell_) {}

  /** Put a code into the cells the box (x0..x1, z0..z1) covers, with the box (bx0, bz0, bx1, bz1) to test it by. */
  void insertBox(double x0, double z0, double x1, double z1, int64_t code, double bx0, double bz0, double bx1, double bz1) {
    const double c = cell;
    const int64_t ix0 = static_cast<int64_t>(std::floor(x0 / c));
    const int64_t ix1 = static_cast<int64_t>(std::floor(x1 / c));
    const int64_t iz0 = static_cast<int64_t>(std::floor(z0 / c));
    const int64_t iz1 = static_cast<int64_t>(std::floor(z1 / c));
    for (int64_t ix = ix0; ix <= ix1; ix++) {
      for (int64_t iz = iz0; iz <= iz1; iz++) {
        SegCell& cell_ = map_[key(ix, iz)];
        cell_.codes.push_back(code);
        cell_.boxes.push_back(bx0);
        cell_.boxes.push_back(bz0);
        cell_.boxes.push_back(bx1);
        cell_.boxes.push_back(bz1);
      }
    }
  }

  /** Take a code out again – with the box it was put in with. */
  void removeBox(double x0, double z0, double x1, double z1, int64_t code) {
    const double c = cell;
    const int64_t ix0 = static_cast<int64_t>(std::floor(x0 / c));
    const int64_t ix1 = static_cast<int64_t>(std::floor(x1 / c));
    const int64_t iz0 = static_cast<int64_t>(std::floor(z0 / c));
    const int64_t iz1 = static_cast<int64_t>(std::floor(z1 / c));
    for (int64_t ix = ix0; ix <= ix1; ix++) {
      for (int64_t iz = iz0; iz <= iz1; iz++) {
        auto it = map_.find(key(ix, iz));
        if (it == map_.end()) continue;
        std::vector<int64_t>& codes = it->second.codes;
        std::vector<double>& boxes = it->second.boxes;
        size_t i = 0;
        while (i < codes.size() && codes[i] != code) i++;
        if (i == codes.size()) continue;
        const size_t last = codes.size() - 1;
        codes[i] = codes[last];
        codes.pop_back();
        for (size_t j = 0; j < 4; j++) boxes[i * 4 + j] = boxes[last * 4 + j];
        boxes.resize(last * 4);
        if (last == 0) map_.erase(it);
      }
    }
  }

  /** The cell containing (x,z), or null when nothing is in it. */
  const SegCell* cellAt(double x, double z) const {
    const double c = cell;
    auto it = map_.find(key(static_cast<int64_t>(std::floor(x / c)), static_cast<int64_t>(std::floor(z / c))));
    return it == map_.end() ? nullptr : &it->second;
  }

  /** Codes whose cells overlap the cell containing (x,z). */
  const std::vector<int64_t>& at(double x, double z) const {
    static const std::vector<int64_t> EMPTY_CODES;
    const SegCell* cell_ = cellAt(x, z);
    return cell_ ? cell_->codes : EMPTY_CODES;
  }

  /** Visit every code in cells overlapping the box (codes may repeat). */
  template <class Visit>
  void queryBox(double minX, double minZ, double maxX, double maxZ, Visit&& visit) const {
    const double c = cell;
    const int64_t x0 = static_cast<int64_t>(std::floor(minX / c));
    const int64_t x1 = static_cast<int64_t>(std::floor(maxX / c));
    const int64_t z0 = static_cast<int64_t>(std::floor(minZ / c));
    const int64_t z1 = static_cast<int64_t>(std::floor(maxZ / c));
    for (int64_t ix = x0; ix <= x1; ix++) {
      for (int64_t iz = z0; iz <= z1; iz++) {
        auto it = map_.find(key(ix, iz));
        if (it == map_.end()) continue;
        const std::vector<int64_t>& codes = it->second.codes;
        for (size_t i = 0; i < codes.size(); i++) visit(codes[i]);
      }
    }
  }

 private:
  std::unordered_map<int64_t, SegCell> map_;

  static int64_t key(int64_t ix, int64_t iz) { return (ix + 32768) * 65536 + (iz + 32768); }
};

}  // namespace fs
