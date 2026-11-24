// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/world/xsection.ts @ v1.7.7
#include "world/xsection.h"

#include <algorithm>
#include <cmath>

#include "core/js.h"

namespace fs {

namespace {
/** `!v` of a field that may be missing: missing, 0 and NaN are nothing */
bool nothing(const std::optional<double>& v) { return !v || *v == 0 || *v != *v; }
bool nothing(const std::optional<int>& v) { return !v || *v == 0; }
}  // namespace

double parkDepth(int orient) { return orient == ORIENT_PERPENDICULAR ? 5.0 : orient == ORIENT_DIAGONAL ? 5.3 : 2.0; }

double parkWidth(const RoadSideJson* s) {
  if (!s || nothing(s->p)) return 0;
  return s->pw ? *s->pw : parkDepth(s->po ? *s->po : ORIENT_PARALLEL);
}

double sideInner(const RoadSideJson* s) {
  if (!s) return 0;
  const double pw = parkWidth(s);
  double w = s->c == CYCLE_LANE ? (s->cw ? *s->cw : 1.5) : 0;
  if (s->p == PARK_LANE || s->p == PARK_STREET_SIDE || s->p == PARK_SHOULDER) w += pw;
  else if (s->p == PARK_HALF_ON_KERB) w += pw / 2;
  return w;
}

double sideOuter(const RoadSideJson* s) {
  if (!s) return 0;
  const double pw = parkWidth(s);
  double w = s->p == PARK_ON_KERB ? pw : s->p == PARK_HALF_ON_KERB ? pw / 2 : 0;
  w += s->v ? *s->v : 0;
  if (s->c == CYCLE_TRACK) w += s->cw ? *s->cw : 1.6;
  return w;
}

double kerbHeight(const RoadSideJson* s) {
  const int k = s && s->k ? *s->k : 0;
  // (a kind the list does not have: the raised kerb)
  return k >= 0 && k <= 2 ? KERB_HEIGHTS[k] : KERB_HEIGHTS[0];
}

bool sideIsPlain(const RoadSideJson* s) {
  if (!s) return true;
  return !(s->p || s->po || s->pw || s->pm || s->pq || s->c || s->cw || s->cr || s->ca || s->cs || s->v || s->k || s->g || s->el || s->vp ||
           s->vt || s->pt || s->ft || s->fh || s->fo || s->wm);
}

SideLayout sideLayout(double half, double beyond, const RoadSideJson* s) {
  const double pw = parkWidth(s);
  const double kerb = half;
  double inner = kerb;
  std::optional<std::array<double, 2>> park;
  if (s && !nothing(s->p)) {
    if (s->p == PARK_HALF_ON_KERB) park = {kerb - pw / 2, kerb + pw / 2};
    else if (s->p == PARK_ON_KERB) park = {kerb, kerb + pw};
    else park = {kerb - pw, kerb};
    inner = jsMin(inner, (*park)[0]);
  }
  std::optional<std::array<double, 2>> cycleLane;
  if (s && s->c == CYCLE_LANE) {
    const double cw = s->cw ? *s->cw : 1.5;
    cycleLane = {inner - cw, inner};
    inner -= cw;
  }
  double out = s && (s->p == PARK_ON_KERB || s->p == PARK_HALF_ON_KERB) && park ? (*park)[1] : kerb;
  const double outerEdge = kerb + jsMax(0, beyond);
  std::optional<std::array<double, 2>> verge;
  if (s && !nothing(s->v) && *s->v > 0) {
    verge = {out, jsMin(outerEdge, out + *s->v)};
    out = (*verge)[1];
  }
  std::optional<std::array<double, 2>> track;
  if (s && s->c == CYCLE_TRACK) {
    track = {out, jsMin(outerEdge, out + (s->cw ? *s->cw : 1.6))};
    out = (*track)[1];
  }
  std::optional<std::array<double, 2>> walk;
  if (outerEdge - out > 0.3) walk = {out, outerEdge};
  SideLayout l;
  l.kerb = kerb;
  l.kerbH = beyond > 0 ? kerbHeight(s) : 0;
  l.laneEdge = inner;
  l.cycleLane = cycleLane;
  l.park = park;
  l.verge = verge;
  l.track = track;
  l.walk = walk;
  l.outer = outerEdge;
  return l;
}

double gutterWidth(const RoadSideJson* s, bool urban, double kerbH) {
  if (kerbH < 0.005 || (s && s->p == PARK_STREET_SIDE)) return 0;
  if (s && s->g) return *s->g;
  return urban && kerbH >= 0.08 ? GUTTER_DEFAULT : 0;
}

bool edgeLineOn(const RoadSideJson* s, bool byClass) { return s && s->el ? *s->el == 1 : byClass; }

int medianBits(const std::string& v) {
  if (v.empty()) return 0;
  int bits = 0;
  for (const std::string& part : split(v, ";")) {
    const std::string p = toLowerAscii(trim(part));
    if (p == "grass") bits |= MEDIAN_GRASS;
    else if (p == "parking") bits |= MEDIAN_PARKING;
    else if (p == "hedge") bits |= MEDIAN_HEDGE;
    else if (p == "trees" || p == "tree") bits |= MEDIAN_TREES;
    else if (p == "paving_stones" || p == "paved" || p == "sett" || p == "cobblestone") bits |= MEDIAN_PAVED;
  }
  return bits;
}

}  // namespace fs
