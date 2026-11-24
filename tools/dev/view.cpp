// SPDX-License-Identifier: AGPL-3.0-only
// LINK: game render engine world core platform geo
/**
 * A picture of the map without a browser: opens the map from disk, loads what lies
 * around a place, draws it off screen and writes a PNG – and says what the frame
 * took (draw calls, triangles, milliseconds).
 *
 *   tools/t.sh view out.png [--at X,Z] [--height H] [--yaw DEG] [--pitch DEG] [--size WxH]
 *                           [--radius M] [--shadows SIZE] [--frames N] [--map DIR] [--sun ELEVATION,AZIMUTH]
 *
 * --at: the place looked from, in world metres (default 0,0: Baunatal); --height: metres above
 * the ground there (default 60); --yaw: which way (0 north, 90 east); --pitch: down is negative.
 */
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <array>
#include <functional>
#include <map>
#include <string>

#include "game/app.h"
#include "platform/glhost.h"
#include "platform/native/native.h"
#include "platform/plat.h"

#include <GLES3/gl3.h>

using namespace fs;

static double now() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

int main(int argc, char** argv) {
  std::string out = "view.png", mapDir = "/home/user/driving-simulator/public/map/";
  double x = 0, z = 0, height = 60, yaw = 0, pitch = -25, radius = 1400, sunEl = 52, sunAz = 215;
  int width = 1280, height_px = 720, shadows = 3072, frames = 30;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
    if (a == "--at") std::sscanf(next(), "%lf,%lf", &x, &z);
    else if (a == "--height") height = std::atof(next());
    else if (a == "--yaw") yaw = std::atof(next());
    else if (a == "--pitch") pitch = std::atof(next());
    else if (a == "--size") std::sscanf(next(), "%dx%d", &width, &height_px);
    else if (a == "--radius") radius = std::atof(next());
    else if (a == "--shadows") shadows = std::atoi(next());
    else if (a == "--frames") frames = std::atoi(next());
    else if (a == "--map") mapDir = next();
    else if (a == "--sun") std::sscanf(next(), "%lf,%lf", &sunEl, &sunAz);
    else out = a;
  }
  if (!mapDir.empty() && mapDir.back() != '/') mapDir += '/';
  plat::native::mount(plat::locationBase() + "map/", mapDir);
  plat::native::setViewport(width, height_px, 1);
  if (!plat::gl::createContext("view", width, height_px)) {
    std::fprintf(stderr, "no OpenGL ES here\n");
    return 1;
  }
  std::printf("driver: %s\n", plat::gl::rendererName().c_str());

  App app(dom::el("canvas"));
  double t0 = now();
  plat::native::wait(app.loadWorld());
  std::printf("map %s opened in %.0f ms\n", app.map->version.c_str(), now() - t0);
  app.tiles->viewRadius = radius;
  app.sky.setSun(sunEl, sunAz);
  if (shadows > 0) app.setShadows(shadows, 220);

  // everything around the place: the road network's pieces and the tiles
  t0 = now();
  for (int i = 0; i < 100000; i++) {
    app.map->update(x, z);
    app.tiles->update(x, z);
    if (plat::native::pump() == 0 && app.tiles->settled() && app.map->ready(x, z)) break;
  }
  std::printf("%zu tiles, %zu roads, %zu junctions loaded in %.0f ms\n", app.tiles->tiles.size(), app.world->allEdges.size(), app.world->allNodes.size(), now() - t0);

  double ground = app.tiles->heightAt(x, z);
  if (std::isnan(ground)) ground = 0;
  Camera& cam = *app.camera;
  cam.position.set(x, ground + height, z);
  cam.rotation.set(pitch * PI / 180, -yaw * PI / 180, 0, Euler::YXZ);
  Vector3 focus(x, ground, z);

  // the first frame makes the programs and uploads everything; the ones after it are what a frame costs
  t0 = now();
  app.render(focus);
  double first = now() - t0;
  double sum = 0, worst = 0;
  for (int i = 0; i < frames; i++) {
    double a = now();
    app.render(focus);
    uint8_t px[4];
    app.renderer.readPixels(0, 0, 1, 1, px);   // (wait for the graphics card: the time is the frame's, not the queue's)
    double d = now() - a;
    sum += d;
    if (d > worst) worst = d;
  }
  const Renderer::Info& info = app.renderer.info;
  std::printf("first frame %.0f ms; then %.1f ms a frame (worst %.1f) = %.0f fps; %d draw calls, %d triangles, %d culled, %d programs, %d geometries, %d textures\n", first,
              frames > 0 ? sum / frames : 0, worst, frames > 0 && sum > 0 ? 1000.0 * frames / sum : 0, info.calls, info.triangles, info.culled, info.programs, info.geometries,
              info.textures);
  // what the picture is made of: triangles by the kind of mesh (those in the picture; "+" what else is loaded)
  {
    Matrix4 ps;
    ps.multiplyMatrices(cam.projectionMatrix, cam.matrixWorldInverse);
    Frustum fr;
    fr.setFromProjectionMatrix(ps);
    std::map<std::string, std::array<long, 4>> by;   // drawn meshes, drawn triangles, all meshes, all triangles
    std::function<void(Object3D&, bool)> walk = [&](Object3D& o, bool shown) {
      shown = shown && o.visible;
      if (o.type == ObjectType::Mesh || o.type == ObjectType::InstancedMesh) {
        Mesh& m = static_cast<Mesh&>(o);
        if (m.geometry) {
          BufferGeometry& g = *m.geometry;
          long tris = static_cast<long>((g.index ? g.index->count() : (g.position() ? g.position()->count() : 0)) / 3);
          if (o.type == ObjectType::InstancedMesh) tris *= static_cast<InstancedMesh&>(o).count;
          std::string name = o.name.empty() ? (o.parent && o.parent->type == ObjectType::LOD ? "terrain" : "(unnamed)") : o.name;
          // (an attribute with another number of items than the positions: WebGL refuses such a draw)
          if (g.position()) {
            const size_t nv = g.position()->count();
            for (const auto& kv : g.attributes)
              if (kv.second && kv.second->count() != nv && kv.second->divisor == 0)
                std::printf("  MISMATCH %s: %s has %zu items for %zu vertices (array %zu, itemSize %d)\n", name.c_str(), kv.first.c_str(), kv.second->count(), nv,
                            kv.second->length(), kv.second->itemSize);
          }
          auto& e = by[name];
          e[2]++;
          e[3] += tris;
          bool in = shown;
          if (in && o.frustumCulled) {
            if (!g.boundingSphere) g.computeBoundingSphere();
            Sphere sp = *g.boundingSphere;
            sp.applyMatrix4(o.matrixWorld);
            in = fr.intersectsSphere(sp);
          }
          if (in) {
            e[0]++;
            e[1] += tris;
          }
        }
      }
      for (const ObjectPtr& c : o.children) walk(*c, shown);
    };
    walk(*app.scene, true);
    for (const auto& kv : by)
      std::printf("  %-12s %4ld meshes %8ld triangles in the picture  (of %4ld, %8ld loaded)\n", kv.first.c_str(), kv.second[0], kv.second[1], kv.second[2], kv.second[3]);
  }
  std::printf("%s: %s\n", out.c_str(), plat::gl::savePng(out, app.renderer.bufferWidth(), app.renderer.bufferHeight()) ? "written" : "NOT written");
  return 0;
}
