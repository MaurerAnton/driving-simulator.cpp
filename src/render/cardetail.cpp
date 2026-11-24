// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/cardetail.ts @ v1.7.7
#include "render/cardetail.h"

#include <cmath>
#include <functional>
#include <limits>
#include <optional>

#include "core/js.h"
#include "engine/geometries.h"
#include "render/car.h"
#include "render/carBody.h"
#include "render/detailkit.h"
#include "render/mirrors.h"

namespace fs {

namespace {

Vector3 v3(double x, double y, double z) { return Vector3(x, y, z); }

using UV = std::array<double, 2>;

// materials of the outside (the inside's are made per car, as the cabin's: render/car.ts cabinMaterial)
struct Outside {
  MaterialPtr GAP, GLOSS_BLACK, RUBBER, PLASTIC, CHROME, HOUSING, ENAMEL, LENS, TYRE, METAL, CALIPER;
  Outside() {
    GAP = lambert({.color = Color(0x0a0b0c), .polygonOffset = true, .polygonOffsetFactor = -1, .polygonOffsetUnits = -2});
    // (the parts that open leave the gaps along their edges on the body: render/caropen.ts)
    GAP->name = "panel gap";
    GLOSS_BLACK = phong({.color = Color(0x0e0f11), .polygonOffset = true, .polygonOffsetFactor = -1, .polygonOffsetUnits = -2, .specular = Color(0x4a4f55), .shininess = 70});
    RUBBER = lambert({.color = Color(0x141517)});
    PLASTIC = lambert({.color = Color(0x1d1f22), .polygonOffset = true, .polygonOffsetFactor = -1, .polygonOffsetUnits = -2});
    CHROME = phong({.color = Color(0xd6dbe0), .specular = Color(0xffffff), .shininess = 110});
    HOUSING = phong({.color = Color(0x15171a), .specular = Color(0x5a6068), .shininess = 60});
    ENAMEL = phong({.color = Color(0x18305e), .specular = Color(0x8899bb), .shininess = 80});
    /** the clear covers of the lamps: only a sheen over what is under them */
    LENS = phong({.color = Color(0xdfe8ee), .transparent = true, .opacity = 0.16, .depthWrite = false, .specular = Color(0xffffff), .shininess = 140});
    TYRE = lambert({.color = Color(0x1b1c1e)});
    METAL = phong({.color = Color(0xc3c7cb), .vertexColors = true, .specular = Color(0x777777), .shininess = 70});
    CALIPER = phong({.color = Color(0x9a1d17), .specular = Color(0x553333), .shininess = 40});
  }
};

const Outside& M() {
  static const Outside m;
  return m;
}

/** A rounded-rectangle outline (w × h, corner radius r) in the x-y plane, counter-clockwise. */
std::vector<UV> rounded(double w, double h, double r, int seg = 3) {
  std::vector<UV> out;
  for (const auto& [cx, cy, a0] : {std::array<double, 3>{w / 2 - r, -h / 2 + r, -90}, std::array<double, 3>{w / 2 - r, h / 2 - r, 0},
                                   std::array<double, 3>{-w / 2 + r, h / 2 - r, 90}, std::array<double, 3>{-w / 2 + r, -h / 2 + r, 180}}) {
    for (int i = 0; i <= seg; i++) {
      const double a = ((a0 + (90.0 * i) / seg) * PI) / 180;
      out.push_back({cx + std::cos(a) * r, cy + std::sin(a) * r});
    }
  }
  return out;
}

/** A flat outline in a frame, pushed out to `w` (a ring of points for tubeOf). */
std::vector<V3> ringIn(const Frame& f, const std::vector<UV>& outline, double w) {
  std::vector<V3> out;
  for (const auto& [u, v] : outline) {
    const Vector3 p = f.at(u, v, w);
    out.push_back({p.x, p.y, p.z});
  }
  return out;
}

/** A slab with rounded corners: an outline in a frame, from w0 to w1 off it (both ends closed). */
GeometryPtr slab(const Frame& f, const std::vector<UV>& outline, double w0, double w1) { return tubeOf({ringIn(f, outline, w0), ringIn(f, outline, w1)}); }

/** A sheet through two rows of points with their normals (no lift). */
GeometryPtr sheetRows(const std::vector<std::vector<Hit>>& rows) {
  std::vector<float> pos;
  std::vector<float> nrm;
  for (size_t j = 0; j + 1 < rows.size(); j++) {
    for (size_t i = 0; i + 1 < rows[j].size(); i++) {
      const Hit& a = rows[j][i];
      const Hit& b = rows[j][i + 1];
      const Hit& c = rows[j + 1][i + 1];
      const Hit& d = rows[j + 1][i];
      const Vector3 f = Vector3().subVectors(b.p, a.p).cross(Vector3().subVectors(d.p, a.p));
      const bool fwd = f.dot(a.n) >= 0;
      for (const Hit* h : fwd ? std::array<const Hit*, 6>{&a, &b, &c, &a, &c, &d} : std::array<const Hit*, 6>{&a, &c, &b, &a, &d, &c}) {
        pos.insert(pos.end(), {static_cast<float>(h->p.x), static_cast<float>(h->p.y), static_cast<float>(h->p.z)});
        nrm.insert(nrm.end(), {static_cast<float>(h->n.x), static_cast<float>(h->n.y), static_cast<float>(h->n.z)});
      }
    }
  }
  const GeometryPtr g = make<BufferGeometry>();
  g->setAttribute("position", attr(std::move(pos), 3));
  g->setAttribute("normal", attr(std::move(nrm), 3));
  return g;
}

/** The top of the gear knob (a cap of its sphere, render/car.ts), with the picture laid flat over it from above. */
GeometryPtr knobCap() {
  GeometryPtr g = sphereGeometry(0.0274, 20, 4, 0, PI * 2, 0, 0.55);
  g->scale(1, 0.9, 1.05).translate(0, 0.205, 0);
  const AttrPtr pos = g->getAttribute("position");
  const AttrPtr uv = g->getAttribute("uv");
  const double r = 0.0274 * std::sin(0.55);
  // (seen from above: x to the right, forwards – towards −z – up the picture)
  for (size_t i = 0; i < pos->count(); i++) uv->setXY(i, 0.5 + pos->getX(i) / (2 * r), 0.5 - pos->getZ(i) / (2 * r * 1.05));
  return g;
}

/** Boxes merged as they are (for a small repeated part). */
GeometryPtr tubeFree(const std::vector<GeometryPtr>& parts) {
  Parts p;
  const auto tmp = make<Group>();
  const MaterialPtr mat = basic();
  p.add(mat, parts);
  const std::vector<std::shared_ptr<Mesh>> built = p.build(*tmp, "cell");
  return built[0]->geometry;
}

/** The shift pattern on the knob: 1 3 5 above, 2 4 6 below, R at the left. */
Canvas patternCanvas() {
  const Canvas c(128, 128);
  const Ctx2D x = c.getContext2D();
  x.fillStyle("#16181b");
  x.fillRect(0, 0, 128, 128);
  x.strokeStyle("#d9dde2");
  x.lineWidth(5);
  x.lineCap("round");
  x.beginPath();
  for (const double gx : {28.0, 64.0, 100.0}) {
    x.moveTo(gx, 34);
    x.lineTo(gx, 94);
  }
  x.moveTo(28, 64);
  x.lineTo(100, 64);
  x.stroke();
  x.fillStyle("#d9dde2");
  x.font("700 22px Arial, sans-serif");
  x.textAlign("center");
  x.textBaseline("middle");
  const char* top[] = {"1", "3", "5"};
  const char* bottom[] = {"2", "4", "6"};
  for (int i = 0; i < 3; i++) x.fillText(top[i], 28 + i * 36, 20);
  for (int i = 0; i < 3; i++) x.fillText(bottom[i], 28 + i * 36, 110);
  x.fillText("R", 10, 64);
  return c;
}

/** The selector's display: P R N D, the one engaged lit. */
void drawSelector(const Canvas& c, int gear) {
  const Ctx2D x = c.getContext2D();
  if (!x) return;
  x.fillStyle("#0d0f12");
  x.fillRect(0, 0, 64, 128);
  x.font("700 24px Arial, sans-serif");
  x.textAlign("center");
  x.textBaseline("middle");
  const std::string on = gear == -2 ? "P" : gear == -1 ? "R" : gear == 0 ? "N" : "D";
  const char* letters[] = {"P", "R", "N", "D"};
  for (int i = 0; i < 4; i++) {
    x.fillStyle(letters[i] == on ? "#ffffff" : "#5b636c");
    x.fillText(letters[i], 32, 18 + i * 30);
  }
}

double sign(double v) { return v > 0 ? 1 : v < 0 ? -1 : v; }

}  // namespace

CarDetail::~CarDetail() = default;

CarDetail::CarDetail(const CarParts& car) : car_(car) {
  const Outside& K = M();
  CarExterior& ext = *car.exterior;
  const CarShell& sh = ext.shell;
  const auto at = bodyProfile(ext.dims);
  Surface body({sh.paint, sh.pillars, sh.trim});
  Surface glassy({sh.glass, sh.paint, sh.pillars});
  Parts out;
  const MaterialPtr paint = ext.paint;
  const double hw = sh.halfWidth;
  using Ctrl = std::pair<Vector3, Vector3>;

  // ================================================================ outside ====
  // ---- panel gaps: doors, sill, bonnet, tailgate ------------------------------
  const auto gap = [&](const std::vector<Hit>& pts, double width = 0.0065) {
    if (pts.size() > 1) out.add(K.GAP, ribbon(pts, width, 0.0022));
  };
  /** a path on the surface through control points [x, y, z], each seen along its own direction */
  const auto path = [&](const std::vector<Ctrl>& ctrl, double step = 0.02, Surface* s = nullptr) {
    Surface& surf = s ? *s : body;
    std::vector<Hit> pts;
    for (size_t i = 0; i + 1 < ctrl.size(); i++) {
      const auto& [a, da] = ctrl[i];
      const auto& [b, db] = ctrl[i + 1];
      const int n = static_cast<int>(jsMax(1, std::ceil(a.distanceTo(b) / step)));
      for (int k = i == 0 ? 0 : 1; k <= n; k++) {
        const double t = static_cast<double>(k) / n;
        const std::optional<Hit> h = surf.onto(a.clone().lerp(b, t), da.clone().lerp(db, t));
        if (h) pts.push_back(*h);
      }
    }
    return pts;
  };
  const auto belt = [&](double z) { return at(z)[4][1]; };
  for (const double s : {-1.0, 1.0}) {
    const Vector3 side = v3(-s, 0, 0);
    const auto flank = [&](double z, double y) { return Ctrl{v3(s * hw, y, z), side}; };
    const double zF = sh.zCowl + 0.105;
    const double zB = sh.zPillarB;
    const double zR = sh.zAxleR - 0.255;
    // the front door's front edge, the gap between the doors, the rear door's edge round the wheel arch, the sill
    gap(path({flank(zF, 0.37), flank(zF, belt(zF) - 0.02)}));
    gap(path({flank(zB, 0.37), flank(zB, belt(zB) - 0.012)}));
    gap(path({flank(zR, belt(zR) - 0.012), flank(zR, 0.63), flank(zR - 0.07, 0.55), flank(zR - 0.13, 0.42), flank(zR - 0.16, 0.37)}));
    gap(path({flank(zF, 0.37), flank(zR - 0.16, 0.37)}, 0.05));
    // the bonnet's sides along the top of the wings, from the scuttle to its front
    const Vector3 top = v3(0, -1, 0);
    const auto bonnetX = [&](double z) { return s * (at(z)[5][0] - 0.05); };
    std::vector<Ctrl> bonnet;
    for (double z = sh.zCowl - 0.09; z > sh.zFront + 0.1; z -= 0.08) bonnet.push_back({v3(bonnetX(z), 1.2, z), top});
    bonnet.push_back({v3(bonnetX(sh.zFront + 0.105), 1.2, sh.zFront + 0.105), top});
    gap(path(bonnet));
    // the tailgate: down the C pillar beside the rear window, over the corner inside the tail lamps, across the tail
    const double zt = sh.zTail;
    // (on the pillar beside the rear window, a little below the roof's edge, seen square on)
    const auto cp = [&](double z) {
      const auto pr = at(z);
      const double x4 = pr[4][0], y4 = pr[4][1];
      const double x5 = pr[5][0], y5 = pr[5][1];
      const Vector2 n = Vector2(y5 - y4, x4 - x5).normalize();
      const double px = x5 + (x4 - x5) * 0.08;
      const double py = y5 + (y4 - y5) * 0.08;
      return Ctrl{v3(s * (px + n.x * 0.1), py + n.y * 0.1, z), v3(-s * n.x, -n.y, 0)};
    };
    const Vector3 back = v3(0, -0.6, -0.8);
    gap(path({
        cp(sh.zRoofRear + 0.01),
        cp((sh.zRoofRear + zt) / 2),
        cp(zt - 0.01),
        {v3(s * 0.4, 1.3, zt + 0.07), back},
        {v3(s * 0.385, 1.3, sh.zRear - 0.06), back},
        {v3(s * 0.385, 0.745, sh.zRear + 0.3), v3(0, 0, -1)},
    }));
  }
  // the bonnet's front edge and the tailgate's lower edge, across
  gap(path({{v3(-(at(sh.zFront + 0.105)[5][0] - 0.05), 1.2, sh.zFront + 0.105), v3(0, -1, 0)}, {v3(at(sh.zFront + 0.105)[5][0] - 0.05, 1.2, sh.zFront + 0.105), v3(0, -1, 0)}}, 0.05));
  gap(path({{v3(-0.385, 0.745, sh.zRear + 0.3), v3(0, 0, -1)}, {v3(0.385, 0.745, sh.zRear + 0.3), v3(0, 0, -1)}}, 0.05));
  // the roof's rear edge over the tailgate
  gap(path({{v3(-(at(sh.zRoofRear + 0.01)[5][0] - 0.02), 2, sh.zRoofRear + 0.01), v3(0, -1, 0)}, {v3(at(sh.zRoofRear + 0.01)[5][0] - 0.02, 2, sh.zRoofRear + 0.01), v3(0, -1, 0)}}, 0.05));

  // ---- black trim: the B pillars, seals along the windows, the band round the windscreen, the scuttle ----
  for (const double s : {-1.0, 1.0}) {
    const Vector3 side = v3(-s, 0, 0);
    const double zb = sh.zPillarB;
    // the B pillar between the windows
    out.add(K.GLOSS_BLACK, drape(body, [&](double u, double v) { return v3(s * 1.4, belt(zb) + 0.004 + (at(zb)[5][1] - belt(zb) - 0.012) * v, zb - 0.044 + 0.088 * u); }, side, 3, 8, 0.0016));
    // the seal along the foot of the side windows, and the trim along their top
    const double z0 = sh.zCowl + 0.125;
    const double z1 = sh.zRoofRear - 0.17;
    std::vector<Hit> seal;
    std::vector<Hit> head;
    for (double z = z0; z <= z1 + 1e-6; z += 0.04) {
      const auto p = at(z);
      const std::optional<Hit> h = glassy.onto(v3(s * hw, p[4][1] + 0.009, z), side);
      if (h) seal.push_back(*h);
      if (z > sh.zRoofFront - 0.02) {
        const std::optional<Hit> t = glassy.onto(v3(s * hw, p[5][1] - 0.012, z), side);
        if (t) head.push_back(*t);
      }
    }
    if (seal.size() > 1) out.add(K.GLOSS_BLACK, ribbon(seal, 0.016, 0.0018));
    if (head.size() > 1) out.add(K.GLOSS_BLACK, ribbon(head, 0.014, 0.0018));
  }
  // the black band round the windscreen (along its foot and its top) and the scuttle panel under it
  const Vector3 down = v3(0, -1, 0);
  const auto across = [&](double z, double x, int n = 24) {
    std::vector<Hit> pts;
    for (int i = 0; i <= n; i++) {
      const std::optional<Hit> h = glassy.onto(v3(-x + (2 * x * i) / n, 2, z), down);
      if (h) pts.push_back(*h);
    }
    return pts;
  };
  out.add(K.GLOSS_BLACK, ribbon(across(sh.zCowl + 0.03, at(sh.zCowl + 0.03)[5][0] - 0.02), 0.045, 0.0016));
  out.add(K.GLOSS_BLACK, ribbon(across(sh.zRoofFront - 0.025, at(sh.zRoofFront - 0.025)[5][0] - 0.03), 0.04, 0.0016));
  const double zs0 = sh.zCowl - 0.085;
  out.add(K.PLASTIC, drape(body, [&](double u, double v) { return v3((at(zs0)[5][0] - 0.035) * (2 * u - 1), 2, zs0 + 0.082 * v); }, down, 16, 2, 0.002));
  // slots of the scuttle's air intake
  for (int i = 0; i < 9; i++) {
    const double x = -0.42 + i * 0.105;
    const std::optional<Hit> h = body.onto(v3(x, 2, zs0 + 0.04), down);
    if (h) out.add(K.GAP, Frame::on(h->p, h->n, v3(0, 0, -1)).box(0.08, 0.012, 0.004, 0, 0, 0.003));
  }

  // ---- the wipers, parked on the windscreen just above its foot ---------------
  Parts wiperParts;
  for (const auto& [x0, x1, zz, px] : {std::array<double, 4>{-0.72, -0.06, sh.zCowl + 0.05, -0.47}, std::array<double, 4>{0.0, 0.6, sh.zCowl + 0.065, 0.16}}) {
    const std::optional<Hit> a = glassy.onto(v3(x0, 2, zz), down);
    const std::optional<Hit> b = glassy.onto(v3(x1, 2, zz), down);
    const std::optional<Hit> pivot = body.onto(v3(px, 2, sh.zCowl - 0.04), down);
    if (!a || !b || !pivot) continue;
    const Vector3 n = a->n.clone().add(b->n).normalize();
    Vector3 dir = b->p.clone().sub(a->p);
    const double len = dir.length();
    dir.normalize();
    // the blade: rubber on a slim carrier
    const Frame f(a->p.clone().add(b->p).multiplyScalar(0.5), dir, n.clone().cross(dir).negate(), n);
    wiperParts.add(K.RUBBER, f.box(len, 0.016, 0.01, 0, 0, 0.008), f.box(len * 0.9, 0.01, 0.012, 0, 0, 0.018));
    // the arm from the pivot to the middle of the blade
    const Vector3 tip = f.at(0, 0, 0.024);
    const Vector3 foot = pivot->p.clone().addScaledVector(pivot->n, 0.02);
    const Vector3 arm = tip.clone().sub(foot);
    const Frame fa(foot.clone().add(tip).multiplyScalar(0.5), arm.clone().normalize(), pivot->n);
    wiperParts.add(K.RUBBER, fa.box(arm.length(), 0.012, 0.01), Frame::on(foot, pivot->n).cyl(0.018, 0.03, 0, 0, -0.008, 12));
  }
  keep(wiperParts.build(*ext.group, "wipers", true), 0);

  // ---- headlamps: following the curve of the nose, from the face up onto the bonnet's edge ----
  Parts lampParts;
  // (rays from in front and above, down onto the nose: the low ones meet its face, the high ones the bonnet's edge)
  const Vector3 front = v3(0, -0.6, 0.8);
  const double zO = sh.zFront - 0.2;
  for (const double s : {-1.0, 1.0}) {
    const auto xs = [&](double u) { return s * (0.3 + 0.42 * u); };
    const auto ys = [&](double v) { return 0.778 + 0.15 * v; };
    const auto o = [&](double u, double v) { return v3(xs(u), ys(v), zO); };
    lampParts.add(K.HOUSING, drape(body, o, front, 14, 5, 0.004));
    // the daytime running light along its lower edge, the indicator at its outer end
    std::vector<Hit> drl;
    for (int i = 0; i <= 12; i++) {
      const std::optional<Hit> h = body.hit(o(0.05 + 0.72 * (i / 12.0), 0.07), front);
      if (h) drl.push_back(*h);
    }
    if (drl.size() > 1) lampParts.add(ext.head, ribbon(drl, 0.011, 0.0065));
    lampParts.add(s < 0 ? ext.blinkL : ext.blinkR, drape(body, [&](double u, double v) { return o(0.82 + 0.16 * u, 0.12 + 0.6 * v); }, front, 3, 3, 0.0065));
    // two projectors in chrome bowls, their lenses lit by the lamps' material
    for (const double u : {0.3, 0.6}) {
      const std::optional<Hit> h = body.hit(o(u, 0.55), front);
      if (!h) continue;
      const Frame f = Frame::on(h->p, h->n);
      lampParts.add(K.CHROME, f.ring(0.026, 0.004, 0, 0, 0.007, 20), f.disc(0.025, 0, 0, 0.0055, 20));
      lampParts.add(ext.head, f.cyl(0.0125, 0.012, 0, 0, 0.012, 16));
    }
    // the clear cover over it all
    lampParts.add(K.LENS, drape(body, o, front, 14, 5, 0.013));
  }

  // ---- tail lamps: over the rear corners, round onto the flanks --------------
  const Vector3 rear = v3(0, -0.6, -0.8);
  const double zT = sh.zRear + 0.2;
  for (const double s : {-1.0, 1.0}) {
    const auto xs = [&](double u) { return s * (0.4 + 0.44 * u); };
    const auto ys = [&](double v) { return 0.985 + 0.2 * v; };
    const auto o = [&](double u, double v) { return v3(xs(u), ys(v), zT); };
    const auto part = [&](double u0, double u1, double v0, double v1, int nu) {
      return drape(body, [&](double u, double v) { return o(u0 + (u1 - u0) * u, v0 + (v1 - v0) * v); }, rear, nu, 4, 0.0075);
    };
    lampParts.add(K.HOUSING, drape(body, o, rear, 14, 5, 0.004));
    lampParts.add(ext.reverse, part(0.02, 0.12, 0.12, 0.88, 2));
    lampParts.add(s < 0 ? ext.blinkL : ext.blinkR, part(0.14, 0.27, 0.12, 0.88, 2));
    lampParts.add(ext.tail, part(0.29, 0.98, 0.08, 0.92, 10));
    // chrome ribs across the red
    for (const double v : {0.3, 0.5, 0.7}) {
      std::vector<Hit> rib;
      for (int i = 0; i <= 10; i++) {
        const std::optional<Hit> h = body.onto(o(0.3 + 0.67 * (i / 10.0), v), rear);
        if (h) rib.push_back(*h);
      }
      if (rib.size() > 1) lampParts.add(K.CHROME, ribbon(rib, 0.0035, 0.0095));
    }
    lampParts.add(K.LENS, drape(body, o, rear, 14, 5, 0.013));
  }
  keep(lampParts.build(*ext.group, "lamps"), 0);
  hide(std::vector<ObjectPtr>(ext.lampMeshes.begin(), ext.lampMeshes.end()));

  // ---- lower air intakes with a honeycomb mesh, and fog lamps -------------------
  const std::vector<std::array<double, 2>>& nose = sh.noseOutline;
  const auto halfAt = [&](double y) {
    double w = std::numeric_limits<double>::infinity();
    for (size_t i = 0; i + 1 < nose.size(); i++) {
      const double x0 = nose[i][0], y0 = nose[i][1];
      const double x1 = nose[i + 1][0], y1 = nose[i + 1][1];
      if (y0 == y1 || y < jsMin(y0, y1) || y > jsMax(y0, y1)) continue;
      w = jsMin(w, x0 + ((x1 - x0) * (y - y0)) / (y1 - y0));
    }
    return std::isinf(w) ? 0.0 : w;
  };
  const double zf = sh.zFront;
  const Frame faceF(v3(0, 0, zf), v3(-1, 0, 0), v3(0, 1, 0), v3(0, 0, -1));
  std::vector<Matrix4> cells;
  for (const double s : {-1.0, 1.0}) {
    const double y0 = 0.318;
    const double y1 = 0.428;
    const double xi = 0.31;
    const auto xo = [&](double y) { return halfAt(y) - 0.115; };
    // the dark opening, its frame
    const std::array<UV, 4> outline = {UV{s * xi, y0}, UV{s * xo(y0), y0}, UV{s * xo(y1), y1}, UV{s * xi, y1}};
    std::vector<Vector3> open;
    for (const auto& [x, y] : outline) open.push_back(faceF.at(-x, y, 0.0015));
    const GeometryPtr g = make<BufferGeometry>();
    g->setFromPoints(s > 0 ? std::vector<Vector3>{open[0], open[2], open[1], open[0], open[3], open[2]} : std::vector<Vector3>{open[0], open[1], open[2], open[0], open[2], open[3]});
    g->computeVertexNormals();
    out.add(K.PLASTIC, g);
    for (int i = 0; i < 4; i++) {
      const auto& [xa, ya] = outline[static_cast<size_t>(i)];
      const auto& [xb, yb] = outline[static_cast<size_t>((i + 1) % 4)];
      const Vector3 mid = faceF.at(-(xa + xb) / 2, (ya + yb) / 2, 0.004);
      Vector3 d = v3(xb - xa, yb - ya, 0);
      const double len = d.length() + 0.012;
      d.normalize();
      const Frame f(mid, d, v3(0, 0, -1).cross(d).normalize(), v3(0, 0, -1));
      out.add(K.GLOSS_BLACK, f.box(len, 0.012, 0.008));
    }
    // the cells of the mesh, in rows inside the frame
    const double r = 0.0105;
    int row = 0;
    for (double y = y0 + 0.014; y < y1 - 0.01; row++, y += r * 1.55) {
      for (double x = xi + 0.016 + (row % 2) * r * 0.9; x < xo(y) - 0.012; x += r * 1.8) {
        cells.push_back(Matrix4().makeTranslation(s * x, y, zf - 0.004));
      }
    }
    // the fog lamp at the outer end
    const Frame fl(v3(s * (halfAt(0.373) - 0.07), 0.373, zf - 0.002), v3(-1, 0, 0), v3(0, 1, 0), v3(0, 0, -1));
    out.add(K.CHROME, fl.ring(0.03, 0.005, 0, 0, 0.004, 20), fl.disc(0.029, 0, 0, 0.002, 20));
    out.add(ext.head, fl.cyl(0.016, 0.01, 0, 0, 0.007, 16));
  }
  // one hexagonal cell, drawn as many times as there are cells
  std::vector<GeometryPtr> hex;
  for (int i = 0; i < 6; i++) {
    const double a = (i / 6.0) * PI * 2 + PI / 6;
    GeometryPtr b = boxGeometry(0.0108, 0.0022, 0.006);
    b->translate(0, 0.0094, 0).rotateZ(a);
    hex.push_back(b);
  }
  const GeometryPtr cellGeo = tubeFree(hex);
  const auto honeycomb = make<InstancedMesh>(cellGeo, K.PLASTIC, static_cast<int>(cells.size()));
  for (size_t i = 0; i < cells.size(); i++) honeycomb->setMatrixAt(static_cast<int>(i), cells[i]);
  honeycomb->name = "grille mesh";
  ext.group->add(honeycomb);
  added_.push_back(honeycomb);

  // ---- number plates with their seals, in holders ------------------------------
  const Frame plateF(v3(0, 0.36, zf - 0.017), v3(-1, 0, 0), v3(0, 1, 0), v3(0, 0, -1));
  const Frame plateR(v3(0, 0.66, sh.zRear + 0.017), v3(1, 0, 0), v3(0, 1, 0), v3(0, 0, 1));
  for (const auto& [f, rearPlate] : {std::pair<const Frame*, bool>{&plateF, false}, std::pair<const Frame*, bool>{&plateR, true}}) {
    out.add(K.GLOSS_BLACK, slab(*f, rounded(0.548, 0.136, 0.012), -0.006, 0.002));
    for (const double u : {-0.205, 0.205}) out.add(K.CHROME, f->cyl(0.0045, 0.004, u, 0, 0.0045, 10));
    GeometryPtr pg = planeGeometry(0.52, 0.11);
    pg->translate(0, 0, 0.0025);
    const auto plate = make<Mesh>(f->put(pg), basic({.map = plateTexture("KS FS 2026", rearPlate)}));
    plate->name = rearPlate ? "number plate rear" : "number plate front";
    ext.group->add(plate);
    added_.push_back(plate);
  }

  // ---- badges on the nose and the tailgate --------------------------------------
  const auto badge = [&](const std::optional<Hit>& h, const Vector3& up) {
    if (!h) return;
    const Frame f = Frame::on(h->p, h->n, up);
    const std::vector<UV> oval = rounded(0.085, 0.046, 0.0225, 4);
    out.add(K.CHROME, slab(f, oval, 0.001, 0.005));
    out.add(K.ENAMEL, slab(f, rounded(0.074, 0.036, 0.018, 4), 0.005, 0.0065));
    out.add(K.CHROME, f.box(0.052, 0.006, 0.003, 0, 0, 0.007), f.box(0.006, 0.026, 0.003, -0.012, 0, 0.007));
  };
  badge(body.hit(v3(0, 0.86, zO), front), v3(0, 0.8, 0.6));
  badge(body.hit(v3(0, 1.08, zT), rear), v3(0, 0.8, -0.6));

  // ---- door handles (body colour, in a dark recess) and the lock of the driver's door ----
  Parts handles;
  for (const double s : {-1.0, 1.0}) {
    for (const auto& [z, y] : {UV{sh.zPillarB - 0.24, belt(sh.zPillarB - 0.24) - 0.095}, UV{sh.zAxleR - 0.37, belt(sh.zAxleR - 0.37) - 0.095}}) {
      const std::optional<Hit> h = body.onto(v3(s * hw, y, z), v3(-s, 0, 0));
      if (!h) continue;
      const Frame f = Frame::on(h->p, h->n);
      out.add(K.GAP, slab(f, rounded(0.15, 0.05, 0.022, 3), 0.0004, 0.0016));
      handles.add(paint, tubeOf({ringIn(f, rounded(0.135, 0.022, 0.01, 3), 0.004), ringIn(f, rounded(0.14, 0.026, 0.012, 3), 0.014), ringIn(f, rounded(0.13, 0.02, 0.009, 3), 0.022)}));
      if (s < 0 && z < sh.zPillarB) out.add(K.CHROME, f.cyl(0.0085, 0.006, s * 0.058, 0, 0.018, 12));
    }
  }
  keep(handles.build(*ext.group, "door handles", true), 0);

  // ---- the fuel flap on the right rear wing --------------------------------------
  {
    const std::optional<Hit> h = body.onto(v3(hw, 0.79, sh.zAxleR + 0.02), v3(-1, 0, 0));
    if (h) {
      const Frame f = Frame::on(h->p, h->n);
      std::vector<Hit> loop;
      for (const auto& [u, v] : rounded(0.17, 0.115, 0.03, 3)) loop.push_back(Hit{f.at(u, v), h->n});
      const Hit l0 = loop[0];
      const Hit l1 = loop[1];
      loop.push_back(l0);
      loop.push_back(l1);
      out.add(K.GAP, ribbon(loop, 0.005, 0.0022));
      out.add(K.GAP, f.box(0.012, 0.03, 0.002, -0.075, 0, 0.002));
    }
  }

  // ---- the exhaust tip under the tail, the antenna on the roof --------------------
  {
    const Frame f(v3(-0.44, 0.27, sh.zRear - 0.03), v3(1, 0, 0), v3(0, 1, 0), v3(0, 0, 1));
    out.add(K.CHROME, f.cyl(0.034, 0.13, 0, 0, 0, 18), f.ring(0.034, 0.003, 0, 0, 0.065, 18));
    out.add(K.GAP, f.disc(0.03, 0, 0, 0.064, 18));
    const std::optional<Hit> roof = body.onto(v3(0, 2, sh.zRoofRear - 0.12), down);
    if (roof) {
      std::vector<std::vector<V3>> fin;
      for (const auto& [dz, hh, ww] : {std::array<double, 3>{-0.085, 0.004, 0.012}, std::array<double, 3>{-0.05, 0.026, 0.036}, std::array<double, 3>{0.0, 0.05, 0.048},
                                       std::array<double, 3>{0.045, 0.058, 0.046}, std::array<double, 3>{0.07, 0.034, 0.042}, std::array<double, 3>{0.08, 0.006, 0.036}}) {
        const Vector3 b = roof->p.clone().add(v3(0, -0.003, dz));
        std::vector<V3> ring;
        for (const auto& [x, y] : {UV{-ww / 2, 0}, UV{ww / 2, 0}, UV{ww * 0.32, hh * 0.72}, UV{0, hh}, UV{-ww * 0.32, hh * 0.72}}) ring.push_back({b.x + x, b.y + y, b.z});
        fin.push_back(ring);
      }
      out.add(K.GLOSS_BLACK, tubeOf(fin));
    }
  }

  // ---- the indicator repeaters on the door mirrors' housings ----------------------
  for (const Mirror* m : car.mirrors) {
    if (m->spec.name == "inside") continue;
    const double s = m->spec.name == "left" ? -1 : 1;
    const Vector3 n = m->spec.normal.clone().normalize();
    const Vector3 up = v3(0, 1, 0).addScaledVector(n, -n.y).normalize();
    const Vector3 right = Vector3().crossVectors(up, n);
    const Vector3 outward = right.clone().multiplyScalar(s);
    const Vector3 c = m->spec.centre.clone().addScaledVector(n, -0.055).addScaledVector(outward, m->spec.width / 2 - 0.002).addScaledVector(up, -m->spec.height * 0.18);
    const Frame f(c, n.clone().negate(), up, outward);
    out.add(s < 0 ? ext.blinkL : ext.blinkR, f.box(0.055, 0.016, 0.006));
  }

  keep(out.build(*ext.group, "body detail", false), 0);

  // ---- the wheels ---------------------------------------------------------------------
  const CarDims& d = ext.dims;
  wheels_ = std::make_unique<WheelSet>(WheelSpec{.radius = d.wheelRadius, .width = d.tyreWidth, .rim = d.wheelRadius * 0.66, .spokes = 10, .nuts = 5, .disc = true},
                                       std::vector<int>{-1, 1, -1, 1}, WheelSet::Materials{K.TYRE, K.METAL, K.CALIPER});
  ext.group->add(wheels_->group);
  added_.push_back(wheels_->group);
  for (const std::shared_ptr<Group>& sp : ext.wheelSpinners) hide(sp->children);
  // (where the plain ones stand now – before the first update, in a menu, the instances would all stand in the middle)
  placeWheels();

  // ================================================================= inside ====
  const MaterialPtr soft = cabinMaterial(0x40444b);
  const MaterialPtr hard = cabinMaterial(0x575d65);
  const MaterialPtr pale = cabinMaterial(0xb9bbb8);
  const MaterialPtr silver = cabinMaterial(0xb9bec4, 50);
  const MaterialPtr black = cabinMaterial(0x1a1c20);
  const MaterialPtr gloss = cabinMaterial(0x16181b, 60);
  const MaterialPtr grip = cabinMaterial(0x1f2125, 12);
  const MaterialPtr stitch = cabinMaterial(0x8d939a);
  const MaterialPtr red = cabinMaterial(0xb3241c, 30);
  (void)soft;
  Parts inside;
  const std::shared_ptr<Group>& cockpit = car.cockpit;
  const double ex = car.eye.x;
  const double zc = sh.zCowl;
  const double xe = hw * 0.975 - 0.022;
  // (the dashboard's shape as render/car.ts buildDashboard makes it)
  const auto browZ = [&](double x) { return zc + 0.385 + 0.05 * std::pow(std::fabs(x / xe), 3); };
  const auto browY = [&](double x) { return sh.beltY + 0.002 - 0.012 * std::pow(x / xe, 2); };
  const auto cowlAt = [&](double x) { return sh.cowlY + (sh.cowlCrownY - sh.cowlY) * (1 - std::fabs(x) / (hw * 0.9)); };
  const auto dashTop = [&](double x, double z) {
    const double zf0 = zc + 0.004;
    const double yf = cowlAt(x) - 0.014;
    const double s = jsMax(0, jsMin(1, (z - zf0) / (browZ(x) - 0.032 - zf0)));
    return yf + (browY(x) - yf) * s + 0.006 * std::sin(PI * s);
  };
  const auto onDash = [&](double x, double z, double lift = 0.0015) { return Frame(v3(x, dashTop(x, z) + lift, z), v3(1, 0, 0), v3(0, 0, -1), v3(0, 1, 0)); };

  // ---- the dashboard: defroster grille along the windscreen, tweeters, buttons ----
  {
    const double z = zc + 0.06;
    std::vector<std::vector<Hit>> rows;
    for (const double dz : {-0.022, 0.022}) {
      std::vector<Hit> row;
      for (int i = 0; i <= 12; i++) {
        const double x = -0.3 + 0.05 * i;
        row.push_back(Hit{v3(x, dashTop(x, z + dz) + 0.0015, z + dz), v3(0, 1, 0)});
      }
      rows.push_back(row);
    }
    inside.add(black, sheetRows(rows));
    for (int k = 0; k < 6; k++) {
      const double dz = -0.016 + k * 0.0064;
      std::vector<Hit> row;
      for (int i = 0; i <= 12; i++) {
        const double x = -0.29 + (0.58 * i) / 12;
        row.push_back(Hit{v3(x, dashTop(x, z + dz) + 0.0035, z + dz), v3(0, 1, 0)});
      }
      inside.add(hard, ribbon(row, 0.0022, 0));
    }
    for (const double s : {-1.0, 1.0}) {
      const Frame f = onDash(s * 0.6, zc + 0.15);
      inside.add(black, f.disc(0.03, 0, 0, 0.0005, 18));
      inside.add(silver, f.ring(0.031, 0.0022, 0, 0, 0.001, 18));
      // the side windows' demister slots at the ends
      const Frame g = onDash(s * (xe - 0.06), zc + 0.12);
      inside.add(black, g.box(0.05, 0.09, 0.002, 0, 0, 0.001));
    }
    // hazard button between the middle vents, on the face of the centre stack
    const double ys = browY(0);
    const double zs = browZ(0);
    const double lean = std::atan2(0.052 - 0.03, ys - 0.075 - 0.72);
    const auto onFace = [&](double y) { return zs + 0.03 + (ys - 0.075 - y) * std::tan(lean); };
    const double yh = ys - 0.118;
    const Frame fh(v3(0, yh, onFace(yh)), v3(1, 0, 0), v3(0, std::cos(lean), -std::sin(lean)));
    inside.add(red, fh.box(0.02, 0.016, 0.008, 0, 0, 0.004));
    // the start button right of the steering column, the light switch left of it
    const auto lowerZ = [&](double x, double y) { return browZ(x) + 0.004 - ((browY(x) - 0.1 - y) / 0.1) * 0.002; };
    const Frame fs(v3(ex + 0.145, 0.8, lowerZ(ex + 0.145, 0.8)), v3(1, 0, 0), v3(0, 1, 0), v3(0, 0, 1));
    inside.add(silver, fs.ring(0.0135, 0.0028, 0, 0, 0.004, 18));
    inside.add(black, fs.cyl(0.012, 0.006, 0, 0, 0.004, 18));
    const Frame fl(v3(ex - 0.26, 0.82, lowerZ(ex - 0.26, 0.82)), v3(1, 0, 0), v3(0, 1, 0), v3(0, 0, 1));
    inside.add(silver, fl.ring(0.02, 0.003, 0, 0, 0.004, 18));
    inside.add(black, fl.cyl(0.017, 0.014, 0, 0, 0.008, 18), fl.box(0.006, 0.026, 0.006, 0, 0, 0.017));
    // the glove box's lock
    const double gx = 0.62;
    const Frame fg(v3(gx, browY(gx) - 0.124, browZ(gx) + 0.01), v3(1, 0, 0), v3(0, 1, 0), v3(0, 0, 1));
    inside.add(silver, fg.cyl(0.009, 0.005, 0, 0, 0.002, 14));
    inside.add(black, fg.box(0.003, 0.009, 0.002, 0, 0, 0.005));
  }

  // ---- doors: switch panels, lock pins, pockets, tweeters -------------------------
  {
    const double xg = hw * 0.975 - 0.006;
    const auto sill = [&](double z) { return sh.beltY - 0.004 - (sh.beltY - (sh.cowlY - 0.015)) * jsMax(0, jsMin(1, (sh.zRoofFront - z) / (sh.zRoofFront - zc))); };
    const double armY = 0.735;
    struct Door {
      double s, z0, z1;
      bool frontDoor;
    };
    const Door doors[] = {
        {-1, zc + 0.1, sh.zPillarB - 0.04, true},
        {1, zc + 0.1, sh.zPillarB - 0.04, true},
        {-1, sh.zPillarB + 0.04, sh.zRoofRear - 0.15, false},
        {1, sh.zPillarB + 0.04, sh.zRoofRear - 0.15, false},
    };
    for (const Door& dr : doors) {
      const double s = dr.s, z0 = dr.z0, z1 = dr.z1;
      const double len = z1 - z0;
      const double zp = z0 + len * 0.6;
      // the switch panel on the armrest (the driver's has its four switches already)
      const Frame sp(v3(s * (xg - 0.12), armY + 0.034, zp - len * 0.18), v3(0, 0, 1), v3(-s * std::sin(0.12), std::cos(0.12), 0));
      inside.add(gloss, sp.box(0.1, 0.004, 0.03));
      if (!(dr.frontDoor && s < 0)) inside.add(silver, sp.box(0.022, 0.006, 0.012, 0, 0.004, 0));
      // the lock pin at the back of the sill
      const double zl = z1 - 0.07;
      inside.add(silver, Frame(v3(s * (xg - 0.035), sill(zl) + 0.006, zl), v3(1, 0, 0), v3(0, 0, -1), v3(0, 1, 0)).cyl(0.0045, 0.024, 0, 0, 0.004, 10));
      // the pocket along the foot of the panel
      inside.add(hard, Frame(v3(s * (xg - 0.095), 0.37, z0 + len * 0.5), v3(0, 0, 1), v3(0, 1, 0)).box(len * 0.72, 0.05, 0.016));
      // a tweeter in the front corner of a front door
      if (dr.frontDoor) {
        const double zt0 = z0 + 0.07;
        const Frame ft(v3(s * (xg - 0.077), sill(zt0) - 0.045, zt0), v3(0, 0, s), v3(0, 1, 0), v3(-s, 0, 0));
        inside.add(black, ft.disc(0.022, 0, 0, 0.002, 16));
        inside.add(silver, ft.ring(0.023, 0.0022, 0, 0, 0.002, 16));
      }
    }
  }

  // ---- seat-belt buckles, the belts' height adjusters on the B pillars ----------------
  for (const double s : {-1.0, 1.0}) {
    const double x = s * 0.106;
    const Frame f(v3(x, 0.52, 0.29), v3(0, 0, 1), v3(0, 1, 0));
    inside.add(black, f.box(0.035, 0.07, 0.016), f.box(0.02, 0.12, 0.012, 0, -0.09, 0));
    inside.add(red, f.box(0.012, 0.008, 0.012, 0, 0.037, 0));
    // (the pillar's lining runs up and in along the side glass)
    const auto p = at(sh.zPillarB);
    const double x4 = p[4][0], y4 = p[4][1];
    const double x5 = p[5][0], y5 = p[5][1];
    const double yb = 1.15;
    const double xb = x4 + ((x5 - x4) * (yb - y4)) / (y5 - y4) - 0.03;
    const Frame pb(v3(s * xb, yb, sh.zPillarB), v3(0, 0, s), v3(s * (x5 - x4), y5 - y4, 0).normalize());
    inside.add(black, pb.box(0.016, 0.08, 0.006));
  }

  // ---- the roof: sun-visor clips and mirror lids, grab handles, the console's buttons, the mirror's sensor ----
  {
    const auto roofAt = [&](double x) { return sh.roofY - 0.004 - 0.051 * (std::fabs(x) / (hw * 0.74)) - 0.025; };
    for (const double s : {-1.0, 1.0}) {
      const double vx = s * 0.465;
      const Frame vf(v3(vx, roofAt(0.465) - 0.008, sh.zRoofFront + 0.105), v3(std::cos(0.077), -s * std::sin(0.077), 0), v3(s * std::sin(0.077), std::cos(0.077), 0));
      inside.add(hard, vf.box(0.17, 0.002, 0.085, 0.02, -0.0075, 0));
      inside.add(pale, Frame(v3(s * 0.285, roofAt(0.285) - 0.012, sh.zRoofFront + 0.06), v3(1, 0, 0), v3(0, 1, 0)).box(0.03, 0.012, 0.026));
      inside.add(pale, Frame(v3(s * 0.66, roofAt(0.66) - 0.012, sh.zRoofFront + 0.06), v3(1, 0, 0), v3(0, 1, 0)).box(0.035, 0.016, 0.03));
    }
    // grab handles over the passenger's door and the rear doors
    for (const auto& [s, z] : {UV{1, (sh.zRoofFront + sh.zPillarB) / 2 + 0.02}, UV{-1, (sh.zPillarB + sh.zRoofRear) / 2}, UV{1, (sh.zPillarB + sh.zRoofRear) / 2}}) {
      // (from the corner of the lining, where roof and side glass meet, down and in)
      const double x = s * (at(z)[5][0] - 0.03);
      const double y = at(z)[5][1] - 0.03;
      const Frame f(v3(x, y, z), v3(0, 0, 1), v3(s * 0.5, 0.87, 0));
      inside.add(hard, f.box(0.19, 0.014, 0.016, 0, -0.045, 0), f.box(0.014, 0.045, 0.014, -0.085, -0.0225, 0), f.box(0.014, 0.045, 0.014, 0.085, -0.0225, 0));
    }
    const double cz = sh.zRoofFront + 0.2;
    const double cy = roofAt(0.03) - 0.014;
    for (const double dx : {0.012, 0.048}) inside.add(black, Frame(v3(dx, cy - 0.001, cz - 0.04), v3(1, 0, 0), v3(0, 0, -1), v3(0, -1, 0)).box(0.014, 0.012, 0.004));
    inside.add(red, Frame(v3(0.03, cy - 0.001, cz + 0.042), v3(1, 0, 0), v3(0, 0, -1), v3(0, -1, 0)).box(0.018, 0.01, 0.005));
    const Mirror* mirror = nullptr;
    for (const Mirror* m : car.mirrors)
      if (m->spec.name == "inside") {
        mirror = m;
        break;
      }
    if (mirror) {
      const Vector3& c = mirror->spec.centre;
      inside.add(black, Frame(v3(c.x + 0.035, sh.roofY - 0.05, sh.zRoofFront - 0.008), v3(1, 0, 0), v3(0, 1, 0)).box(0.085, 0.034, 0.045));
    }
  }
  keep(inside.build(*cockpit, "cockpit detail"), 0);

  // ---- the instrument cluster: dials in chrome rings, needle hubs, the centre display's frame ----
  if (Object3D* dialObj = cockpit->getObjectByName("cluster"); dialObj && dialObj->type == ObjectType::Mesh) {
    Mesh& dial = static_cast<Mesh&>(*dialObj);
    // (the PlaneGeometry's parameters: its width and height, as its bounds have them)
    dial.geometry->computeBoundingBox();
    const double w = dial.geometry->boundingBox->max.x - dial.geometry->boundingBox->min.x;
    const double h = dial.geometry->boundingBox->max.y - dial.geometry->boundingBox->min.y;
    const double k = w / 768;
    Parts cl;
    const Frame f(v3(0, 0, 0), v3(1, 0, 0), v3(0, 1, 0));
    for (const double u : {(150 - 384) * k, (618 - 384) * k}) {
      const double r = 116 * k;
      cl.add(silver, f.ring(r + 0.002, 0.0017, u, 0, 0.004, 40));
      // the cup round the dial (seen from inside) and the needle's hub
      GeometryPtr cup = lathe({{r + 0.004, 0.0035}, {r + 0.004, 0}}, 32);
      cup->rotateY(-PI / 2).translate(u, 0, 0);
      cl.add(black, cup);
      cl.add(gloss, f.cyl(0.0075, 0.005, u, 0, 0.0025, 14));
    }
    const double cw = 100 * k;
    const double chh = (100.0 / 256) * h;
    for (const auto& [bw, bh, bu, bv] :
         {std::array<double, 4>{2 * cw + 0.004, 0.002, 0, chh}, std::array<double, 4>{2 * cw + 0.004, 0.002, 0, -chh}, std::array<double, 4>{0.002, 2 * chh, -cw, 0}, std::array<double, 4>{0.002, 2 * chh, cw, 0}})
      cl.add(silver, f.box(bw, bh, 0.002, bu, bv, 0.001));
    // (solid like the instruments when the cabin is see-through)
    keep(cl.build(dial, "cluster detail"), -1);
  }

  // ---- the steering wheel: airbag pad with its badge, buttons on the spokes, grips, stitching ----
  {
    Parts sw;
    const double R = 0.17;
    const auto ring = [&](double a, double b, double c, double z, int n = 18) {
      std::vector<V3> pts;
      for (int i = 0; i < n; i++) {
        const double phi = (static_cast<double>(i) / n) * PI * 2;
        const double cs = std::cos(phi);
        const double sn = std::sin(phi);
        pts.push_back({a * sign(cs) * std::sqrt(std::fabs(cs)), (sn >= 0 ? b : c) * sign(sn) * std::sqrt(std::fabs(sn)) - 0.004, z});
      }
      return pts;
    };
    sw.add(grip, tubeOf({ring(0.06, 0.039, 0.045, -0.0215), ring(0.058, 0.037, 0.043, -0.015), ring(0.05, 0.031, 0.037, -0.0105), ring(0.03, 0.019, 0.022, -0.0088)}));
    const Frame fb(v3(0, -0.002, -0.0082), v3(1, 0, 0), v3(0, 1, 0));
    sw.add(silver, fb.ring(0.0105, 0.0016, 0, 0, 0, 20), fb.box(0.016, 0.003, 0.0015, 0, 0, 0.0005));
    for (const double s : {-1.0, 1.0}) {
      const double ry = s * 0.2;
      const Frame pf(v3(s * 0.098, -0.004, -0.0125), v3(std::cos(ry), 0, -std::sin(ry)), v3(0, 1, 0));
      sw.add(silver, pf.ring(0.0075, 0.0013, -s * 0.006, 0.002, 0.0028, 16));
      sw.add(gloss, pf.cyl(0.0042, 0.003, -s * 0.006, 0.002, 0.0028, 12));
      for (const double dv : {-0.0075, 0.0075}) sw.add(gloss, pf.box(0.008, 0.0055, 0.003, s * 0.011, dv, 0.0028));
    }
    // the grips at a quarter to three, the stitching round the inside of the rim
    for (const double centre : {0.0, PI}) {
      GeometryPtr g = torusGeometry(R, 0.0164, 8, 10, 0.85);
      g->rotateZ(centre - 0.425);
      sw.add(grip, g);
    }
    {
      GeometryPtr g = torusGeometry(R - 0.009, 0.0008, 4, 72);
      g->translate(0, 0, 0.0122);
      sw.add(stitch, g);
    }
    keep(sw.build(*car.steering, "steering wheel detail"), 1);
  }

  // ---- ribbed ends of the stalks -------------------------------------------------------
  {
    Parts st;
    for (const double x : {-0.19, -0.21}) {
      GeometryPtr g = torusGeometry(0.0108, 0.0011, 5, 16);
      g->rotateY(PI / 2).translate(x, 0, 0);
      st.add(silver, g);
    }
    keep(st.build(*car.stalk, "stalk detail"), 1);
    Object3D* column = car.stalk->parent;
    if (column) {
      const auto right = make<Group>();
      right->position.set(0, 0.04, -0.085);
      right->rotation.z = -0.06;
      column->add(right);
      added_.push_back(right);
      Parts sr;
      for (const double x : {0.19, 0.21}) {
        GeometryPtr g = torusGeometry(0.0108, 0.0011, 5, 16);
        g->rotateY(PI / 2).translate(x, 0, 0);
        sr.add(silver, g);
      }
      for (const std::shared_ptr<Mesh>& m : sr.build(*right, "stalk detail")) ghosts.emplace_back(m, 1);
    }
  }

  // ---- the gear knob's shift pattern, the selector's P R N D, a chrome collar --------------
  {
    Parts lv;
    {
      GeometryPtr g = torusGeometry(0.0105, 0.0024, 6, 18);
      g->rotateX(PI / 2).translate(0, 0.182, 0);
      lv.add(silver, g);
    }
    keep(lv.build(*car.lever, "gear lever detail"), 1);
    const TexturePtr tex = canvasTexture(patternCanvas());
    tex->srgb = true;
    const auto top = make<Mesh>(knobCap(), lambert({.map = tex, .emissiveMap = tex, .emissive = Color(0x444444)}));
    top->name = "shift pattern";
    car.lever->add(top);
    added_.push_back(top);
    pattern_ = top;
    selectorCanvas_ = Canvas(64, 128);
    selectorTexture_ = canvasTexture(selectorCanvas_);
    selectorTexture_->srgb = true;
    GeometryPtr sg = planeGeometry(0.03, 0.06);
    sg->rotateX(-PI / 2).translate(0.075, 0.5115, -0.12);
    selector_ = make<Mesh>(sg, basic({.map = selectorTexture_, .toneMapped = false}));
    selector_->name = "selector display";
    cockpit->add(selector_);
    added_.push_back(selector_);
  }

  // ---- the handbrake's slot, ribbed rubber on the pedals --------------------------------------
  {
    Parts hb;
    hb.add(black, Frame(v3(0.058, 0.5105, 0.12), v3(1, 0, 0), v3(0, 0, -1), v3(0, 1, 0)).box(0.05, 0.18, 0.002));
    keep(hb.build(*cockpit, "handbrake detail"), 0);
    for (size_t i = 0; i < car.pedals.size(); i++) {
      Parts pp;
      const bool throttle = i == 2;
      const int n = throttle ? 8 : 5;
      for (int k = 0; k < n; k++) {
        const double y = throttle ? -0.25 + (k * 0.1) / (n - 1) : -0.252 + (k * 0.044) / (n - 1);
        GeometryPtr g = boxGeometry(throttle ? 0.04 : 0.062, 0.0035, 0.004);
        g->translate(0, y, 0.0145);
        pp.add(black, g);
      }
      keep(pp.build(*car.pedals[i], "pedal detail"), 0);
    }
  }
}

void CarDetail::keep(const std::vector<std::shared_ptr<Mesh>>& meshes, int layer) {
  for (const std::shared_ptr<Mesh>& m : meshes) {
    added_.push_back(m);
    // (transparent ones – the lamps' clear covers – stay as they are)
    if (layer >= 0 && !m->material->transparent) ghosts.emplace_back(m, layer);
  }
}

void CarDetail::hide(const std::vector<ObjectPtr>& objects) { plain_.insert(plain_.end(), objects.begin(), objects.end()); }

void CarDetail::show(bool on) {
  on_ = on;
  for (const ObjectPtr& o : added_) o->visible = on;
  for (const ObjectPtr& o : plain_) o->visible = !on;
  pattern_->visible = on && !automatic_;
  selector_->visible = on && automatic_;
  if (on) selectorShown_ = "";
}

void CarDetail::placeWheels() {
  CarExterior& ext = *car_.exterior;
  for (int i = 0; i < 4; i++) {
    const Group& w = *ext.wheels[static_cast<size_t>(i)];
    wheels_->place(i, wheelPos_.copy(w.position), -w.rotation.y, -ext.wheelSpinners[static_cast<size_t>(i)]->rotation.x);
  }
}

void CarDetail::update(const CarVisualState& s) {
  placeWheels();
  automatic_ = s.automatic;
  pattern_->visible = on_ && !s.automatic;
  const std::string want = s.automatic ? jsStr(s.gear) : "";
  selector_->visible = on_ && s.automatic;
  if (s.automatic && want != selectorShown_) {
    selectorShown_ = want;
    drawSelector(selectorCanvas_, static_cast<int>(s.gear));
    selectorTexture_->needsUpdate();
  }
}

}  // namespace fs
