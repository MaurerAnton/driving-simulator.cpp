// SPDX-License-Identifier: AGPL-3.0-only
/** The 2D canvas of the native host: what a test asks about a context. */
#pragma once

#include <string>
#include <vector>

#include "platform/canvas.h"

namespace fs::canvasNative {

/** how many drawing calls the context has taken */
long calls(const Ctx2D& g);
/** the texts it was asked to write (the last few hundred) */
std::vector<std::string> texts(const Ctx2D& g);
/** start counting anew */
void forget(const Ctx2D& g);

}  // namespace fs::canvasNative
