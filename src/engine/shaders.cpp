// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Shaders put together as three.js does (see shaders.h).
 * Origin: three.js r187, src/renderers/webgl/WebGLProgram.js (MIT licence) – the
 * parts that concern the materials and the lights this game uses.
 */
#include "engine/shaders.h"

#include <cstdio>
#include <cstring>
#include <unordered_map>

#include "engine/three_glsl.h"

namespace fs {

namespace {

const char* chunkSource(const std::string& name) {
  static std::unordered_map<std::string, const char*> byName;
  if (byName.empty())
    for (int i = 0; i < three::CHUNK_COUNT; i++) byName.emplace(three::CHUNKS[i].name, three::CHUNKS[i].source);
  auto it = byName.find(name);
  return it == byName.end() ? nullptr : it->second;
}

const three::Lib* libOf(MaterialType type) {
  const char* name = type == MaterialType::Basic || type == MaterialType::Line ? "meshbasic"
                     : type == MaterialType::Lambert                             ? "meshlambert"
                     : type == MaterialType::Phong                               ? "meshphong"
                     : type == MaterialType::Sprite                              ? "sprite"
                     : type == MaterialType::Points                              ? "points"
                     : type == MaterialType::Depth                               ? "depth"
                                                                                 : nullptr;
  if (!name) return nullptr;
  for (int i = 0; i < three::LIB_COUNT; i++)
    if (!std::strcmp(three::LIBS[i].name, name)) return &three::LIBS[i];
  return nullptr;
}

const char* shaderTypeName(MaterialType type) {
  switch (type) {
    case MaterialType::Basic: return "MeshBasicMaterial";
    case MaterialType::Lambert: return "MeshLambertMaterial";
    case MaterialType::Phong: return "MeshPhongMaterial";
    case MaterialType::Shader: return "ShaderMaterial";
    case MaterialType::Sprite: return "SpriteMaterial";
    case MaterialType::Points: return "PointsMaterial";
    case MaterialType::Line: return "LineBasicMaterial";
    case MaterialType::Depth: return "MeshDepthMaterial";
  }
  return "Material";
}

std::string replaceAllText(std::string s, const std::string& from, const std::string& to) {
  size_t at = 0;
  while ((at = s.find(from, at)) != std::string::npos) {
    s.replace(at, from.size(), to);
    at += to.size();
  }
  return s;
}

/** three.js replaceLightNums: the numbers of lights written into the loops and the #ifs */
std::string replaceLightNums(std::string s, const ShaderParams& p) {
  auto n = [](int v) { return std::to_string(v); };
  s = replaceAllText(std::move(s), "NUM_SUN_LIGHTS", "0");
  s = replaceAllText(std::move(s), "NUM_DIR_LIGHTS", n(p.numDirLights));
  s = replaceAllText(std::move(s), "NUM_SPOT_LIGHTS", n(p.numSpotLights));
  s = replaceAllText(std::move(s), "NUM_SPOT_LIGHT_MAPS", "0");
  s = replaceAllText(std::move(s), "NUM_SPOT_LIGHT_COORDS", "0");
  s = replaceAllText(std::move(s), "NUM_RECT_AREA_LIGHTS", "0");
  s = replaceAllText(std::move(s), "NUM_POINT_LIGHTS", n(p.numPointLights));
  s = replaceAllText(std::move(s), "NUM_HEMI_LIGHTS", n(p.numHemiLights));
  s = replaceAllText(std::move(s), "NUM_SUN_LIGHT_SHADOWS", "0");
  s = replaceAllText(std::move(s), "NUM_DIR_LIGHT_SHADOWS", n(p.numDirLightShadows));
  s = replaceAllText(std::move(s), "NUM_SPOT_LIGHT_SHADOWS_WITH_MAPS", "0");
  s = replaceAllText(std::move(s), "NUM_SPOT_LIGHT_SHADOWS", "0");
  s = replaceAllText(std::move(s), "NUM_POINT_LIGHT_SHADOWS", "0");
  s = replaceAllText(std::move(s), "NUM_LIGHT_PROBE_GRIDS", "0");
  // (no clipping planes: three.js replaceClippingPlaneNums)
  s = replaceAllText(std::move(s), "NUM_CLIPPING_PLANES", "0");
  s = replaceAllText(std::move(s), "UNION_CLIPPING_PLANES", "0");
  return s;
}

/** the edits of a material's patch on one of the two shaders, before the includes are resolved */
std::string applyPatch(std::string source, ShaderPatch::Stage stage, const ShaderPatch* patch) {
  if (!patch) return source;
  for (const ShaderPatch::Edit& e : patch->edits) {
    if (e.stage != stage) continue;
    std::string needle = e.text ? e.chunk : "#include <" + e.chunk + ">";
    size_t at = source.find(needle);
    if (at == std::string::npos) {
      static std::unordered_map<std::string, bool> told;
      std::string id = patch->key + "|" + needle;
      if (!told[id]) {
        told[id] = true;
        std::fprintf(stderr, "fahrsim: a shader patch (%s) found no \"%s\" in the %s shader\n", patch->key.c_str(), needle.c_str(),
                     stage == ShaderPatch::Vertex ? "vertex" : "fragment");
      }
      continue;
    }
    std::string by = e.mode == ShaderPatch::Replace ? e.code : e.mode == ShaderPatch::Before ? e.code + "\n" + needle : needle + "\n" + e.code;
    source.replace(at, needle.size(), by);
  }
  return source;
}

const char* const PRECISION =
    "precision highp float;\n"
    "precision highp int;\n"
    "precision highp sampler2D;\n"
    "precision highp samplerCube;\n"
    "precision highp sampler3D;\n"
    "precision highp sampler2DArray;\n"
    "precision highp sampler2DShadow;\n"
    "precision highp samplerCubeShadow;\n"
    "precision highp sampler2DArrayShadow;\n"
    "precision highp isampler2D;\n"
    "precision highp isampler3D;\n"
    "precision highp isamplerCube;\n"
    "precision highp isampler2DArray;\n"
    "precision highp usampler2D;\n"
    "precision highp usampler3D;\n"
    "precision highp usamplerCube;\n"
    "precision highp usampler2DArray;\n"
    "#define HIGH_PRECISION\n";

void line(std::string& out, bool on, const char* text) {
  if (on) {
    out += text;
    out += '\n';
  }
}

std::string customDefines(const ShaderParams& p) {
  std::string out;
  if (p.custom)
    for (const auto& d : p.custom->defines) out += "#define " + d.first + " " + d.second + "\n";
  return out;
}

}  // namespace

namespace shaderText {

std::string resolveIncludes(const std::string& source) {
  // three.js: /^[ \t]*#include +<([\w\d./]+)>/gm
  std::string out;
  out.reserve(source.size() * 2);
  size_t at = 0;
  while (at <= source.size()) {
    size_t end = source.find('\n', at);
    if (end == std::string::npos) end = source.size();
    size_t i = at;
    while (i < end && (source[i] == ' ' || source[i] == '\t')) i++;
    bool done = false;
    if (source.compare(i, 8, "#include") == 0) {
      size_t j = i + 8;
      size_t sp = j;
      while (j < end && source[j] == ' ') j++;
      if (j > sp && j < end && source[j] == '<') {
        size_t close = source.find('>', j);
        if (close != std::string::npos && close < end) {
          std::string name = source.substr(j + 1, close - j - 1);
          const char* src = chunkSource(name);
          if (src) {
            out += resolveIncludes(src);
            out.append(source, close + 1, end - close - 1);
            done = true;
          } else std::fprintf(stderr, "fahrsim: no shader chunk <%s>\n", name.c_str());
        }
      }
    }
    if (!done) out.append(source, at, end - at);
    if (end < source.size()) out += '\n';
    at = end + 1;
  }
  return out;
}

std::string unrollLoops(const std::string& source) {
  static const std::string START = "#pragma unroll_loop_start";
  static const std::string END = "#pragma unroll_loop_end";
  std::string out;
  size_t at = 0;
  for (;;) {
    size_t s = source.find(START, at);
    if (s == std::string::npos) break;
    // for ( int i = A; i < B; i ++ ) {
    size_t i = s + START.size();
    auto ws = [&]() {
      while (i < source.size() && std::isspace(static_cast<unsigned char>(source[i]))) i++;
    };
    auto word = [&](const char* w) {
      ws();
      size_t n = std::strlen(w);
      if (source.compare(i, n, w) != 0) return false;
      i += n;
      return true;
    };
    auto number = [&](int& v) {
      ws();
      size_t b = i;
      v = 0;
      while (i < source.size() && source[i] >= '0' && source[i] <= '9') v = v * 10 + (source[i++] - '0');
      return i > b;
    };
    int from = 0, to = 0;
    bool ok = word("for") && word("(") && word("int") && word("i") && word("=") && number(from) && word(";") && word("i") && word("<") && number(to) &&
              word(";") && word("i") && word("++") && word(")") && word("{");
    size_t e = ok ? source.find(END, i) : std::string::npos;
    size_t close = e == std::string::npos ? std::string::npos : source.rfind('}', e);
    if (!ok || e == std::string::npos || close == std::string::npos || close < i) {
      // (not the shape three.js unrolls: left as it is)
      out.append(source, at, s + START.size() - at);
      at = s + START.size();
      continue;
    }
    out.append(source, at, s - at);
    std::string body = source.substr(i, close - i);
    for (int k = from; k < to; k++) {
      std::string idx = std::to_string(k);
      // [ i ] → [ k ]
      std::string b;
      for (size_t c = 0; c < body.size(); c++) {
        if (body[c] == '[') {
          size_t d = c + 1;
          while (d < body.size() && std::isspace(static_cast<unsigned char>(body[d]))) d++;
          if (d < body.size() && body[d] == 'i') {
            size_t f = d + 1;
            while (f < body.size() && std::isspace(static_cast<unsigned char>(body[f]))) f++;
            if (f < body.size() && body[f] == ']') {
              b += "[ " + idx + " ]";
              c = f;
              continue;
            }
          }
        }
        b += body[c];
      }
      out += replaceAllText(std::move(b), "UNROLLED_LOOP_INDEX", idx);
    }
    at = e + END.size();
  }
  out.append(source, at, std::string::npos);
  return out;
}

std::string replaceWord(const std::string& source, const std::string& word, const std::string& by) {
  auto isWord = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
  std::string out;
  size_t at = 0;
  for (;;) {
    size_t i = source.find(word, at);
    if (i == std::string::npos) break;
    bool whole = (i == 0 || !isWord(source[i - 1])) && (i + word.size() >= source.size() || !isWord(source[i + word.size()]));
    out.append(source, at, i - at);
    out += whole ? by : word;
    at = i + word.size();
  }
  out.append(source, at, std::string::npos);
  return out;
}

}  // namespace shaderText

std::string shaderKey(const ShaderParams& p) {
  char buf[96];
  unsigned bits = (p.map ? 1u : 0) | (p.vertexColors ? 2u : 0) | (p.instancing ? 4u : 0) | (p.instancingColor ? 8u : 0) | (p.fog ? 16u : 0) |
                  (p.shadowMap ? 32u : 0) | (p.doubleSided ? 64u : 0) | (p.flipSided ? 128u : 0) | (p.flatShading ? 256u : 0) | (p.alphaTest ? 512u : 0) |
                  (p.opaque ? 1024u : 0) | (p.toneMapping ? 2048u : 0) | (p.srgbOutput ? 4096u : 0) | (p.vertexNormals ? 8192u : 0) |
                  (p.sizeAttenuation ? 16384u : 0) | (p.alphaMap ? 32768u : 0) | (p.emissiveMap ? 65536u : 0);
  std::snprintf(buf, sizeof buf, "%d:%x:%d.%d.%d.%d.%d", static_cast<int>(p.type), bits, p.numDirLights, p.numHemiLights, p.numSpotLights, p.numPointLights,
                p.numDirLightShadows);
  std::string key = buf;
  if (p.patch) key += "|p:" + p.patch->key;
  if (p.custom) {
    // (a ShaderMaterial: its own text is the key – two with the same text share a program)
    key += "|c:" + std::to_string(std::hash<std::string>()(p.custom->vertexShader)) + ":" + std::to_string(std::hash<std::string>()(p.custom->fragmentShader));
    for (const auto& d : p.custom->defines) key += ";" + d.first + "=" + d.second;
  }
  return key;
}

void buildShader(const ShaderParams& p, std::string& vertex, std::string& fragment) {
  const three::Lib* lib = libOf(p.type);
  std::string vs = p.custom ? p.custom->vertexShader : (p.patch && !p.patch->vertexSource.empty()) ? p.patch->vertexSource : lib ? lib->vertex : "";
  std::string fs = p.custom ? p.custom->fragmentShader : (p.patch && !p.patch->fragmentSource.empty()) ? p.patch->fragmentSource : lib ? lib->fragment : "";
  vs = applyPatch(std::move(vs), ShaderPatch::Vertex, p.patch);
  fs = applyPatch(std::move(fs), ShaderPatch::Fragment, p.patch);

  const bool lit = p.type == MaterialType::Lambert || p.type == MaterialType::Phong || (p.custom != nullptr);
  const bool shadow = p.shadowMap && p.numDirLightShadows > 0 && lit;
  const std::string defines = customDefines(p);

  // --- the vertex shader's head (WebGLProgram.js prefixVertex) ---
  std::string pv = "#version 300 es\n#define attribute in\n#define varying out\n#define texture2D texture\n";
  pv += PRECISION;
  pv += std::string("#define SHADER_TYPE ") + shaderTypeName(p.type) + "\n#define SHADER_NAME \n";
  pv += defines;
  line(pv, p.instancing, "#define USE_INSTANCING");
  line(pv, p.instancingColor, "#define USE_INSTANCING_COLOR");
  line(pv, p.fog, "#define USE_FOG");
  line(pv, p.map, "#define USE_MAP");
  line(pv, p.map, "#define MAP_UV uv");
  line(pv, p.alphaMap, "#define USE_ALPHAMAP");
  line(pv, p.alphaMap, "#define ALPHAMAP_UV uv");
  line(pv, p.emissiveMap, "#define USE_EMISSIVEMAP");
  line(pv, p.emissiveMap, "#define EMISSIVEMAP_UV uv");
  line(pv, p.vertexNormals, "#define HAS_NORMAL");
  line(pv, p.vertexColors, "#define USE_COLOR");
  line(pv, p.flatShading, "#define FLAT_SHADED");
  line(pv, p.doubleSided, "#define DOUBLE_SIDED");
  line(pv, p.flipSided, "#define FLIP_SIDED");
  line(pv, shadow, "#define USE_SHADOWMAP");
  line(pv, shadow, "#define SHADOWMAP_TYPE_PCF");
  line(pv, p.sizeAttenuation, "#define USE_SIZEATTENUATION");
  pv += "uniform mat4 modelMatrix;\n"
        "uniform mat4 modelViewMatrix;\n"
        "uniform mat4 projectionMatrix;\n"
        "uniform mat4 viewMatrix;\n"
        "uniform mat3 normalMatrix;\n"
        "uniform vec3 cameraPosition;\n"
        "uniform bool isOrthographic;\n"
        "#ifdef USE_INSTANCING\n"
        "\tattribute mat4 instanceMatrix;\n"
        "#endif\n"
        "#ifdef USE_INSTANCING_COLOR\n"
        "\tattribute vec3 instanceColor;\n"
        "#endif\n"
        "attribute vec3 position;\n"
        "attribute vec3 normal;\n"
        "attribute vec2 uv;\n"
        "#if defined( USE_COLOR_ALPHA )\n"
        "\tattribute vec4 color;\n"
        "#elif defined( USE_COLOR )\n"
        "\tattribute vec3 color;\n"
        "#endif\n"
        "\n";

  // --- the fragment shader's head (WebGLProgram.js prefixFragment) ---
  std::string pf = "#version 300 es\n"
                   "#define varying in\n"
                   "layout(location = 0) out highp vec4 pc_fragColor;\n"
                   "#define gl_FragColor pc_fragColor\n"
                   "#define gl_FragDepthEXT gl_FragDepth\n"
                   "#define texture2D texture\n"
                   "#define textureCube texture\n"
                   "#define texture2DProj textureProj\n"
                   "#define texture2DLodEXT textureLod\n"
                   "#define texture2DProjLodEXT textureProjLod\n"
                   "#define textureCubeLodEXT textureLod\n"
                   "#define texture2DGradEXT textureGrad\n"
                   "#define texture2DProjGradEXT textureProjGrad\n"
                   "#define textureCubeGradEXT textureGrad\n";
  pf += PRECISION;
  pf += std::string("#define SHADER_TYPE ") + shaderTypeName(p.type) + "\n#define SHADER_NAME \n";
  pf += defines;
  line(pf, p.fog, "#define USE_FOG");
  line(pf, p.map, "#define USE_MAP");
  line(pf, p.alphaMap, "#define USE_ALPHAMAP");
  line(pf, p.emissiveMap, "#define USE_EMISSIVEMAP");
  line(pf, p.alphaTest, "#define USE_ALPHATEST");
  line(pf, p.vertexColors || p.instancingColor, "#define USE_COLOR");
  line(pf, p.flatShading, "#define FLAT_SHADED");
  line(pf, p.doubleSided, "#define DOUBLE_SIDED");
  line(pf, p.flipSided, "#define FLIP_SIDED");
  line(pf, shadow, "#define USE_SHADOWMAP");
  line(pf, shadow, "#define SHADOWMAP_TYPE_PCF");
  pf += "uniform mat4 viewMatrix;\n"
        "uniform vec3 cameraPosition;\n"
        "uniform bool isOrthographic;\n";
  if (p.toneMapping) {
    pf += "#define TONE_MAPPING\n";
    pf += chunkSource("tonemapping_pars_fragment");
    pf += "\nvec3 toneMapping( vec3 color ) { return ACESFilmicToneMapping( color ); }\n";
  }
  line(pf, p.opaque, "#define OPAQUE");
  pf += chunkSource("colorspace_pars_fragment");
  pf += std::string("\nvec4 linearToOutputTexel( vec4 value ) {\n\treturn ") + (p.srgbOutput ? "sRGBTransferOETF" : "LinearTransferOETF") +
        "( vec4( value.rgb * mat3( 1.0000,0.0000,0.0000,0.0000,1.0000,0.0000,0.0000,0.0000,1.0000 ), value.a ) );\n}\n";
  pf += "float luminance( const in vec3 rgb ) {\n\tconst vec3 weights = vec3( 0.2126, 0.7152, 0.0722 );\n\treturn dot( weights, rgb );\n}\n\n";

  vs = shaderText::unrollLoops(replaceLightNums(shaderText::resolveIncludes(vs), p));
  fs = shaderText::unrollLoops(replaceLightNums(shaderText::resolveIncludes(fs), p));
  vertex = pv + vs;
  fragment = pf + fs;
}

void buildDepthShader(bool instancing, bool alphaTestedMap, std::string& vertex, std::string& fragment) {
  vertex = "#version 300 es\nprecision highp float;\n";
  if (instancing) vertex += "#define USE_INSTANCING\n";
  if (alphaTestedMap) vertex += "#define USE_MAP\n";
  vertex += "in vec3 position;\n"
            "#ifdef USE_INSTANCING\nin mat4 instanceMatrix;\n#endif\n"
            "#ifdef USE_MAP\nin vec2 uv;\nout vec2 vUv;\n#endif\n"
            "uniform mat4 modelMatrix;\n"
            "uniform mat4 lightMatrix;\n"
            "void main() {\n"
            "  vec4 p = vec4( position, 1.0 );\n"
            "#ifdef USE_INSTANCING\n  p = instanceMatrix * p;\n#endif\n"
            "#ifdef USE_MAP\n  vUv = uv;\n#endif\n"
            "  gl_Position = lightMatrix * ( modelMatrix * p );\n"
            "}\n";
  fragment = "#version 300 es\nprecision highp float;\n";
  if (alphaTestedMap) fragment += "#define USE_MAP\n";
  fragment += "#ifdef USE_MAP\nuniform sampler2D map;\nuniform float alphaTest;\nin vec2 vUv;\n#endif\n"
              "layout(location = 0) out highp vec4 pc_fragColor;\n"
              "void main() {\n"
              "#ifdef USE_MAP\n  if ( texture( map, vUv ).a < alphaTest ) discard;\n#endif\n"
              "  pc_fragColor = vec4( 1.0 );\n"
              "}\n";
}

namespace {
std::vector<std::pair<std::string, int>>& registry() {
  static std::vector<std::pair<std::string, int>> r = {
      {"position", 0}, {"normal", 1}, {"uv", 2}, {"color", 3}, {"instanceMatrix", 4}, {"instanceColor", 8},
  };
  return r;
}
}  // namespace

int attributeLocation(const std::string& name) {
  auto& r = registry();
  int next = 9;
  for (const auto& e : r) {
    if (e.first == name) return e.second;
    if (e.second >= next) next = e.second + 1;
  }
  if (next > 15) return -1;
  r.emplace_back(name, next);
  return next;
}

const std::vector<std::pair<std::string, int>>& attributeLocations() { return registry(); }

}  // namespace fs
