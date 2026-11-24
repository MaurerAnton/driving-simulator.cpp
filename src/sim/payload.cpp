// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/payload.ts @ v1.7.7
#include "sim/payload.h"

#include <algorithm>

#include "core/js.h"

namespace fs {

VehicleSpec withPayload(const VehicleSpec& spec, const Payload* load) {
  if (!load || !(load->mass > 0)) return spec;
  const double M = spec.mass;
  const double m = load->mass;
  const double total = M + m;
  // centre of mass, from the ground below the middle of the wheelbase (z: + back)
  const double zc = spec.cgToFront - spec.wheelbase / 2;
  const double yc = spec.cgHeight;
  const double z2 = (M * zc + m * load->z) / total;
  const double y2 = (M * yc + m * load->y) / total;
  const double ix = spec.inertia[0];
  const double iy = spec.inertia[1];
  const double iz = spec.inertia[2];
  const double dy = y2 - yc;
  const double dz = z2 - zc;
  const double ly = load->y - y2;
  const double lz = load->z - z2;
  // pitch (about the cross axis), yaw (about the vertical), roll (about the length)
  const double pitch = ix + M * (dy * dy + dz * dz) + m * (ly * ly + lz * lz) + (m * (load->hgt * load->hgt + load->len * load->len)) / 12;
  const double yaw = iy + M * dz * dz + m * lz * lz + (m * (load->wid * load->wid + load->len * load->len)) / 12;
  const double roll = iz + M * dy * dy + m * ly * ly + (m * (load->wid * load->wid + load->hgt * load->hgt)) / 12;
  // (the centre of mass stays between the axles: a load hung far out behind would need a model of its own)
  const double cgToFront = std::min(spec.wheelbase * 0.9, std::max(spec.wheelbase * 0.1, z2 + spec.wheelbase / 2));
  VehicleSpec out = spec;
  out.name = spec.name + ", " + jsStr(jsRound(m)) + " kg geladen";
  out.mass = total;
  out.inertia = {pitch, yaw, roll};
  out.cgHeight = y2;
  out.cgToFront = cgToFront;
  return out;
}

}  // namespace fs
