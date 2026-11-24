// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/people.ts @ v1.7.7
#include "render/people.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <stdexcept>

#include "core/geom.h"
#include "core/js.h"
#include "engine/geometries.h"
#include "render/glsl_gen.h"

namespace fs {

namespace {

constexpr double P_PI = 3.14159265358979323846;

// ---- the measures of the figure, for a person 1.72 m tall (everything scales with the height)
constexpr double HEIGHT = 1.72;
/** half the distance between the hip joints */
constexpr double HIP_X = 0.088;
/** hip joint to knee, and knee to sole */
constexpr double THIGH = 0.42;
constexpr double LOWER_LEG = 0.49;
/** the heel behind the ankle, the toe ahead of it */
constexpr double HEEL_Z = 0.06;
constexpr double TOE_Z = -0.19;
/** the shoulder joints and the foot of the neck above the hip joints */
constexpr double SHOULDER_Y = 0.475;
constexpr double SHOULDER_X = 0.182;
constexpr double NECK_Y = 0.555;
constexpr double UPPER_ARM = 0.31;
/** where the hand holds things, in the frame of the arm (shoulder joint at the origin, the arm hanging down) */
const Vector3 GRIP(0, -0.635, -0.075);
/** the radius of the wheels of a suitcase */
constexpr double CASE_WHEEL = 0.032;

// ---- the variants of each part (a bit each in the mask of a piece)
constexpr int JACKET = 0;
constexpr int HOODIE = 1;
constexpr int COAT = 2;
constexpr int DRESS = 3;
/** a coat and a dress on somebody sitting: the skirt lies over the thighs */
constexpr int COAT_SITTING = 4;
constexpr int DRESS_SITTING = 5;

constexpr int LONG_SLEEVE = 0;
constexpr int SHORT_SLEEVE = 1;

constexpr int SHOES = 0;
constexpr int BOOTS = 1;

constexpr int SHORT = 0;
constexpr int LONG = 1;
constexpr int BUN = 2;
constexpr int PONYTAIL = 3;
constexpr int CAP = 4;
constexpr int BEANIE = 5;
constexpr int HEADSCARF = 6;
constexpr int BALDING = 7;
constexpr int CURLY = 8;
constexpr int FLAT_CAP = 9;

constexpr int NOTHING = -1;
constexpr int SHOULDER_BAG = 0;
constexpr int STRAP = 1;
constexpr int BACKPACK = 2;
constexpr int BACKPACK_STRAPS = 3;
constexpr int HANDBAG = 4;
constexpr int SHOPPING_BAG = 5;
constexpr int BRIEFCASE = 6;
constexpr int SUITCASE = 7;
constexpr int STICK = 8;
constexpr int UMBRELLA = 9;

// ------------------------------------------------------------------------------------ shapes
using V3 = std::array<double, 3>;
using Variants = std::initializer_list<int>;

/** Which colour a piece takes: the first colour of its part, the second one, or a colour of its own. */
constexpr int A = 0;
constexpr int B = 1;
constexpr int OWN = 2;

/**
 * Prepares a piece of a part: its colour slot, the variants it belongs to
 * (empty: all of them) and its own colour – for A and B pieces a shade that
 * darkens the person's colour (a lining, a flap), for OWN pieces the colour.
 */
GeometryPtr piece(GeometryPtr g, int slot, Variants variants, uint32_t own = 0xffffff, bool all = false) {
  if (g->getAttribute("uv")) g->deleteAttribute("uv");
  const size_t n = g->getAttribute("position")->count();
  const Color c(own);
  std::vector<float> colour(n * 3);
  for (size_t i = 0; i < n; i++) {
    colour[i * 3] = static_cast<float>(c.r);
    colour[i * 3 + 1] = static_cast<float>(c.g);
    colour[i * 3 + 2] = static_cast<float>(c.b);
  }
  int mask = 0xffff;
  if (!all) {
    mask = 0;
    for (const int v : variants) mask |= 1 << v;
  }
  g->setAttribute("color", std::make_shared<BufferAttribute>(std::move(colour), 3));
  g->setAttribute("aSlot", std::make_shared<BufferAttribute>(std::vector<float>(n, static_cast<float>(slot)), 1));
  g->setAttribute("aMask", std::make_shared<BufferAttribute>(std::vector<float>(n, static_cast<float>(mask)), 1));
  return g;
}

/** a piece of all the variants (the original's `null`) */
GeometryPtr pieceAll(GeometryPtr g, int slot, uint32_t own = 0xffffff) { return piece(std::move(g), slot, {}, own, true); }

/** An ellipsoid (or a part of one: down to `thetaLength` from the top, tipped back by `tilt`). */
GeometryPtr ellipsoid(V3 r, V3 at, int w = 10, int h = 7, double thetaLength = P_PI, double tilt = 0, double phiStart = 0, double phiLength = P_PI * 2,
                      double thetaStart = 0) {
  GeometryPtr g = sphereGeometry(1, w, h, phiStart, phiLength, thetaStart, thetaLength);
  g->scale(r[0], r[1], r[2]);
  if (tilt != 0) g->rotateX(tilt);
  g->translate(at[0], at[1], at[2]);
  return g;
}

/** A limb hanging from y0: a round bar, r0 thick at the top and r1 at the bottom, `len` long. */
GeometryPtr limb(double r0, double r1, double len, double y0 = 0, int sides = 8) {
  GeometryPtr g = cylinderGeometry(r0, r1, len, sides);
  g->translate(0, y0 - len / 2, 0);
  return g;
}

const Vector3 UP(0, 1, 0);

/** A round bar from a to b, r0 thick at a and r1 at b. */
GeometryPtr tube(V3 a, V3 b, double r0, double r1 = -1, int sides = 7) {
  if (r1 < 0) r1 = r0;
  Vector3 d(b[0] - a[0], b[1] - a[1], b[2] - a[2]);
  GeometryPtr g = cylinderGeometry(r1, r0, d.length(), sides);
  Quaternion q;
  q.setFromUnitVectors(UP, d.normalize());
  g->applyQuaternion(q);
  g->translate((a[0] + b[0]) / 2, (a[1] + b[1]) / 2, (a[2] + b[2]) / 2);
  return g;
}

GeometryPtr cube(V3 size, V3 at, double rx = 0) {
  GeometryPtr g = boxGeometry(size[0], size[1], size[2]);
  if (rx != 0) g->rotateX(rx);
  g->translate(at[0], at[1], at[2]);
  return g;
}

struct Ring {
  double y = 0;
  double rx = 0;
  double rz = 0;
  double z = 0;
};

/** A body of elliptic rings, given from the lowest up; `inside`: the faces turned inwards (a lining). */
GeometryPtr loft(const std::vector<Ring>& rings, int sides, bool caps, bool inside = false) {
  std::vector<float> pos;
  std::vector<uint32_t> idx;
  for (const Ring& r : rings) {
    for (int k = 0; k < sides; k++) {
      const double a = (static_cast<double>(k) / sides) * P_PI * 2;
      pos.push_back(static_cast<float>(std::sin(a) * r.rx));
      pos.push_back(static_cast<float>(r.y));
      pos.push_back(static_cast<float>(r.z + std::cos(a) * r.rz));
    }
  }
  const uint32_t S = static_cast<uint32_t>(sides);
  for (uint32_t j = 0; j + 1 < rings.size(); j++) {
    for (uint32_t k = 0; k < S; k++) {
      const uint32_t a = j * S + k;
      const uint32_t b = j * S + ((k + 1) % S);
      if (inside) idx.insert(idx.end(), {a, b + S, b, a, a + S, b + S});
      else idx.insert(idx.end(), {a, b, b + S, a, b + S, a + S});
    }
  }
  if (caps) {
    for (const bool top : {false, true}) {
      const uint32_t j = top ? static_cast<uint32_t>(rings.size()) - 1 : 0;
      const Ring& r = rings[j];
      const uint32_t centre = static_cast<uint32_t>(pos.size() / 3);
      pos.push_back(0);
      pos.push_back(static_cast<float>(r.y));
      pos.push_back(static_cast<float>(r.z));
      for (uint32_t k = 0; k < S; k++) {
        const uint32_t a = j * S + k;
        const uint32_t b = j * S + ((k + 1) % S);
        if (top) idx.insert(idx.end(), {centre, a, b});
        else idx.insert(idx.end(), {centre, b, a});
      }
    }
  }
  GeometryPtr g = make<BufferGeometry>();
  g->setAttribute("position", std::make_shared<BufferAttribute>(std::move(pos), 3));
  g->setIndex(idx);
  g->computeVertexNormals();
  return g;
}

GeometryPtr merge(const std::vector<GeometryPtr>& parts) {
  GeometryPtr g = mergeGeometries(parts, false);
  if (!g) throw std::runtime_error("people: the pieces of a part do not fit together");
  g->computeBoundingSphere();
  return g;
}

/** The torso from the hip joints up to the neck, with a jacket, a hooded jacket, a coat and scarf, or a dress. */
GeometryPtr torsoShape() {
  const std::vector<Ring> upper = {
      {-0.03, 0.166, 0.106, 0}, {0.07, 0.158, 0.1, 0},        {0.18, 0.148, 0.095, 0}, {0.31, 0.167, 0.108, -0.012},
      {0.425, 0.177, 0.102, -0.005}, {0.49, 0.158, 0.088, 0}, {0.535, 0.1, 0.066, 0},  {0.56, 0.052, 0.048, 0},
  };
  const std::vector<Ring> coat = {{-0.41, 0.205, 0.2, -0.01}, {-0.2, 0.188, 0.15, 0}, {0.0, 0.17, 0.11, 0}, {0.08, 0.162, 0.103, 0}};
  const std::vector<Ring> skirt = {{-0.36, 0.2, 0.19, -0.01}, {-0.12, 0.178, 0.13, 0}, {0.06, 0.16, 0.104, 0}, {0.19, 0.15, 0.097, 0}};
  auto lining = [](std::vector<Ring> rings) {
    for (Ring& r : rings) {
      r.rx -= 0.006;
      r.rz -= 0.006;
    }
    return rings;
  };
  /** cloth over the thighs of somebody sitting: a flattened tube reaching forward, a little downhill */
  auto lap = [](double length, double y) {
    GeometryPtr g = cylinderGeometry(1, 1, length, 12);
    g->rotateX(P_PI / 2).scale(0.185, 0.09, 1).rotateX(-0.14).translate(0, y, -length / 2 + 0.03);
    return g;
  };
  return merge({
      pieceAll(loft(upper, 10, true), A),
      // the seat of the trousers below a jacket
      piece(loft({{-0.125, 0.128, 0.086, 0}, {-0.06, 0.15, 0.097, 0}, {0.0, 0.155, 0.099, 0}}, 10, true), B, {JACKET, HOODIE}),
      // the hood, lying on the back
      piece(ellipsoid({0.115, 0.075, 0.06}, {0, 0.5, 0.098}), A, {HOODIE}),
      // a coat down to the knees, its lining inside (seen from below), a scarf with an end hanging down the front
      piece(loft(coat, 10, false), A, {COAT}),
      piece(loft(lining(coat), 10, false, true), A, {COAT}, 0x8a8a8a),
      piece(loft({{0.47, 0.106, 0.088, 0}, {0.535, 0.098, 0.083, 0}, {0.62, 0.075, 0.07, 0}}, 10, false), B, {COAT, COAT_SITTING}),
      piece(cube({0.058, 0.22, 0.022}, {0.055, 0.39, -0.116}, 0.06), B, {COAT, COAT_SITTING}),
      // a dress: the skirt flares from the waist to above the knees
      piece(loft(skirt, 10, false), B, {DRESS}),
      piece(loft(lining(skirt), 10, false, true), B, {DRESS}, 0x8a8a8a),
      // sitting (the thighs reach forward 0.42 m, a little downhill): the coat round the hips, over the
      // thighs and down in front of the knees; the dress round the hips and over the thighs
      piece(loft({{-0.13, 0.173, 0.113, 0}, {0.0, 0.17, 0.11, 0}, {0.08, 0.162, 0.103, 0}}, 10, true), A, {COAT_SITTING}),
      piece(lap(0.5, -0.03), A, {COAT_SITTING}),
      piece(cube({0.36, 0.3, 0.035}, {0, -0.21, -0.495}), A, {COAT_SITTING}),
      piece(loft({{-0.12, 0.166, 0.107, 0}, {0.06, 0.16, 0.104, 0}, {0.19, 0.15, 0.097, 0}}, 10, true), B, {DRESS_SITTING}),
      piece(lap(0.38, -0.025), B, {DRESS_SITTING}),
  });
}

/** An arm from the shoulder joint, hanging down, a little bent at the elbow: in a long or a short sleeve. */
GeometryPtr armShape() {
  const V3 elbow = {0, -UPPER_ARM, 0};
  const V3 wrist = {0, -0.555, -0.055};
  return merge({
      pieceAll(ellipsoid({0.05, 0.052, 0.054}, {0, -0.03, 0}, 8, 6), A),
      piece(limb(0.052, 0.045, UPPER_ARM), A, {LONG_SLEEVE}),
      piece(ellipsoid({0.045, 0.045, 0.045}, elbow, 8, 6), A, {LONG_SLEEVE}),
      piece(tube(elbow, wrist, 0.044, 0.038), A, {LONG_SLEEVE}),
      piece(limb(0.057, 0.055, 0.15), A, {SHORT_SLEEVE}),
      piece(limb(0.041, 0.037, UPPER_ARM - 0.12, -0.12), B, {SHORT_SLEEVE}),
      piece(ellipsoid({0.038, 0.038, 0.038}, elbow, 8, 6), B, {SHORT_SLEEVE}),
      piece(tube(elbow, wrist, 0.036, 0.029), B, {SHORT_SLEEVE}),
      // the hand, along the forearm
      pieceAll(ellipsoid({0.026, 0.07, 0.044}, {0, -0.618, -0.07}, 8, 6, P_PI, 0.22), B),
  });
}

GeometryPtr thighShape() { return merge({pieceAll(ellipsoid({0.08, 0.08, 0.08}, {0, 0, 0}, 8, 6), A), pieceAll(limb(0.08, 0.06, THIGH + 0.02, 0, 9), A)}); }

/** The lower leg from the knee down, with a shoe (or a boot). */
GeometryPtr shinShape() {
  const double sole = -LOWER_LEG;
  return merge({
      pieceAll(ellipsoid({0.061, 0.061, 0.063}, {0, 0, 0}, 8, 6), A),
      pieceAll(limb(0.058, 0.042, LOWER_LEG - 0.04), A),
      piece(limb(0.062, 0.052, 0.27, -0.19), B, {BOOTS}),
      pieceAll(cube({0.086, 0.064, 0.25}, {0, sole + 0.032, (HEEL_Z + TOE_Z) / 2}), B),
      pieceAll(ellipsoid({0.042, 0.036, 0.07}, {0, sole + 0.044, -0.12}, 8, 5), B),
  });
}

/** The head on its neck (from the foot of the neck), with the hair styles and hats. */
GeometryPtr headShape() {
  const V3 skull = {0, 0.142, 0};
  std::vector<GeometryPtr> pieces = {
      pieceAll(limb(0.046, 0.05, 0.1, 0.075), A),
      pieceAll(ellipsoid({0.079, 0.114, 0.096}, skull, 12, 9), A),
      pieceAll(cube({0.022, 0.034, 0.03}, {0, 0.128, -0.094}), A),
  };
  for (const double side : {-1.0, 1.0}) {
    pieces.push_back(pieceAll(ellipsoid({0.012, 0.027, 0.019}, {side * 0.078, 0.138, 0.008}, 6, 4), A));
    pieces.push_back(pieceAll(cube({0.017, 0.01, 0.012}, {side * 0.03, 0.157, -0.085}), OWN, 0x2b2420));
    // long hair falls past the face on both sides
    pieces.push_back(piece(ellipsoid({0.022, 0.085, 0.05}, {side * 0.07, 0.1, 0.015}, 6, 5), B, {LONG}));
  }
  for (GeometryPtr g : {
           // short hair, also under the long hair, the bun and the ponytail: cut on a slant, so that it ends
           // 2 cm above the eyes at the front, over the ears at the sides and at the nape behind
           piece(ellipsoid({0.085, 0.121, 0.102}, {0, 0.15, 0.01}, 12, 7, P_PI * 0.567, 0.52), B, {SHORT, LONG, BUN, PONYTAIL}),
           piece(ellipsoid({0.083, 0.15, 0.048}, {0, 0.065, 0.066}, 10, 7), B, {LONG}),
           piece(ellipsoid({0.048, 0.045, 0.048}, {0, 0.215, 0.085}, 8, 6), B, {BUN}),
           piece(ellipsoid({0.032, 0.09, 0.032}, {0, 0.1, 0.12}, 7, 5, P_PI, 0.25), B, {PONYTAIL}),
           // a cap with a peak
           piece(ellipsoid({0.089, 0.092, 0.104}, {0, 0.168, 0.004}, 12, 5, P_PI * 0.5), B, {CAP}),
           piece(cube({0.15, 0.012, 0.09}, {0, 0.172, -0.13}, 0.12), B, {CAP}),
           // a woolly hat, pulled down at the back
           piece(ellipsoid({0.09, 0.135, 0.105}, {0, 0.148, 0.006}, 12, 6, P_PI * 0.5, 0.3), B, {BEANIE}),
           // a headscarf: over the crown, round the face, down over the neck
           piece(ellipsoid({0.091, 0.127, 0.107}, {0, 0.142, 0.006}, 14, 4, P_PI * 0.32), B, {HEADSCARF}),
           piece(ellipsoid({0.091, 0.127, 0.107}, {0, 0.142, 0.006}, 14, 9, P_PI * 0.8, 0, P_PI * 1.5 + 0.75, P_PI * 2 - 1.5), B, {HEADSCARF}),
           piece(loft({{-0.05, 0.15, 0.125, 0}, {0.0, 0.1, 0.092, 0}, {0.06, 0.083, 0.085, 0}}, 12, false), B, {HEADSCARF}),
           // the horseshoe of hair round the back of a balding head, from the temples down to the nape
           piece(ellipsoid({0.087, 0.119, 0.101}, skull, 12, 4, P_PI * 0.3, 0, P_PI / 2 - 1.75, 3.5, P_PI * 0.36), B, {BALDING}),
           // curls
           piece(ellipsoid({0.112, 0.11, 0.118}, {0, 0.17, 0.016}, 12, 7, P_PI * 0.6, 0.45), B, {CURLY}),
           // a flat cap
           piece(ellipsoid({0.095, 0.092, 0.115}, {0, 0.168, -0.008}, 12, 4, P_PI * 0.5), B, {FLAT_CAP}),
           piece(cube({0.16, 0.012, 0.06}, {0, 0.172, -0.118}), B, {FLAT_CAP}),
       })
    pieces.push_back(g);
  return merge(pieces);
}

/** What people carry, each in its own frame (see PeopleRenderer for where each one is held). */
GeometryPtr itemShape() {
  std::vector<GeometryPtr> pieces = {
      // a bag over the shoulder, hanging from its top at the hip
      piece(cube({0.07, 0.24, 0.3}, {0, -0.12, 0}), A, {SHOULDER_BAG}),
      piece(cube({0.076, 0.1, 0.306}, {0, -0.05, 0}), A, {SHOULDER_BAG}, 0xb4b4b4),
      // a backpack, hanging from its top
      piece(cube({0.3, 0.37, 0.14}, {0, -0.2, 0}), A, {BACKPACK}),
      piece(ellipsoid({0.15, 0.045, 0.07}, {0, -0.015, 0}, 10, 4, P_PI * 0.5), A, {BACKPACK}),
      piece(cube({0.22, 0.15, 0.04}, {0, -0.29, 0.085}), A, {BACKPACK}, 0xb4b4b4),
      // held in the hand (the origin is the grip): a handbag, a shopping bag, a briefcase
      piece(cube({0.1, 0.2, 0.27}, {0, -0.15, 0}), A, {HANDBAG}),
      piece(tube({0, -0.05, -0.07}, {0, 0.005, 0}, 0.008, 0.008, 4), B, {HANDBAG}),
      piece(tube({0, 0.005, 0}, {0, -0.05, 0.07}, 0.008, 0.008, 4), B, {HANDBAG}),
      piece(cube({0.12, 0.34, 0.3}, {0, -0.22, 0}), A, {SHOPPING_BAG}),
      piece(cube({0.124, 0.035, 0.304}, {0, -0.068, 0}), A, {SHOPPING_BAG}, 0xc8c8c8),
      piece(tube({0, -0.05, -0.08}, {0, 0.005, 0}, 0.007, 0.007, 4), B, {SHOPPING_BAG}),
      piece(tube({0, 0.005, 0}, {0, -0.05, 0.08}, 0.007, 0.007, 4), B, {SHOPPING_BAG}),
      piece(cube({0.085, 0.28, 0.4}, {0, -0.18, 0}), A, {BRIEFCASE}),
      piece(cube({0.03, 0.035, 0.11}, {0, -0.025, 0}), B, {BRIEFCASE}),
      // a suitcase on wheels, around the axle of its wheels, the handle pulled out
      piece(cube({0.37, 0.52, 0.22}, {0, 0.29, -0.09}), A, {SUITCASE}),
      piece(cube({0.376, 0.04, 0.226}, {0, 0.29, -0.09}), A, {SUITCASE}, 0xa0a0a0),
      piece(cube({0.24, 0.025, 0.03}, {0, 0.905, 0.01}), OWN, {SUITCASE}, 0x222326),
      // a walking stick, held at its crook, 0.8 m down to the rubber tip
      piece(tube({0, -0.8, 0}, {0, 0, 0}, 0.011, 0.012, 6), A, {STICK}),
      piece(tube({0, 0, 0}, {0, 0.015, -0.1}, 0.012, 0.012, 5), A, {STICK}),
      piece(limb(0.014, 0.014, 0.04, -0.76), OWN, {STICK}, 0x1a1a1a),
      // an umbrella, from its handle (the origin) up the shaft to the canopy, and the canopy's underside
      piece(tube({0, -0.1, 0}, {0, 1.05, 0}, 0.009, 0.009, 5), OWN, {UMBRELLA}, 0x2b2b2e),
      piece(loft({{0.86, 0.5, 0.5, 0}, {0.97, 0.33, 0.33, 0}, {1.05, 0.03, 0.03, 0}}, 8, false), A, {UMBRELLA}),
      piece(loft({{0.855, 0.495, 0.495, 0}, {0.965, 0.325, 0.325, 0}, {1.045, 0.025, 0.025, 0}}, 8, false, true), A, {UMBRELLA}, 0x8a8a8a),
  };
  for (const double side : {-1.0, 1.0}) {
    // the strap of the shoulder bag runs from the left shoulder across chest and back to the right hip (frame of the torso)
    pieces.push_back(piece(tube({-0.11, 0.53, side * 0.02}, {-0.02, 0.365, side * 0.12}, 0.012, 0.012, 4), B, {STRAP}));
    pieces.push_back(piece(tube({-0.02, 0.365, side * 0.12}, {0.19, 0.14, side * 0.09}, 0.012, 0.012, 4), B, {STRAP}));
    // the straps of the backpack over the shoulders (frame of the torso)
    pieces.push_back(piece(tube({side * 0.09, 0.535, 0.03}, {side * 0.1, 0.43, -0.112}, 0.014, 0.014, 4), B, {BACKPACK_STRAPS}));
    pieces.push_back(piece(tube({side * 0.1, 0.43, -0.112}, {side * 0.12, 0.22, -0.1}, 0.014, 0.014, 4), B, {BACKPACK_STRAPS}));
    // the wheels and the pulled-out rods of the suitcase
    GeometryPtr wheel = cylinderGeometry(CASE_WHEEL, CASE_WHEEL, 0.03, 8);
    wheel->rotateZ(P_PI / 2).translate(side * 0.14, 0, 0);
    pieces.push_back(piece(wheel, OWN, {SUITCASE}, 0x1c1c1e));
    pieces.push_back(piece(tube({side * 0.1, 0.55, 0.01}, {side * 0.1, 0.9, 0.01}, 0.008, 0.008, 5), OWN, {SUITCASE}, 0x8c9094));
  }
  return merge(pieces);
}

struct Shapes {
  GeometryPtr thigh;
  GeometryPtr shin;
  GeometryPtr torso;
  GeometryPtr arm;
  GeometryPtr head;
  GeometryPtr item;
};

Shapes& shapes() {
  static Shapes s{thighShape(), shinShape(), torsoShape(), armShape(), headShape(), itemShape()};
  return s;
}

// ------------------------------------------------------------------------------------ material
// Per vertex: aSlot (the colour slot) and aMask (the variants the piece belongs to); per instance:
// aColA, aColB (the two colours of the part for this person) and aVar (the variant the person wears).
// Pieces of the variants the person does not wear shrink to a point: no pixels.  (The shader is the
// original's, captured: tools/glsl/people.mjs.)
const MaterialPtr& BODY() {
  static MaterialPtr m = [] {
    MaterialPtr mat = lambert({.vertexColors = true});
    mat->patch = gamePatch("people");
    return mat;
  }();
  return m;
}

// ------------------------------------------------------------------------------------ looks
using List = std::vector<uint32_t>;
const List MUTED = {0x2b3a55, 0x3a3d42, 0x55603f, 0x5e4433, 0x7d6a55, 0x6b2f37, 0x6f747a, 0x222326, 0x2f4a3c, 0x4a5a6e};
const List BRIGHT = {0xc0392b, 0xe0a91f, 0xd96a24, 0x1f8a83, 0x2f62c4, 0xc95683, 0x7fae3a, 0x6b4aa0, 0xe6e4dc, 0x38a0d8};
const List COATS = {0xa8835a, 0x2e3440, 0x6b6f75, 0x2b3a55, 0x7a3b3b, 0xb9ad98, 0x3f4b3a, 0x222326};
const List SCARVES = {0x8a2e2e, 0x6f747a, 0x2f4a6e, 0xc9b38a, 0x3d5a40, 0xd9d4c6, 0xb5652e, 0x5b3a6e};
const List JEANS = {0x34496b, 0x2c3b57, 0x485f82, 0x23262b};
const List TROUSERS = {0x26282c, 0x3d4045, 0x5a5d62, 0x8c7f68, 0x473a2e, 0x2b3240};
const List TIGHTS = {0x1e1e22, 0x2b2b30, 0x4a3a33, 0xb08a74};
const List SKIN = {0xf0c8a8, 0xe3b08e, 0xd29c78, 0xb27a56, 0x8c5a3c, 0x5e3d2a};
const List HAIR = {0x1c1917, 0x33251b, 0x55392a, 0x7a5532, 0xb08a52, 0xd4b77a, 0x8a3f22, 0x1c1917, 0x33251b};
const List GREY_HAIR = {0x9c9c9a, 0xbdbdb8, 0xdedcd6};
const List SHOE_COLOURS = {0x19191b, 0x3f2c20, 0x5a5c60, 0x19191b};
const List TRAINERS = {0xe6e6e2, 0xd2d2ce, 0x2a2c30, 0x9a3a32, 0xe6e6e2};
const List BAGS = {0x2a2a2c, 0x5e3d28, 0x8a6a48, 0x3a4a5e, 0x7a2e30, 0x5a5f55, 0x1f1f21};

List cat(std::initializer_list<List> lists) {
  List out;
  for (const List& l : lists) out.insert(out.end(), l.begin(), l.end());
  return out;
}

/** A kind of person: the choices for each part (one is picked per person). */
struct Kind {
  std::vector<int> torso;
  std::optional<int> sleeve;
  std::vector<int> hair;
  List top;
  List legs;
  /** the colour of the hair, or of the hat or scarf on it (default: natural hair colours) */
  std::optional<List> hairs;
  List shoes;
  /** the second colour of the torso: the scarf to a coat, the skirt of a dress (default: the legs) */
  std::optional<List> second;
  /** the share of them in boots */
  double boots = 0;
  std::vector<int> items;
  std::optional<List> bags;
  std::array<double, 2> height;
  /** '' | 'child' | 'old' | 'woman' */
  std::string build;
};

const std::vector<Kind>& KINDS() {
  static const std::vector<Kind> k = {
      // a man in a short jacket and jeans, a bag over his shoulder
      {{JACKET}, {}, {SHORT, SHORT, BALDING}, MUTED, JEANS, {}, SHOE_COLOURS, {}, 0, {SHOULDER_BAG, NOTHING}, {}, {1.72, 1.9}, ""},
      // a woman in a long coat, with long hair and a handbag
      {{COAT}, {}, {LONG, LONG, BUN}, COATS, cat({JEANS, TIGHTS}), {}, SHOE_COLOURS, SCARVES, 0.5, {HANDBAG, SHOULDER_BAG}, {}, {1.6, 1.76}, "woman"},
      // a student in a hooded jacket with a backpack and white trainers
      {{HOODIE}, {}, {SHORT, BEANIE, SHORT, CURLY}, cat({BRIGHT, {0x6f747a, 0x3a3d42}}), JEANS, cat({HAIR, {0x2b3a55, 0x6f747a}}), TRAINERS, {}, 0, {BACKPACK}, {}, {1.65, 1.88}, ""},
      // an older woman in a coat, her grey hair in a bun, with a walking stick
      {{COAT}, {}, {BUN, SHORT}, {0x6e5a4a, 0x4a5568, 0x7a6e60, 0x5b4a5e, 0x8a7a68}, {0x3d4045, 0x4a3a33, 0x5a5d62}, GREY_HAIR, SHOE_COLOURS, SCARVES, 0, {STICK}, {}, {1.55, 1.66}, "old"},
      // an older man in a jacket and a flat cap, with a shopping bag or a stick
      {{JACKET}, {}, {FLAT_CAP}, {0x5e5546, 0x4a5240, 0x3d4652, 0x6b6258}, {0x5a5d62, 0x8c7f68, 0x473a2e}, List{0x4a4440, 0x5d564c, 0x3a3c40, 0x6b5a48}, SHOE_COLOURS, {}, 0, {SHOPPING_BAG, STICK, NOTHING}, {}, {1.66, 1.8}, "old"},
      // a woman in a dress, tights and boots, with a handbag
      {{DRESS}, {}, {SHORT, PONYTAIL, LONG}, cat({BRIGHT, MUTED}), TIGHTS, {}, SHOE_COLOURS, cat({MUTED, {0x6b2f37, 0x2b3a55}}), 0.6, {HANDBAG, SHOULDER_BAG, NOTHING}, {}, {1.58, 1.74}, "woman"},
      // a schoolchild in a bright jacket with a school bag
      {{JACKET}, {}, {SHORT, PONYTAIL, SHORT, BEANIE}, BRIGHT, JEANS, {}, TRAINERS, {}, 0, {BACKPACK}, BRIGHT, {1.25, 1.5}, "child"},
      // a teenager with a cap
      {{HOODIE, JACKET}, {}, {CAP}, BRIGHT, cat({JEANS, {0x26282c}}), cat({BRIGHT, {0x222326}}), TRAINERS, {}, 0, {BACKPACK, NOTHING}, {}, {1.6, 1.82}, ""},
      // a commuter in a dark coat with a scarf and a briefcase (or a suitcase)
      {{COAT}, {}, {SHORT, BALDING}, {0x2e3440, 0x222326, 0x3a3d42, 0x2b3a55, 0x6b6f75}, TROUSERS, {}, SHOE_COLOURS, SCARVES, 0, {BRIEFCASE, BRIEFCASE, SUITCASE}, {}, {1.72, 1.92}, ""},
      // a woman with a headscarf and a long coat
      {{COAT}, {}, {HEADSCARF}, {0x2e3440, 0x5e4433, 0x6b6f75, 0x3f4b3a, 0x222326, 0x8a7a68}, cat({TROUSERS, TIGHTS}), List{0x2b3a55, 0x6b2f37, 0xc9b38a, 0x3f4b3a, 0x7a6a8a, 0x1f1f22, 0xb5652e}, SHOE_COLOURS, {}, 0, {SHOPPING_BAG, HANDBAG}, {}, {1.58, 1.72}, "woman"},
      // a man in a bright rain jacket with a backpack
      {{JACKET}, {}, {CAP, SHORT, BEANIE}, {0xe0a91f, 0xd96a24, 0x7fae3a, 0xc0392b, 0x2f62c4, 0x1f8a83}, cat({JEANS, TROUSERS}), List{0x222326, 0x2b3a55, 0x6f747a, 0x55392a}, cat({SHOE_COLOURS, TRAINERS}), {}, 0, {BACKPACK}, {}, {1.7, 1.9}, ""},
      // a small child in a bright jacket and a woolly hat
      {{JACKET}, {}, {BEANIE, SHORT}, BRIGHT, cat({JEANS, {0xc95683, 0x2f62c4}}), BRIGHT, TRAINERS, {}, 0, {NOTHING, NOTHING, BACKPACK}, BRIGHT, {1.05, 1.25}, "child"},
      // a woman in a sports jacket and leggings, with a ponytail
      {{JACKET}, {}, {PONYTAIL}, cat({BRIGHT, {0x3a3d42}}), {0x1e1e22, 0x2b2b30, 0x3a3d48}, {}, TRAINERS, {}, 0, {BACKPACK, SHOULDER_BAG}, {}, {1.6, 1.75}, "woman"},
      // a man in a T-shirt and jeans
      {{JACKET}, SHORT_SLEEVE, {SHORT, BALDING, CURLY}, {0xe6e4dc, 0x2f62c4, 0x222326, 0x7a8a9a, 0xc0392b, 0x55603f}, JEANS, {}, cat({TRAINERS, SHOE_COLOURS}), {}, 0, {NOTHING, SHOULDER_BAG}, {}, {1.72, 1.9}, ""},
      // a businesswoman in a long coat, her hair in a bun, with a briefcase
      {{COAT}, {}, {BUN, SHORT}, {0xb9ad98, 0x2e3440, 0x6b6f75, 0xa8835a}, TIGHTS, {}, SHOE_COLOURS, List{0xe6e4dc, 0x8a2e2e, 0x2f4a6e}, 0, {BRIEFCASE, HANDBAG}, {}, {1.62, 1.76}, "woman"},
      // somebody with curly hair in a bright jacket, a shopping bag in hand
      {{JACKET}, {}, {CURLY}, BRIGHT, cat({JEANS, TROUSERS}), {}, TRAINERS, {}, 0, {SHOPPING_BAG, NOTHING}, {}, {1.62, 1.85}, ""},
      // a traveller pulling a suitcase
      {{JACKET, HOODIE}, {}, {SHORT, LONG, CAP}, cat({MUTED, BRIGHT}), JEANS, cat({HAIR, {0x222326}}), cat({TRAINERS, SHOE_COLOURS}), {}, 0, {SUITCASE}, {}, {1.62, 1.88}, ""},
  };
  return k;
}

/** How far a knee is bent at a point of the walking cycle (0: the leg straight up, π/2: it strikes the ground ahead). */
double kneeBend(double phase) {
  const double w = phase - std::floor(phase / (P_PI * 2)) * P_PI * 2;
  const double swing = std::max(0.0, std::cos(w + 0.5));
  double k = 0.06 + swing * std::sqrt(swing);
  // a little give as the weight comes on to the leg
  if (w > P_PI / 2 && w < P_PI) k += 0.16 * std::sin((w - P_PI / 2) * 2);
  return k;
}

/** The lowest point of a foot below the hip joint, for the angle at the hip (forwards) and the bend of the knee. */
double lowestFoot(double hip, double knee) {
  const double a = hip - knee;
  const double ca = std::cos(a);
  const double sa = std::sin(a);
  return -THIGH * std::cos(hip) + std::min(-LOWER_LEG * ca - HEEL_Z * sa, -LOWER_LEG * ca - TOE_Z * sa);
}

Quaternion _q;
Euler _e;
Vector3 _p;
Vector3 _s;
Matrix4 _local;
Vector3 _v;
const Matrix4 IDENTITY;

/**
 * out = parent · translate(x, y, z) · rotate (yaw ry, then pitch rx, then roll rz) · scale.
 * (A positive pitch tips the top backwards and swings a hanging limb forwards.)
 */
Matrix4& joint(Matrix4& out, const Matrix4& parent, double x, double y, double z, double rx, double ry, double rz, double sx = 1, double sy = -1, double sz = -1) {
  if (sy < 0) sy = sx;
  if (sz < 0) sz = sx;
  _e.set(rx, ry, rz, Euler::YXZ);
  _local.compose(_p.set(x, y, z), _q.setFromEuler(_e), _s.set(sx, sy, sz));
  return out.multiplyMatrices(parent, _local);
}

}  // namespace

Look lookOf(int id) {
  auto r = [id](int k) { return hash01(jsImul(id, 0x2c1b3c6d) ^ jsImul(k + 1, 0x297a2d39)); };
  auto pickI = [&](const std::vector<int>& list, int k) { return list[std::min(list.size() - 1, static_cast<size_t>(std::floor(r(k) * static_cast<double>(list.size()))))]; };
  auto pickC = [&](const List& list, int k) { return list[std::min(list.size() - 1, static_cast<size_t>(std::floor(r(k) * static_cast<double>(list.size()))))]; };
  const std::vector<Kind>& kinds = KINDS();
  const Kind& kind = kinds[std::min(kinds.size() - 1, static_cast<size_t>(std::floor(r(0) * static_cast<double>(kinds.size()))))];
  const int torso = pickI(kind.torso, 1);
  const int hair = pickI(kind.hair, 2);
  const double height = kind.height[0] + (kind.height[1] - kind.height[0]) * r(3);
  const double scale = height / HEIGHT;
  const bool child = kind.build == "child";
  const bool old = kind.build == "old";
  const bool woman = kind.build == "woman";
  const Color legs(pickC(kind.legs, 4));
  const int item = pickI(kind.items, 5);
  const double stride = (old ? 1.02 : child ? 1.36 : 1.42) * (0.95 + 0.1 * r(6));
  const bool wrapped = torso == COAT || torso == DRESS;
  const bool backpackish = item == BACKPACK || item == SHOULDER_BAG;
  const List bagList = kind.bags ? *kind.bags : backpackish ? cat({BAGS, List(BRIGHT.begin(), BRIGHT.begin() + 6)}) : BAGS;
  const Color bag(pickC(bagList, 7));
  Look L;
  L.id = id;
  L.scale = scale;
  L.head = 1.06 + (child ? 0.6 * std::max(0.0, 1 - scale) : 0);
  L.wide = (woman ? 0.93 : 1) * (0.95 + 0.1 * r(8));
  L.deep = 0.94 + 0.14 * r(9);
  L.torso = torso;
  L.sleeve = kind.sleeve ? *kind.sleeve : LONG_SLEEVE;
  L.hair = hair;
  L.shoe = r(10) < kind.boots ? BOOTS : SHOES;
  L.top = Color(pickC(kind.top, 11));
  L.second = torso == JACKET || torso == HOODIE ? legs.clone() : Color(pickC(kind.second ? *kind.second : kind.top, 12));
  L.skin = Color(pickC(SKIN, 13));
  L.hairColour = Color(pickC(kind.hairs ? *kind.hairs : (old ? GREY_HAIR : HAIR), 14));
  L.legs = legs;
  L.shoes = Color(pickC(kind.shoes, 15));
  L.bag = bag;
  L.bag2 = item == BACKPACK || item == SHOULDER_BAG ? Color(0x1e1f21) : bag.clone().multiplyScalar(0.55);
  L.item = item;
  L.stride = stride;
  L.swing = (old ? 0.13 : child ? 0.36 : 0.29) * (0.85 + 0.3 * r(16));
  L.hipSwing = std::asin(std::min(0.9, stride / (4 * 0.91))) * (wrapped ? 0.82 : 0.95);
  L.stoop = old ? 0.1 + 0.06 * r(17) : 0.02 * r(17);
  L.seed = r(18);
  L.bow = !child && !old && r(19) < 0.25 ? 0.38 : 0;
  return L;
}

Look workerLook(int id, const std::string& vest) {
  Look L = lookOf(id);
  const Color top(vest == "yellow" ? 0xd7ec2c : 0xff7a18);
  const Color legs(0x2a3244);
  const bool cap = L.seed < 0.5;
  L.torso = JACKET;
  L.sleeve = LONG_SLEEVE;
  L.hair = cap ? CAP : SHORT;
  L.shoe = BOOTS;
  L.top = top;
  L.second = legs.clone();
  L.legs = legs;
  L.shoes = Color(0x1b1b1d);
  if (cap) L.hairColour = Color(0x26324a);
  L.item = NOTHING;
  L.bow = 0;
  L.stoop = 0.02;
  return L;
}

Look roleLook(int id, int seed, const std::string& role) {
  if (role == "police") {
    Look L = lookOf(seed);
    const Color legs(0x1c2434);
    L.id = id;
    L.torso = JACKET;
    L.sleeve = LONG_SLEEVE;
    L.hair = CAP;
    L.shoe = BOOTS;
    L.top = Color(0x24375a);
    L.second = legs.clone();
    L.legs = legs;
    L.shoes = Color(0x141416);
    L.hairColour = Color(0xf0f0ec);
    L.item = NOTHING;
    L.bow = 0;
    L.stoop = 0.01;
    return L;
  }
  // the first old woman (in a coat) among the looks from `seed` on
  int k = seed;
  while (k < seed + 4000 && !(lookOf(k).stoop >= 0.1 && lookOf(k).torso == COAT)) k++;
  Look L = lookOf(k);
  L.id = id;
  L.item = NOTHING;
  L.bow = 0;
  return L;
}

// ------------------------------------------------------------------------------------ renderer
class PeopleRenderer::Part {
 public:
  std::shared_ptr<InstancedMesh> mesh;

