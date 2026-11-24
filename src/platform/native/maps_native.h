// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The native side of platform/geolocation.h, frame.h and observers.h: what a test sets
 * and drives by hand.
 */
#pragma once

#include <optional>
#include <string>

#include "platform/dom.h"
#include "platform/geolocation.h"

namespace fs::plat::native {

/** `navigator.geolocation` answers with this position from now on (asked for, it answers at the next pump()) */
void setGeolocation(const GeoCoords& coords);
/** … or fails with this error (1 denied, 2 unavailable, 3 timeout) */
void setGeolocationError(int code);
/** … or is not there at all (as at the start) */
void clearGeolocation();

/** Run the callbacks requestAnimationFrame is holding (those asked for meanwhile wait for the next call).  Returns how many ran. */
int runFrames();
/** how many callbacks wait for the next picture */
int framesWaiting();

/** The size of an element changed: its ResizeObservers are told. */
void notifyResize(const dom::El& e);
/** An attribute of an element changed ("hidden", "class", "style" …): its MutationObservers that watch it are told. */
void notifyMutation(const dom::El& e, const std::string& attribute);
/** how many observers observe something now */
int observersActive();
/** what `el.offsetParent` answers for an element (unset: its parent element) */
void setOffsetParent(const dom::El& e, const dom::El& parent);

}  // namespace fs::plat::native
