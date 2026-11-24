// SPDX-License-Identifier: AGPL-3.0-only
// LINK: engine render platform core
/**
 * Do the shaders compile on this machine's graphics driver?  Puts the program of every
 * kind of material together (with and without shadows, fog, instances, the game's own
 * patches) and compiles and links each; says what failed.
 *
 *   tools/t.sh shader_probe [-v]      (-v: also list every program's uniforms)
 */
#include <GLES3/gl3.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "engine/shaders.h"
#include "platform/glhost.h"
#include "render/glsl_gen.h"

using namespace fs;

static bool compile(GLenum type, const std::string& src, GLuint& out, const std::string& what) {
  out = glCreateShader(type);
  const char* s = src.c_str();
  glShaderSource(out, 1, &s, nullptr);
  glCompileShader(out);
  GLint ok = 0;
  glGetShaderiv(out, GL_COMPILE_STATUS, &ok);
  if (ok) return true;
  char log[4000];
  glGetShaderInfoLog(out, sizeof log, nullptr, log);
  std::printf("FAILED %s (%s shader):\n%s\n", what.c_str(), type == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
  int errLine = 0;
  if (const char* e = std::strstr(log, "0:")) errLine = std::atoi(e + 2);
  int ln = 1;
  size_t at = 0;
  while (at < src.size()) {
    size_t end = src.find('\n', at);
    if (end == std::string::npos) end = src.size();
    if (errLine > 0 && ln >= errLine - 3 && ln <= errLine + 3) std::printf("%5d: %.*s\n", ln, static_cast<int>(end - at), src.c_str() + at);
    at = end + 1;
    ln++;
  }
  return false;
}

int main(int argc, char** argv) {
  bool verbose = argc > 1 && !std::strcmp(argv[1], "-v");
  if (!plat::gl::createContext("", 64, 64)) return 1;
  std::printf("driver: %s\n", plat::gl::rendererName().c_str());
  struct Case {
    std::string name;
    ShaderParams p;
    ShaderPatchPtr patch;
  };
  std::vector<Case> cases;
  auto add = [&](const std::string& name, MaterialType type, const std::function<void(ShaderParams&)>& set = nullptr, const char* game = nullptr) {
    Case c;
    c.name = name;
    c.p.type = type;
    bool lit = type == MaterialType::Lambert || type == MaterialType::Phong;
    if (lit) {
      c.p.numDirLights = 1;
      c.p.numHemiLights = 1;
    }
    if (set) set(c.p);
    if (game) c.patch = gamePatch(game);
    cases.push_back(c);
  };
  add("lambert", MaterialType::Lambert);
  add("lambert fog shadow tone", MaterialType::Lambert, [](ShaderParams& p) {
    p.fog = p.shadowMap = p.toneMapping = true;
    p.numDirLightShadows = 1;
  });
  add("lambert map colours instanced", MaterialType::Lambert, [](ShaderParams& p) { p.map = p.vertexColors = p.instancing = p.instancingColor = p.fog = true; });
  add("lambert double flat alphaTest transparent", MaterialType::Lambert, [](ShaderParams& p) {
    p.doubleSided = p.flatShading = p.alphaTest = p.map = true;
    p.opaque = false;
  });
  add("lambert spot+point lights", MaterialType::Lambert, [](ShaderParams& p) {
    p.numSpotLights = 2;
    p.numPointLights = 1;
    p.fog = true;
  });
  add("lambert, no normals, linear output", MaterialType::Lambert, [](ShaderParams& p) {
    p.vertexNormals = false;
    p.srgbOutput = false;
  });
  add("basic", MaterialType::Basic, [](ShaderParams& p) { p.fog = p.toneMapping = true; });
  add("basic map transparent", MaterialType::Basic, [](ShaderParams& p) {
    p.map = true;
    p.opaque = false;
  });
  add("phong", MaterialType::Phong, [](ShaderParams& p) {
    p.fog = p.shadowMap = p.toneMapping = true;
    p.numDirLightShadows = 1;
  });
  add("points", MaterialType::Points, [](ShaderParams& p) { p.sizeAttenuation = p.fog = true; });
  add("sprite", MaterialType::Sprite, [](ShaderParams& p) {
    p.map = p.fog = p.sizeAttenuation = true;
    p.opaque = false;
  });
  add("line", MaterialType::Line, [](ShaderParams& p) { p.fog = true; });
  for (const char* g : {"ground", "paint", "terrain", "building", "buildingPlain"})
    add(std::string("game: ") + g, MaterialType::Lambert, [g](ShaderParams& p) {
      p.fog = p.shadowMap = p.toneMapping = true;
      p.numDirLightShadows = 1;
      p.map = !std::strcmp(g, "terrain");
    }, g);

  int failed = 0;
  auto link = [&](const std::string& name, const std::string& vs, const std::string& fsrc) {
    GLuint v = 0, f = 0;
    if (!compile(GL_VERTEX_SHADER, vs, v, name) || !compile(GL_FRAGMENT_SHADER, fsrc, f, name)) {
      failed++;
      return;
    }
    GLuint prog = glCreateProgram();
    glAttachShader(prog, v);
    glAttachShader(prog, f);
    for (const auto& a : attributeLocations()) glBindAttribLocation(prog, static_cast<GLuint>(a.second), a.first.c_str());
    glLinkProgram(prog);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
      char log[2000];
      glGetProgramInfoLog(prog, sizeof log, nullptr, log);
      std::printf("FAILED %s (link):\n%s\n", name.c_str(), log);
      failed++;
      return;
    }
    GLint n = 0;
    glGetProgramiv(prog, GL_ACTIVE_UNIFORMS, &n);
    std::printf("ok     %-44s %5zu + %5zu bytes, %d uniforms\n", name.c_str(), vs.size(), fsrc.size(), n);
    if (verbose) {
      for (GLint i = 0; i < n; i++) {
        char nm[128];
        GLint size = 0;
        GLenum type = 0;
        glGetActiveUniform(prog, static_cast<GLuint>(i), sizeof nm, nullptr, &size, &type, nm);
        std::printf("         %s%s\n", nm, size > 1 ? (" ×" + std::to_string(size)).c_str() : "");
      }
    }
    glDeleteProgram(prog);
  };
  for (Case& c : cases) {
    c.p.patch = c.patch.get();
    std::string vs, fsrc;
    buildShader(c.p, vs, fsrc);
    link(c.name, vs, fsrc);
  }
  for (int i = 0; i < 4; i++) {
    std::string vs, fsrc;
    buildDepthShader(i & 1, i & 2, vs, fsrc);
    link(std::string("shadow map") + (i & 1 ? " instanced" : "") + (i & 2 ? " with a picture" : ""), vs, fsrc);
  }
  std::printf("%d of %zu failed\n", failed, cases.size() + 4);
  return failed ? 1 : 0;
}