  Part(const GeometryPtr& shape, int count, const std::string& name) {
    // (the shape's attributes are shared between renderers; the per-person ones are this renderer's own)
    GeometryPtr g = make<BufferGeometry>();
    for (const auto& [key, attribute] : shape->attributes) g->setAttribute(key, attribute);
    g->setIndex(shape->index);
    g->boundingSphere = shape->boundingSphere;
    colA = std::make_shared<BufferAttribute>(std::vector<float>(static_cast<size_t>(count) * 3), 3);
    colB = std::make_shared<BufferAttribute>(std::vector<float>(static_cast<size_t>(count) * 3), 3);
    variant = std::make_shared<BufferAttribute>(std::vector<float>(static_cast<size_t>(count)), 1);
    for (const AttrPtr& a : {colA, colB, variant}) {
      a->divisor = 1;
      a->dynamic = true;
    }
    g->setAttribute("aColA", colA);
    g->setAttribute("aColB", colB);
    g->setAttribute("aVar", variant);
    mesh = make<InstancedMesh>(g, BODY(), count);
    mesh->instanceMatrix->dynamic = true;
    // (the original folds the pieces away in the shadow pass too, with a depth material of its own – which this
    //  renderer does not take: without it the shadow would show every variant at once, so the people cast none)
    mesh->castShadow = false;
    mesh->receiveShadow = true;
    mesh->count = 0;
    mesh->visible = false;
    mesh->name = name;
  }

