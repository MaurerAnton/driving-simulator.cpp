// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/dirt.ts @ v1.7.7
#include "render/dirt.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "core/js.h"
#include "platform/plat.h"
#include "render/glsl_gen.h"

namespace fs {

namespace {

/** The uniforms every dirty material shares. */
struct DirtUniforms {
  UniformPtr uDirt = uniform(0.0);
  /** world → the vehicle's frame */
  UniformPtr uDirtFrame = uniform(Matrix4());
  /** how tall the vehicle is (m): the dirt climbs to about this height */
  UniformPtr uDirtHeight = uniform(1.45);
  UniformPtr uDirtColour = uniform(Color(0x6a5d4a));
  UniformPtr uDirtNoise = uniform(TexturePtr());
};
DirtUniforms& U() {
  static auto* u = new DirtUniforms();
  return *u;
}

/** A 64 × 64 tile of soft value noise (one channel). */
TexturePtr noiseTexture() {
  constexpr int N = 64;
  constexpr int g = 8;
  std::vector<float> grid(g * g);
  int64_t s = 1234567;
  const auto rand = [&s]() -> double {
    // (JavaScript's numbers: the product is exact up to 2^53, then the & keeps the low 31 bits)
    const double prod = static_cast<double>(s) * 1103515245.0 + 12345.0;
    s = static_cast<int64_t>(static_cast<uint32_t>(jsInt32(prod))) & 0x7fffffff;
    return static_cast<double>(s) / 0x7fffffff;
  };
  for (float& v : grid) v = static_cast<float>(rand());
  std::vector<uint8_t> data(N * N * 4);
  const auto at = [&](int i, int j) -> double { return grid[static_cast<size_t>(((j + g) % g) * g + ((i + g) % g))]; };
  for (int y = 0; y < N; y++) {
    for (int x = 0; x < N; x++) {
      const double fx = (static_cast<double>(x) / N) * g;
      const double fy = (static_cast<double>(y) / N) * g;
      const int i = static_cast<int>(std::floor(fx));
      const int j = static_cast<int>(std::floor(fy));
      const double tx = fx - i;
      const double ty = fy - j;
      const double sx = tx * tx * (3 - 2 * tx);
      const double sy = ty * ty * (3 - 2 * ty);
      const double a = at(i, j) + (at(i + 1, j) - at(i, j)) * sx;
      const double b = at(i, j + 1) + (at(i + 1, j + 1) - at(i, j + 1)) * sx;
      // (a fine grain on top of the soft blots: splashes)
      const double v = (a + (b - a) * sy) * 0.8 + rand() * 0.2;
      const size_t k = static_cast<size_t>((y * N + x) * 4);
      data[k] = data[k + 1] = data[k + 2] = static_cast<uint8_t>(jsRound(v * 255));
      data[k + 3] = 255;
    }
  }
  TexturePtr tex = dataTexture(std::move(data), N, N, PixelFormat::RGBA);
  tex->wrapS = tex->wrapT = Wrapping::Repeat;
  tex->magFilter = Filter::Linear;
  tex->minFilter = Filter::LinearMipmapLinear;
  tex->generateMipmaps = true;
  tex->needsUpdate();
  return tex;
}

/** Can this material get dirty (see above)?  (MeshStandardMaterial is drawn as Phong here.) */
bool dirtyable(const Material& m) {
  if (!(m.type == MaterialType::Lambert || m.type == MaterialType::Phong)) return false;
  return !m.transparent && m.side != Side::Back && !m.map;
}

/** Put the dirt into one material. */
void patch(Material& m) {
  // (once – told by the material's program key, not by its address)
  if (m.patch && m.patch->key.find("|dirt") != std::string::npos) return;
  DirtUniforms& u = U();
  if (!std::get<TexturePtr>(u.uDirtNoise->value)) u.uDirtNoise->value = noiseTexture();
  // the program's key: the material's own one and the dirt's – the name of the shader the original's patch leaves
  const std::string name = std::string(m.type == MaterialType::Phong ? "phong" : "lambert") + (m.patch ? m.patch->key : "") + "|dirt";
  ShaderPatchPtr next = gamePatch(name.c_str());
  if (!next) {
    plat::warn("dirt: no shader " + name);
    return;
  }
  if (m.patch) next->uniforms = m.patch->uniforms;
  next->uniforms["uDirt"] = u.uDirt;
  next->uniforms["uDirtFrame"] = u.uDirtFrame;
  next->uniforms["uDirtHeight"] = u.uDirtHeight;
  next->uniforms["uDirtColour"] = u.uDirtColour;
  next->uniforms["uDirtNoise"] = u.uDirtNoise;
  m.patch = next;
  m.needsUpdate();
}

}  // namespace

void DirtLook::apply(Object3D& top) {
  if (done_.count(&top)) return;
  done_.insert(&top);
  top.traverse([](Object3D& o) {
    if (o.type != ObjectType::Mesh && o.type != ObjectType::InstancedMesh) return;
    Mesh& mesh = static_cast<Mesh&>(o);
    if (mesh.material && dirtyable(*mesh.material)) patch(*mesh.material);
    for (const MaterialPtr& m : mesh.materials)
      if (m && dirtyable(*m)) patch(*m);
  });
}

void DirtLook::update(double level, const Object3D& root, double height) {
  DirtUniforms& u = U();
  u.uDirt->value = std::max(0.0, std::min(1.0, level));
  std::get<Matrix4>(u.uDirtFrame->value).copy(root.matrixWorld).invert();
  u.uDirtHeight->value = std::max(0.8, height);
}

}  // namespace fs
