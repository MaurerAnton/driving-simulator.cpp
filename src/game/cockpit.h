// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/cockpit.ts @ v1.7.7
/**
 * Looking into a mirror: which of the vehicle's mirrors is shown large over
 * the picture (ui/mirrorzoom.ts draws it, render/mirrorzoom.ts renders it).
 *
 * The mirror key and the mirror button go round the mirrors the vehicle has
 * – off → left → right → inside → off –, a tap on a mirror in the driver's
 * seat shows that one (and a second tap puts it away again).  A car has all
 * three, a truck, a bus or a tractor only the two outside ones, the tram's
 * cab none.  Nothing here draws or knows about the engine: it is the state,
 * kept apart so that it can be tested.
 */
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace fs {

/** the mirrors by their names (render/mirrors.ts MirrorSpec.name), in the order the key goes round them */
inline const std::vector<std::string> MIRROR_ORDER = {"left", "right", "inside"};

class MirrorLook {
 public:
  /** the mirror shown large (its name), or null: none */
  std::optional<std::string> shown;

  /** The next one of the mirrors the vehicle has (`have`, by name) – after the last of them, none. */
  std::optional<std::string> next(const std::vector<std::string>& have);

  /** A mirror was tapped: that one large – or, if it is the one shown already, none. */
  std::optional<std::string> pick(const std::string& name, const std::vector<std::string>& have);

  void close();

  /** The vehicle (or what it shows) changed: a mirror it does not have is shown no more. */
  std::optional<std::string> keep(const std::vector<std::string>& have);
};

/** Where a mirror's glass is on the screen: its bounding box (px), and how far it is from the eye (m). */
struct MirrorOnScreen {
  std::string name;
  double x0 = 0;
  double y0 = 0;
  double x1 = 0;
  double y1 = 0;
  double distance = 0;
};

/**
 * The mirror a tap at (x, y) means: one whose glass is there, or – the glass
 * of a mirror is small on a phone – within `margin` pixels of it; of several,
 * the one the tap is deepest inside, then the nearest.  Null: none.
 */
std::optional<std::string> mirrorAt(double x, double y, const std::vector<MirrorOnScreen>& mirrors, double margin = 24);

/**
 * The shape of the panel the mirror is shown in: width : height of its glass,
 * kept within limits – so that a tall truck mirror or a long, narrow inside
 * mirror still gives a picture that fits the upper half of a screen.
 */
double zoomAspect(double glassWidth, double glassHeight);

/** A rectangle of the screen (px). */
struct PanelRect {
  double x = 0;
  double y = 0;
  double w = 0;
  double h = 0;
};

/**
 * The panel on a screen of `w` × `h` pixels: as large as the upper half of
 * the screen allows (and most of its width), centred, its top `top` pixels
 * down – with the shape `aspect` (width : height).
 */
PanelRect zoomPanel(double w, double h, double aspect, double top);

}  // namespace fs