  void paint(int i, const Color& a, const Color& b, int v) {
    colA->setXYZ(static_cast<size_t>(i), a.r, a.g, a.b);
    colB->setXYZ(static_cast<size_t>(i), b.r, b.g, b.b);
    variant->setX(static_cast<size_t>(i), v);
    painted = true;
  }

  /** After the frame's matrices: how many instances are shown, and the sphere around all of them. */
  void show(int count, const Sphere& bounds) {
    InstancedMesh& m = *mesh;
    m.count = count;
    m.visible = count > 0;
    m.boundingSphere = bounds;
    if (count > 0) m.instanceMatrix->needsUpdate();
    if (painted) {
      colA->needsUpdate();
      colB->needsUpdate();
      variant->needsUpdate();
      painted = false;
    }
  }

  void dispose() {
    mesh->geometry->dispose();
    mesh->dispose();
  }

 private:
  AttrPtr colA;
  AttrPtr colB;
  AttrPtr variant;
  bool painted = false;
};

PeopleRenderer::PeopleRenderer(int capacity_) {
  capacity = std::max(1, capacity_);
  const int n = capacity;
  Shapes& s = shapes();
  thigh = std::make_unique<Part>(s.thigh, n * 2, "people: thighs");
  shin = std::make_unique<Part>(s.shin, n * 2, "people: legs");
  torso = std::make_unique<Part>(s.torso, n, "people: torsos");
  arm = std::make_unique<Part>(s.arm, n * 2, "people: arms");
  head = std::make_unique<Part>(s.head, n, "people: heads");
  // (a bag and its strap are two items – and an umbrella a third)
  item = std::make_unique<Part>(s.item, n * 3, "people: things they carry");
  slots.assign(static_cast<size_t>(n), nullptr);
  torsoShown.assign(static_cast<size_t>(n), -1);
  itemLooks.assign(static_cast<size_t>(n) * 3, nullptr);
  itemKinds.assign(static_cast<size_t>(n) * 3, -1);
  group->name = "people";
  for (Part* p : {thigh.get(), shin.get(), torso.get(), arm.get(), head.get(), item.get()}) group->add(p->mesh);
}

PeopleRenderer::~PeopleRenderer() = default;

void PeopleRenderer::update(const std::vector<PersonPose>& people, double time) {
  const int n = std::min(static_cast<int>(people.size()), capacity);
  int items = 0;
  lo.set(JS_INF, JS_INF, JS_INF);
  hi.set(-JS_INF, -JS_INF, -JS_INF);
  for (int i = 0; i < n; i++) {
    const PersonPose& p = people[static_cast<size_t>(i)];
    std::shared_ptr<const Look> look = slots[static_cast<size_t>(i)];
    const bool fresh = !look || look->id != p.look;
    if (!look || fresh) {
      look = lookFor(p.look);
      slots[static_cast<size_t>(i)] = look;
      thigh->paint(i * 2, look->legs, look->legs, 0);
      thigh->paint(i * 2 + 1, look->legs, look->legs, 0);
      shin->paint(i * 2, look->legs, look->shoes, look->shoe);
      shin->paint(i * 2 + 1, look->legs, look->shoes, look->shoe);
      arm->paint(i * 2, look->top, look->skin, look->sleeve);
      arm->paint(i * 2 + 1, look->top, look->skin, look->sleeve);
      head->paint(i, look->skin, look->hairColour, look->hair);
    }
    // (a coat or a dress lies differently on somebody sitting)
    const bool sit = p.sit.value_or(false);
    const int t = sit ? (look->torso == COAT ? COAT_SITTING : look->torso == DRESS ? DRESS_SITTING : look->torso) : look->torso;
    if (fresh || torsoShown[static_cast<size_t>(i)] != t) {
      torso->paint(i, look->top, look->second, t);
      torsoShown[static_cast<size_t>(i)] = static_cast<int8_t>(t);
    }
    items = pose(i, p, look, time, items);
    lo.min(_v.set(p.x, p.y, p.z));
    hi.max(_v);
  }
  if (n > 0) {
    bounds.center.addVectors(lo, hi).multiplyScalar(0.5);
    bounds.radius = lo.distanceTo(hi) / 2 + 2.5;
  } else {
    bounds.center.set(0, 0, 0);
    bounds.radius = 0;
  }
  thigh->show(n * 2, bounds);
  shin->show(n * 2, bounds);
  torso->show(n, bounds);
  arm->show(n * 2, bounds);
  head->show(n, bounds);
  item->show(items, bounds);
}

void PeopleRenderer::dispose() {
  for (Part* p : {thigh.get(), shin.get(), torso.get(), arm.get(), head.get(), item.get()}) p->dispose();
  group->clear();
}

void PeopleRenderer::wear(const Look& look) { cache[look.id] = std::make_shared<const Look>(look); }

std::shared_ptr<const Look> PeopleRenderer::lookFor(int id) {
  auto it = cache.find(id);
  if (it != cache.end()) return it->second;
  if (cache.size() > 4096) cache.clear();
  std::shared_ptr<const Look> look = std::make_shared<const Look>(lookOf(id));
  cache[id] = look;
  return look;
}

int PeopleRenderer::pose(int i, const PersonPose& p, const std::shared_ptr<const Look>& look, double time, int k) {
  const Look& L = *look;
  const double s = L.scale;
  const double t = time + L.seed * 97;
  double hipR = 0;
  double kneeR = 0;
  double hipL = 0;
  double kneeL = 0;
  double splay = 0.03;
  double aR = 0.04;
  double aL = 0.04;
  double outR = 0.07;
  double outL = 0.07;
  double lean = L.stoop;
  double roll = 0;
  double twist = 0;
  double shift = 0;
  // waiting people look about now and then, some read
  double headYaw = 0.55 * std::sin(t * 0.19) * std::sin(t * 0.053 + L.seed * 3);
  double headBow = L.bow + 0.05 * std::sin(t * 0.27);
  double hip;
  const bool sitting = p.sit.value_or(false);
  const bool walking = !sitting && p.walk >= 0;
  const bool inHand = L.item == HANDBAG || L.item == SHOPPING_BAG || L.item == BRIEFCASE;
  if (sitting) {
    // the hips on the seat, the thighs along it, the feet on the ground (children's dangle)
    hip = SEAT_HEIGHT / s + 0.095;
    const double lift = 1.42;
    const double reach = (hip - THIGH * std::cos(lift)) / LOWER_LEG;
    const double forward = reach >= 1 ? 0 : std::acos(reach);
    hipR = lift;
    hipL = lift;
    kneeR = lift - forward - 0.12 * L.seed;
    kneeL = lift - forward;
    splay = 0.09;
    aR = 0.62;
    aL = 0.62;
    outR = -0.1;
    outL = -0.1;
    lean = -0.04 + L.stoop * 0.5;
  } else if (walking) {
    // the gait follows the distance walked: a double step per stride
    const double phase = (p.walk / s / L.stride) * P_PI * 2 + L.seed * 6.283;
    const double sn = std::sin(phase);
    const double cs = std::cos(phase);
    hipR = 0.02 + L.hipSwing * sn;
    hipL = 0.02 - L.hipSwing * sn;
    kneeR = kneeBend(phase);
    kneeL = kneeBend(phase + P_PI);
    aR = 0.05 - L.swing * sn;
    aL = 0.05 + L.swing * sn * (inHand ? 0.5 : 1);
    if (L.item == SUITCASE) {
      // the right hand pulls the suitcase behind
      aR = -0.5;
      outR = 0.14;
    }
    lean = 0.045 + L.stoop;
    twist = -0.05 * sn;
    roll = 0.02 * cs;
    shift = -0.018 * cs;
    headYaw = -twist * 0.8;
    headBow = 0.04;
    hip = -std::min(lowestFoot(hipR, kneeR), lowestFoot(hipL, kneeL));
  } else {
    // standing: the weight now on one leg, now on the other (the free knee a little bent, its foot a
    // little forward), the arms hanging loosely; the leg that carries is straight
    const double weight = std::sin(t * 0.31) * 0.75 + std::sin(t * 0.083 + 1.3) * 0.25;
    const double easeL = std::max(0.0, weight);
    const double easeR = std::max(0.0, -weight);
    hipL = 0.12 * easeL;
    kneeL = 0.15 * easeL;
    hipR = 0.12 * easeR;
    kneeR = 0.15 * easeR;
    shift = 0.03 * weight;
    roll = -0.025 * weight;
    aR = 0.03 + 0.025 * std::sin(t * 0.7);
    aL = 0.03 + 0.025 * std::sin(t * 0.7 + 2);
    hip = THIGH + LOWER_LEG;
  }
  // the people of the town (sim/pedestrians): looking out for the traffic, a start, in a hurry, an umbrella
  if (p.head && !sitting) {
    const double ph = *p.head;
    headYaw = walking ? headYaw + ph : ph;
    // (a look to the side lifts the head – and turns the shoulders a little with it)
    if (!walking && std::abs(ph) > 0.25) headBow = std::min(headBow, 0.05);
    twist += ph * 0.15;
  }
  if (p.lean && *p.lean != 0) lean += *p.lean;
  const double start = sitting ? 0 : p.startle.value_or(0);
  if (start > 0.01) {
    aR += 0.9 * start;
    aL += 0.9 * start;
    outR += 0.2 * start;
    outL += 0.2 * start;
    lean -= 0.14 * start;
    headBow -= 0.12 * start;
  }
  const bool brolly = p.umbrella.value_or(false) && !sitting && L.item != STICK && L.item != SUITCASE;
  if (brolly) {
    // (the right hand holds it up in front)
    aR = 0.62;
    outR = 0.08;
  }
  if (p.arms && !sitting) {
    aR = (*p.arms)[0];
    outR = (*p.arms)[1];
    aL = (*p.arms)[2];
    outL = (*p.arms)[3];
  }

  // ---- the frames, in the person's own measure: the world matrix scales them to the height
  world.compose(_p.set(p.x, p.y, p.z), _q.setFromAxisAngle(UP, p.yaw), _s.set(s, s, s));
  joint(base, IDENTITY, 0, hip, 0, 0, 0, 0);
  joint(body, base, shift, 0, 0, -lean, twist, roll);
  joint(armR, body, SHOULDER_X * L.wide, SHOULDER_Y, 0, aR, 0, outR);
  joint(armL, body, -SHOULDER_X * L.wide, SHOULDER_Y, 0, aL, 0, -outL);
  joint(thighR, base, HIP_X, 0, 0, hipR, 0, splay);
  joint(thighL, base, -HIP_X, 0, 0, hipL, 0, -splay);
  put(*torso, i, joint(m, body, 0, 0, 0, 0, 0, 0, L.wide, 1, L.deep));
  put(*head, i, joint(m, body, 0, NECK_Y, 0, lean * 0.8 - headBow, headYaw, 0, L.head));
  put(*arm, i * 2, armR);
  put(*arm, i * 2 + 1, armL);
  put(*thigh, i * 2, thighR);
  put(*thigh, i * 2 + 1, thighL);
  put(*shin, i * 2, joint(m, thighR, 0, -THIGH, 0, -kneeR, 0, 0));
  put(*shin, i * 2 + 1, joint(m, thighL, 0, -THIGH, 0, -kneeL, 0, 0));

  // ---- what they carry (bags hang from their top, things in the hand from the grip)
  const int kinds = static_cast<int>(itemKinds.size());
  // (the umbrella from the right hand, its top tipped back over the head)
  if (brolly && k < kinds) k = carry(k, look, UMBRELLA, joint(m, body, 0.18 * L.wide, 0.02, -0.42, 0.22, 0, 0));
  if (L.item == NOTHING || k + 2 > kinds) return k;
  switch (L.item) {
    case SHOULDER_BAG:
      if (sitting) return carry(k, look, SHOULDER_BAG, putDown(0.24));
      k = carry(k, look, SHOULDER_BAG, joint(m, body, 0.205 * L.wide, 0.145, 0.01, 0, 0, 0.05));
      return carry(k, look, STRAP, joint(m, body, 0, 0, 0, 0, 0, 0, L.wide, 1, L.deep));
    case BACKPACK:
      if (sitting) return carry(k, look, BACKPACK, putDown(0.385));
      k = carry(k, look, BACKPACK, joint(m, body, 0, 0.5, 0.103 * L.deep + 0.07, 0, 0, 0));
      return carry(k, look, BACKPACK_STRAPS, joint(m, body, 0, 0, 0, 0, 0, 0, L.wide, 1, L.deep));
    case HANDBAG:
      // (sitting: on the seat beside)
      if (sitting) return carry(k, look, HANDBAG, joint(m, base, -0.45, -0.095 + 0.25, 0.02, 0, 0.2, 0));
      return carry(k, look, HANDBAG, joint(m, armL, GRIP.x, GRIP.y, GRIP.z, 0, 0, 0));
    case SHOPPING_BAG:
    case BRIEFCASE:
      if (sitting) return carry(k, look, L.item, putDown(L.item == BRIEFCASE ? 0.32 : 0.39));
      return carry(k, look, L.item, joint(m, armL, GRIP.x, GRIP.y, GRIP.z, 0, 0, 0));
    case SUITCASE: {
      if (!walking) return carry(k, look, SUITCASE, joint(m, IDENTITY, sitting ? 0.45 : 0.36, CASE_WHEEL, sitting ? -0.3 : 0.04, 0, sitting ? 0.25 : 0, 0));
      // pulled behind on its wheels, tipped so that the handle is in the hand
      _v.copy(GRIP).applyMatrix4(armR);
      const double grip = 0.905;
      const double tip = std::acos(std::min(0.99, std::max(0.3, (_v.y - CASE_WHEEL) / grip)));
      return carry(k, look, SUITCASE, joint(m, IDENTITY, _v.x, CASE_WHEEL, _v.z + grip * std::sin(tip), -tip, 0, 0));
    }
    case STICK:
      if (sitting) return carry(k, look, STICK, joint(m, IDENTITY, 0.34, 0.78, -0.3, 0.25, 0, 0));
      // upright from the hand to the ground
      _v.copy(GRIP).applyMatrix4(armR);
      return carry(k, look, STICK, joint(m, IDENTITY, _v.x, _v.y, _v.z, 0, 0, 0, 1, std::max(0.3, _v.y / 0.8), 1));
    default:
      break;
  }
  return k;
}

void PeopleRenderer::put(Part& part, int index, const Matrix4& local) { part.mesh->setMatrixAt(index, out.multiplyMatrices(world, local)); }

int PeopleRenderer::carry(int k, const std::shared_ptr<const Look>& L, int kind, const Matrix4& local) {
  const size_t kk = static_cast<size_t>(k);
  if (itemLooks[kk] != L || itemKinds[kk] != kind) {
    itemLooks[kk] = L;
    itemKinds[kk] = static_cast<int16_t>(kind);
    item->paint(k, L->bag, L->bag2, kind);
  }
  put(*item, k, local);
  return k + 1;
}

const Matrix4& PeopleRenderer::putDown(double top) { return joint(m, IDENTITY, 0.4, top, -0.42, 0, 0.3, 0); }

}  // namespace fs
