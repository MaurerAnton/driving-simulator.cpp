// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The measured details of walls in a tile (tools/mapgen/facadedetail.ts):
 * real windows, doors, balconies, shop fronts and plinths as geometry,
 * instead of the window grid the facade shader works out.
 *
 * Two sections carry them, both new in 1.6.0 and never asked for by older
 * versions (which read sections only by name – they draw these walls with
 * their usual windows):
 *  - `fdet_pos`, `fdet_idx`, `fdet_a8`: the details, a mesh like `struct`
 *    (r, g, b per corner);
 *  - `bldg_obs`: the walls in the building mesh that have them, as pairs
 *    (first corner, number of corners).  Their face style gets OBSERVED
 *    added when the tile is loaded: the facade shader then leaves out its
 *    windows and doors there (render/materials).
 *
 * The details are built only while buildings are drawn with facades (from
 * the medium graphics level up), and drawn only near by: the tile is cut
 * into squares, each shown within DETAIL_DISTANCE of the viewer.
 *
 * Origin: src/render/facadedetail.ts @ v1.7.7
 */
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "engine/engine.h"
#include "world/tilefmt.h"

namespace fs {

/** added to the face style of a wall with measured details (the styles themselves are below 16) */
inline constexpr int OBSERVED = 16;
/** the details of a square are drawn while the viewer is this near to its middle (metres) */
inline constexpr double DETAIL_DISTANCE = 380;

/**
 * Mark the walls with measured details in the building mesh's style bytes
 * (`a8`: style, r, g, b per corner) – once: a corner already marked stays as
 * it is.  Returns the number of corners marked.
 *
 * `a8` is the section `bldg_a8` – which in a tile that is loaded is the array
 * of the attribute `astyle` of the mesh named `bldg` in the tile's group;
 * `runs` the section `bldg_obs`.
 */
int flagObservedWalls(std::vector<uint8_t>* a8, const SectionArray* runs);

/** The material of the details: their colours, lit; drawn just before the wall they lie on, both sides (railings). */
MaterialPtr makeDetailMaterial();

/**
 * The details of a tile as objects to add to its group: one level of detail
 * per square of the tile that has any (null: the tile has none).
 *
 * The arrays of the sections become those of the geometries (they move: the
 * sections are left empty, as those of the tile's other meshes are), and the
 * geometries go with the last reference to the group – the original's list
 * of `disposables` is not needed.
 */
std::shared_ptr<Group> buildFacadeDetail(UnpackedTile& data, const TileHeader& h, const MaterialPtr& mat);

}  // namespace fs
