// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The towns the comparison tools drive in (options for makeTown of the original's
 * tests/game/town.ts) – the same as tools/dev/sim_towns.h (keep the two alike): plain (4 × 4
 * streets), lights (two junctions with lights, a zebra and a pedestrian light), wide (two lanes
 * each way on the second row, one-way streets, a cul-de-sac).
 */
/* eslint-disable @typescript-eslint/no-explicit-any */
export function townOptions(which: string): any {
  if (which === 'lights') {
    return {
      cols: 4,
      rows: 4,
      block: 100,
      pavement: 2,
      lights: [
        [1, 1],
        [2, 2],
      ],
      crossings: [
        { street: [0, 1, 1, 1], at: 0.5, k: 1 /* XING.ZEBRA */ },
        { street: [2, 1, 3, 1], at: 0.4, k: 2 /* XING.SIGNAL */ },
      ],
    };
  }
  if (which === 'wide') {
    return {
      cols: 5,
      rows: 4,
      block: 120,
      wide: [
        [0, 1, 1, 1],
        [1, 1, 2, 1],
        [2, 1, 3, 1],
        [3, 1, 4, 1],
      ],
      oneWay: [
        [0, 2, 1, 2],
        [1, 2, 2, 2],
      ],
      missing: [[3, 0, 4, 0]],
      deadEnd: 60,
    };
  }
  return { cols: 4, rows: 4, block: 100 };
}
