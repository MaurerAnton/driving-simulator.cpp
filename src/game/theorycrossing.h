// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/theorycrossing.ts @ v1.7.7
/**
 * Two theory lessons (game/theory.ts) at a level crossing (Bahnübergang):
 * a road crossed by a railway line with the St Andrew's cross (Zeichen
 * 201), its yellow and red lights, the warning sign (Zeichen 151) before
 * it and a stop line; the regional tram of the line comes from the east.
 *
 *   - levelCrossing: the lights come on as you come to it – you stop at the
 *     line before the cross (§ 19 Abs. 2 StVO), and wait until they are
 *     out again, not only until the train has gone.
 *   - levelCrossingQueue: the traffic behind the tracks stands still – you
 *     wait before the cross until there is room for the whole car behind
 *     the tracks, and never stop on them (§ 19 Abs. 3 StVO).
 *
 * The same kind of script as the others: plain data, checked by
 * tests/game/theory.test.ts like every lesson.
 */
#pragma once

#include "game/theory.h"

namespace fs::theory {

/** At the level crossing the lights come on: you stop before the cross, and wait until they are out. */
LessonDef LEVEL_CROSSING();

/** The traffic behind the tracks stands still: you wait before the cross, never on the rails. */
LessonDef LEVEL_CROSSING_QUEUE();

}  // namespace fs::theory
