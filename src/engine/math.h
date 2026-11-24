// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The maths of three.js, by the same names and with the same arithmetic, so that
 * the numbers come out as in the original:
 *
 *   math/mathutils.h   PI, TAU, DEG, RAD, clampD …, MathUtils
 *   math/linear.h      Vector2, Vector3, Vector4, Quaternion, Euler (six orders), Matrix3, Matrix4
 *   math/color.h       Color (linear-sRGB numbers; hex numbers, CSS texts and names are sRGB), ColorSpace, HSL
 *   math/shapes.h      Box2, Box3, Sphere, Plane, Line3, Triangle, Frustum, Ray, Spherical
 *
 * Origin: three.js r187 src/math/ (one class a file there)
 *
 * Everything is `double` and defined in the headers (nothing to link but fs_core, and
 * that only for texts and random numbers: Color::getStyle, Vector3::random …).  The
 * game's own ground plane is y-up, x-east, z-south (src/core/geom.ts).
 *
 * How JavaScript's forms are written here:
 *   v.clone()                    a copy by value:  Vector3 w = v.clone().add(u);
 *   box.getCenter(target)        the same, and without a target:  Vector3 c = box.getCenter();
 *   a.union(b)                   a.union_(b)   (`union` is a C++ word)
 *   m.elements[12]               the same; also m.e[12]
 *   new Color(0x808890) / '#rgb' Color(0x808890), Color("#rgb") – also implicitly, so that
 *                                `MaterialParams{.color = 0x808890}` works
 *   euler.order = 'YXZ'          euler.order = Euler::YXZ
 *   a result that may be null    a pointer (Ray::intersectTriangle …) or a std::optional<double>
 *   v.fromBufferAttribute(a, i)  the same (a BufferAttribute, or anything with getX/getY/getZ)
 *   v.project(camera)            the same (anything with matrixWorldInverse and projectionMatrix)
 */
#pragma once

#include "engine/math/mathutils.h"
#include "engine/math/linear.h"
#include "engine/math/color.h"
#include "engine/math/shapes.h"
