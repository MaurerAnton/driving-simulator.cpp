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

#include "core/geom.h"

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
    int64_t ix0, ix1, iz0, iz1;
    if (!gridCell(x0, c, ix0) || !gridCell(x1, c, ix1) || !gridCell(z0, c, iz0) || !gridCell(z1, c, iz1)) return;
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
    int64_t ix0, ix1, iz0, iz1;
    if (!gridCell(x0, c, ix0) || !gridCell(x1, c, ix1) || !gridCell(z0, c, iz0) || !gridCell(z1, c, iz1)) return;
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
    int64_t ix, iz;
    if (!gridCell(x, cell, ix) || !gridCell(z, cell, iz)) return nullptr;
    auto it = map_.find(key(ix, iz));
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
    int64_t x0, x1, z0, z1;
    if (!gridCell(minX, c, x0) || !gridCell(maxX, c, x1) || !gridCell(minZ, c, z0) || !gridCell(maxZ, c, z1)) return;
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
