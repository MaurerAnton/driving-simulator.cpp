// SPDX-License-Identifier: AGPL-3.0-only
/**
 * What three.js's LightShadow does before a shadow map is drawn, for the renderer to call:
 *
 *     shadow.updateMatrices(light)      updateShadowMatrices(*light)
 *
 * Origin: three.js r187 src/lights/LightShadow.js (updateMatrices), src/lights/SpotLightShadow.js
 */
#pragma once

#include "engine/engine.h"

namespace fs {

/**
 * Puts the light's shadow camera where the light is (in the world), looking at the light's target,
 * and works out `light.shadow.matrix`: from the world into the shadow map (0 … 1 across, down and
 * in depth).  A spot light's shadow camera first takes the light's angle (fov = 2 · angle), the
 * map's shape and the light's distance (as its far plane, when the light has one).  The world
 * matrices of the light and of its target must be up to date.  `frustum`, when given, takes what
 * the shadow camera sees.  A point light (six pictures) and lights without a shadow camera are
 * left as they are.
 */
void updateShadowMatrices(Light& light, Frustum* frustum = nullptr);

}  // namespace fs
