// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/windscreen.ts @ v1.7.7
/**
 * The windscreen in rain and snow, seen from the driver's seat ("very high"
 * graphics only): drops gather on the glass and run up and to the sides
 * when driving fast, a sheet of water and the spray of the road blur the
 * view, snowflakes settle and build up into a white cover – and the wipers
 * sweep it clear (game/wipers.h keeps what lies where, and the stalk).
 *
 * How it is drawn, cheaply: one thin mesh lying just inside the windscreen
 * of the vehicle (so the dashboard, the pillars and the driver's hands stand
 * in front of it as they should), with a shader that
 *   - reads a coarse grid of the glass (48 × 24: water, snow, dirt, drops)
 *     sent up each frame as a tiny texture;
 *   - draws the drops themselves procedurally, a few per centimetre, each a
 *     little lens that shows the scene behind it upside down;
 *   - blurs the picture behind it where water lies: after each frame the
 *     scene behind the glass is drawn into a texture, and the next frame
 *     samples it around each point of the glass;
 *   - lays the snow over it, and draws the two wiper arms where they are.
 *
 * Where the glass is: for a car from its body (render/carBody.h: the
 * windscreen runs from the cowl to the roof's front edge, crowned in the
 * middle); for a truck, a bus, a tractor or a crane's cab the biggest pane
 * of glass in front of the driver's eyes; the tram's cab has an opening.
 */
#pragma once

#include <memory>
#include <optional>
#include <vector>

#include "engine/engine.h"
#include "game/wipers.h"

namespace fs {

class VehicleModel;
class Renderer;

/** The glass as a mesh in the model's frame, and its size along the glass (m). */
struct GlassShape {
  GeometryPtr geometry;
  double width = 0;
  double height = 0;
};

/**
 * Where the windscreen of a model is (null: it has none – a motorcycle, the
 * aeroplane's canopy is not done).  Found once per model.
 */
std::optional<GlassShape> findGlass(VehicleModel& model);

/** What the game gives each frame. */
struct WindscreenFrame {
  Renderer* renderer = nullptr;
  /** the scene and camera: to draw the background once without the glass for its blur (world, not part of the glass) */
  Scene* scene = nullptr;
  Camera* camera = nullptr;
  /** the vehicle the player drives (null: none) */
  VehicleModel* model = nullptr;
  /** the "very high" graphics: there is a windscreen to see */
  bool detail = false;
  /** seen from the driver's seat now (not on foot, not from outside, not the aeroplane) */
  bool cockpit = false;
  double dt = 0;
  /** the weather (game/weather.h) */
  double rain = 0;
  double snow = 0;
  double temperature = 0;
  double roadWet = 0;
  /** the wind (m/s, world frame: where it blows to) */
  double windX = 0;
  double windZ = 0;
  bool heater = false;
  /** the washer held */
  bool washer = false;
  /** daylight 0 … 1 (snow is not white in the night) */
  double light = 1;
};

/** The windscreen of the vehicle the player drives, and the stalk of its wipers. */
class WindscreenView {
 public:
  /** the stalk: where it stands is kept from vehicle to vehicle */
  WiperMode mode = "off";
  /** the glass of the vehicle now (null: none, or not the "very high" graphics) */
  std::unique_ptr<Windscreen> screen;
  /** seconds the driver has been driving with too little view through the glass */
  double blindFor = 0;

  WindscreenView();
  WindscreenView(const WindscreenView&) = delete;
  WindscreenView& operator=(const WindscreenView&) = delete;
  ~WindscreenView();

  /** The stalk one place on (the key, the button): the new place. */
  WiperMode cycle();
  /** How much of the view is lost now, 0 … 1 (0 without the "very high" graphics). */
  double obscured() const;
  /**
   * Once a frame, right after the picture is drawn: the weather on the
   * glass, the wipers, and the picture kept for the next frame's blur.
   * Returns the vehicle's speed it measured (m/s forwards).
   */
  double frame(const WindscreenFrame& f);
  /** the mesh on the glass (null: none) – for the tests */
  const std::shared_ptr<Mesh>& glassMesh() const { return mesh_; }
  void dispose();

 private:
  /** The vehicle changed (or the graphics): a new glass for it, or none. */
  void attach(VehicleModel* model);
  void upload(const Windscreen& s);
  void keepPicture(Renderer& renderer, Scene& scene, Camera& camera);

  VehicleModel* model_ = nullptr;
  std::shared_ptr<Mesh> mesh_;
  TexturePtr stateTex_;
  TexturePtr genTex_;
  /** the scene behind the glass drawn into a target of its own, for the blur (kept clean: no glass in it) */
  RenderTargetPtr offscreen_;
  MaterialPtr material_;
  struct Uniforms {
    UniformPtr uState, uGen;
    UniformPtr uBg = uniform(TexturePtr());
    UniformPtr uSize = uniform(Vector2(1, 1));
    UniformPtr uRes = uniform(Vector2(1, 1));
    UniformPtr uHaveBg = uniform(0.0);
    UniformPtr uLight = uniform(1.0);
    UniformPtr uFlow = uniform(Vector2());
    UniformPtr uRun = uniform(0.0);
    UniformPtr uArm0 = uniform(Vector4());
    UniformPtr uArm1 = uniform(Vector4());
    UniformPtr uIn = uniform(Vector2());
  } u_;
  Vector3 lastPos_;
  bool havePos_ = false;
};

}  // namespace fs
