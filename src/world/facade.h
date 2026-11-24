// SPDX-License-Identifier: AGPL-3.0-only
/**
 * What the faces of a building need for their facades.
 *
 * Every corner of a face carries four numbers for the shader: where on the
 * face it lies (u along it, v up it) and, for walls, how long and how high
 * the wall is (L, H) – rows and columns of windows are fitted to these.
 * They follow from where the corners of the face are, so a tile does not
 * store them: it says for each face which rule gives them (FACE), and the
 * game works them out when it loads the tile.  Only the faces for which the
 * rule does not give what the map build wanted (to a few centimetres) keep
 * their numbers.  That saves about an eighth of a tile in town.
 *
 * A face is a run of corners that follow each other in the list of
 * vertices, in the order of its outline.
 *
 * Origin: src/world/facade.ts (MaurerAnton/driving-simulator @ 3420ad0, v0.8.0).
 */
#pragma once

#include <cstdint>
#include <vector>

namespace fs {

enum FaceRule : int {
  /** the four numbers of each corner are stored */
  FACE_EXPLICIT = 0,
  /** a level roof: u east, v south */
  FACE_ROOF_FLAT = 1,
  /** a sloping roof: u level along it, v up the slope */
  FACE_ROOF_SLOPED = 2,
  /** a wall: u level along it, v up from its lowest corner */
  FACE_WALL = 3,
  /** a wall whose foot is sunk into the ground (SINK): v counts from where the foot was */
  FACE_WALL_SUNK = 4,
};

/** how far the map build sinks the foot of a wall that stands on the ground into it */
constexpr double SINK = 1.6;
/** a corner higher than this above the foot of a wall can be its eave */
constexpr double EAVE_ABOVE = 0.6;
/** how far (in units of UV_SCALE) what a rule gives may be from what the map build wanted */
constexpr int TOLERANCE = 2;
/** a run is written as  corners × 8 + rule  in 16 bits */
constexpr int MAX_RUN = 8191;

/** The numbers of the corners `first` … `first + n − 1` by a rule, written to `out`
 *  (four per corner, at the corner's place in the list).  False if the rule
 *  cannot be applied to this face. */
bool deriveFace(const std::vector<double>& pos, int first, int n, int rule,
                std::vector<int16_t>& out);

/** The tile build's side: runs of corners with the rule that gives their four numbers. */
struct PackedFaces {
  std::vector<uint16_t> run;
  std::vector<int16_t> uvx;
  /** how many corners a rule covers (the rest are explicit) */
  int ruled = 0;
};

PackedFaces packFaces(const std::vector<double>& pos, const std::vector<double>& idx,
                      const std::vector<int16_t>& a16);

/** For the game: the four numbers of every corner, from the runs of a tile. */
std::vector<int16_t> unpackFaces(const std::vector<double>& pos, const std::vector<uint16_t>& run,
                                 const std::vector<int16_t>* uvx);

}  // namespace fs
