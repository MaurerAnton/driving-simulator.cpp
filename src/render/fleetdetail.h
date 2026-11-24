// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The truck, the bus and the tractor in finer detail – for the "very high"
 * graphics only (render/fleetdetail.ts, with render/detailkit.ts).
 *
 * Not translated yet: what the models ask of it stands here – the interface
 * of a finer model and the parts it is fixed to – and the three makers are
 * hooks of the original's names, null until the translation sets them.  The
 * models then stay the plain ones on "very high" as well.  When the file is
 * translated, its declarations take the place of these and the hooks go.
 */
#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "engine/engine.h"
#include "sim/vehicle/types.h"

namespace fs {

/** The finer model of one vehicle: shown or not, and moved with it each frame. */
class FleetDetail {
 public:
  virtual ~FleetDetail() = default;
  virtual void show(bool on) = 0;
  virtual void update() = 0;
};

struct TruckParts {
  std::shared_ptr<Group> root;
  std::shared_ptr<Group> trailer;
  const VehicleSpec* spec = nullptr;
  const std::vector<std::shared_ptr<Group>>* wheels = nullptr;
  const std::vector<std::shared_ptr<Group>>* trailerWheels = nullptr;
};

struct BusParts {
  std::shared_ptr<Group> root;
  const VehicleSpec* spec = nullptr;
  const std::vector<std::shared_ptr<Group>>* wheels = nullptr;
};

struct TractorParts {
  std::shared_ptr<Group> root;
  const std::vector<std::shared_ptr<Group>>* wheels = nullptr;
  /** the radii of the wheels as drawn (front, front, rear, rear) */
  std::vector<double> radii;
};

/** The truck of classes C and C1 with its cab and box body (render/truck.ts), and its trailer.  (null: not translated) */
inline std::function<std::unique_ptr<FleetDetail>(const TruckParts& t)> truckDetail = nullptr;
/** The city bus and the minibus (render/fleet.ts PlayerBus).  (null: not translated) */
inline std::function<std::unique_ptr<FleetDetail>(const BusParts& b)> busDetail = nullptr;
/** The tractor (render/fleet.ts PlayerTractor).  (null: not translated) */
inline std::function<std::unique_ptr<FleetDetail>(const TractorParts& t)> tractorDetail = nullptr;

}  // namespace fs
