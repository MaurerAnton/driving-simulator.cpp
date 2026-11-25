// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/water.ts @ v1.7.7
/**
 * Water in the picture: rivers, lakes and ponds that look like water, and
 * the splashes, spray and wake of a vehicle driving through them.
 *
 * WaterLook teaches the terrain's material (and, simpler, the far view's)
 * what water is.  Where the ground cover is water (the cover texture's blue
 * of world/cover.h) the ground is drawn as a water surface:
 *  - a little below the banks (the terrain's vertices in the water are
 *    lowered by WATER_DROP – the game's wading lowers the bed for the
 *    wheels by as much and more);
 *  - its colour: the bed showing through at the edge (a little
 *    transparency), dark and blue-green towards the middle, where the water
 *    is deep (how much water there is round a point: the cover texture's
 *    coarse mipmap level);
 *  - small ripples drifting over it: the slopes of two octaves of value
 *    noise – computed, no textures – fading out with distance (where they
 *    would only flicker);
 *  - the sky mirrored with a Fresnel term (the sky dome's own colours, which
 *    the weather tints) and the sun glinting on the ripples.
 * It costs a few noise lookups per pixel, and only in water pixels.  It is
 * a recompile, so it is switched on and off as a whole (`enabled`, the
 * graphics setting "real water").  The far view gets the mirrored sky and a
 * broad glint on a flat surface.
 *
 * (The shaders are the original's, as its patch leaves them – captured by tools/glsl/weather.mjs.)
 *
 * WaterSpray is a pool of particles – drops thrown up by the wheels and the
 * foam of the wake – drawn as one Points; game/wading.h emits them.
 */
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "engine/engine.h"

namespace fs {

/** how far the water's surface lies below the banks (m) – game/wading.h lowers the bed for the wheels by this and more */
inline constexpr double WATER_DROP = 0.15;

/** 'terrain' | 'far' */
using WaterTarget = std::string;

/**
 * Real water in the terrain's and the far view's materials.  patch() each
 * material once (as it is made); `enabled` switches the water on and off
 * (a recompile); update() every frame lets the ripples drift.
 */
class WaterLook {
 public:
  struct Uniforms {
    UniformPtr uFsWtrTime = uniform(0.0);
    UniformPtr uFsWtrZenith = uniform(Color(0x3a74c6));
    UniformPtr uFsWtrHorizon = uniform(Color(0xc2d3e4));
    UniformPtr uFsWtrSunDir = uniform(Vector3(0.4, 0.7, 0.3).normalize());
    UniformPtr uFsWtrSunColor = uniform(Color(0xfff2d0));
  } uniforms;

  /**
   * The sky the water mirrors: the dome of render/sky.h's Sky (a mesh with
   * a ShaderMaterial whose uSunDir, uZenith, uHorizon and uSunColor the sky
   * and the weather keep up to date) – its uniforms are shared, not copied.
   */
  void attachSky(const Object3D* dome);

  /** real water drawn (the graphics setting) – a change recompiles the patched materials */
  bool enabled() const { return on_; }
  void enabled(bool on);

  /** Once a frame: the ripples drift on. */
  void update(double dt);

  /**
   * Teach a material the water (once): 'terrain' – a tile's terrain (a
   * MeshLambertMaterial with the cover texture as its map); 'far' – the far
   * view's (vertex colours).  Chained after what the material already does.
   */
  void patch(Material& material, const WaterTarget& target);

 private:
  bool on_ = false;
  /** the materials patched: the patch this put on each (weakly held: the tiles' terrain materials come and go), what they had before */
  struct Patched {
    std::weak_ptr<ShaderPatch> mine;
    Material* material;
    ShaderPatch plain;
    WaterTarget target;
  };
  std::vector<Patched> mats_;

  /** the patch of a material for the state of `on_` */
  void apply(const Patched& p, ShaderPatch& out) const;
};

/** one particle: drops (fall under gravity) or foam (lies on the water and spreads) */
struct SprayParticle {
  double x = 0;
  double y = 0;
  double z = 0;
  double vx = 0;
  double vy = 0;
  double vz = 0;
  /** age and life span (s) */
  double age = 1;
  double life = 0;
  /** diameter at birth and at the end (m) */
  double size0 = 0;
  double size1 = 0;
  /** the water's surface where it was born: a drop falling back below it is gone */
  double floor = 0;
  bool foam = false;
};

/**
 * The drops and the foam: a pool of at most `capacity` particles drawn as one
 * Points (soft round sprites, white tinted by the light).  emit() adds
 * one (the oldest is replaced when the pool is full); update() once a frame.
 */
class WaterSpray {
 public:
  std::shared_ptr<Points> points;
  /** how bright the light is (1 = a clear day): the drops' colour */
  double light = 1;
  const int capacity;

  explicit WaterSpray(int capacity = 600);
  WaterSpray(const WaterSpray&) = delete;
  WaterSpray& operator=(const WaterSpray&) = delete;

  /** the particles alive now */
  int count() const { return live_; }
  /** Add a particle (its fields are copied).  The pool full: the one born longest ago makes room. */
  void emit(const SprayParticle& p);
  /** Move the particles on by `dt` seconds and hand them to the GPU. */
  void update(double dt);
  /** Gone at once (a new drive). */
  void clear();
  void dispose();

 private:
  std::vector<SprayParticle> pool_;
  int live_ = 0;
  int next_ = 0;
  GeometryPtr geo_;
  MaterialPtr mat_;
  AttrPtr pos_, size_, alpha_;
  UniformPtr uViewH_ = uniform(800.0);
  UniformPtr uColor_ = uniform(Color(0.85, 0.9, 0.93));
};

}  // namespace fs
