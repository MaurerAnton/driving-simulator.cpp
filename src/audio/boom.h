// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/audio/boom.ts @ v1.7.7
/**
 * The bang of a vehicle going up in flames: a hard crack, a deep thump
 * that rolls on, and the roar and crackle of the fire dying down – made of
 * filtered noise and a falling sine, nothing recorded.  It plays in the
 * game's AudioContext, into its master gain (the game's volume).
 */
#pragma once

#include "platform/audio.h"

namespace fs {

/** Play the bang now; `level` 0…1 (how near it was). */
void playBoom(const AudioContext& ctx, const AudioNode& out, double level = 1);

}  // namespace fs
