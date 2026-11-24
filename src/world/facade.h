// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/world/facade.ts @ v1.7.7
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
 */
#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace fs {

/** FACE */
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

/**
 * The numbers of the corners `first` … `first + n − 1` by a rule, written to
 * `out` (four per corner, at the corner's place in the list).  False if the
 * rule cannot be applied to this face.
 */
bool deriveFace(std::span<const int16_t> pos, size_t first, size_t n, int rule, std::span<int16_t> out);

struct PackedFaces {
  /** per run of corners: corners × 8 + rule */
  std::vector<uint16_t> run;
  /** the four numbers of each corner of the runs without a rule, in their order */
  std::vector<int16_t> uvx;
  /** how many corners were given a rule */
  int ruled = 0;
};

/**
 * For the map build: find the faces of a mesh (triangles that share corners
 * belong together; a face's corners follow each other in the list) and the
 * rule that gives each face's numbers `a16` (four per corner).
 */
PackedFaces packFaces(std::span<const int16_t> pos, std::span<const uint16_t> idx, std::span<const int16_t> a16);
PackedFaces packFaces(std::span<const int16_t> pos, std::span<const uint32_t> idx, std::span<const int16_t> a16);

/** For the game: the four numbers of every corner, from the runs of a tile (`uvx` null: the tile has none). */
std::vector<int16_t> unpackFaces(std::span<const int16_t> pos, std::span<const uint16_t> run, const std::vector<int16_t>* uvx);

}  // namespace fs
