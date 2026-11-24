// SPDX-License-Identifier: AGPL-3.0-only
/**
 * JavaScript's Math functions as the original's engine (V8) computes them.
 *
 * The simulation has to come out with the same doubles as the original.  `+ - * /`
 * and Math.sqrt are the same everywhere; the functions here are not: V8 carries its own
 * (fdlibm's algorithms, src/base/ieee754.cc) and its own Math.hypot, and both differ
 * from the C library's in the last bit often enough that a drive of twenty seconds drifts
 * apart.  Math.pow is the C library's in V8 too: use std::pow.
 *
 * These belong beside jsRound and jsSign in core/js.h; they live here until the layers
 * are joined (the simulation is the first part that is compared with the original
 * bit by bit).
 *
 * One more difference is switched off here for every file that includes this header:
 * on machines that can multiply and add in one instruction (AArch64), GCC fuses a*b+c
 * unless it is told not to – and JavaScript never does.
 */
#pragma once

#if defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif

#include <cmath>

namespace fs {

/** Math.sin, Math.cos, Math.tan */
double jsSin(double x);
double jsCos(double x);
double jsTan(double x);
/** Math.atan, Math.atan2, Math.acos */
double jsAtan(double x);
double jsAtan2(double y, double x);
double jsAcos(double x);
/** Math.exp, Math.tanh */
double jsExp(double x);
double jsTanh(double x);
/** Math.hypot (the largest taken out, the squares summed with a correction, the root times the largest) */
double jsHypot(double a, double b);
double jsHypot(double a, double b, double c);
double jsHypot(double a, double b, double c, double d);

}  // namespace fs
