// SPDX-License-Identifier: AGPL-3.0-only
/** canvas_fonts.h: the fonts of the native 2D canvas, read with stb_truetype (third_party/plutovg). */
#include "platform/native/canvas_fonts.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <unordered_map>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "plutovg-stb-truetype.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#ifndef FS_FONT_DIR
#define FS_FONT_DIR ""
#endif

namespace fs::canvasFonts {

struct Face {
  std::vector<unsigned char> data;
  stbtt_fontinfo info{};
  int ascent = 0, descent = 0, lineGap = 0;
  /** pixels per font unit at 1 px */
  float unit = 0;
  int weight = 400;
  bool italic = false;
  std::unordered_map<uint32_t, int> glyphOf;
  std::unordered_map<int, std::vector<stbtt_vertex>> shapes;

  bool load(const std::string& path, int index) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n <= 0) {
      std::fclose(f);
      return false;
    }
    data.resize(static_cast<size_t>(n));
    const size_t got = std::fread(data.data(), 1, data.size(), f);
    std::fclose(f);
    if (got != data.size()) return false;
    const int offset = stbtt_GetFontOffsetForIndex(data.data(), index);
    if (offset < 0 || !stbtt_InitFont(&info, data.data(), offset)) return false;
    stbtt_GetFontVMetrics(&info, &ascent, &descent, &lineGap);
    unit = stbtt_ScaleForMappingEmToPixels(&info, 1);
    return unit > 0;
  }
  int glyph(uint32_t cp) {
    const auto it = glyphOf.find(cp);
    if (it != glyphOf.end()) return it->second;
    const int g = stbtt_FindGlyphIndex(&info, static_cast<int>(cp));
    glyphOf[cp] = g;
    return g;
  }
  const std::vector<stbtt_vertex>& shape(int g) {
    auto it = shapes.find(g);
    if (it != shapes.end()) return it->second;
    stbtt_vertex* v = nullptr;
    const int n = stbtt_GetGlyphShape(&info, g, &v);
    std::vector<stbtt_vertex> out(v, v + std::max(0, n));
    if (v) STBTT_free(v, info.userdata);
    return shapes.emplace(g, std::move(out)).first->second;
  }
};

namespace {

struct FaceFile {
  std::string file;
  int weight = 400;
  bool italic = false;
  std::unique_ptr<Face> face;
  bool tried = false;
};

struct Family {
  std::vector<FaceFile> files;
};

/** the folder of the fonts that come with the program */
std::string fontDir() {
  static const std::string dir = [] {
    std::vector<std::string> places;
    if (const char* e = std::getenv("FAHRSIM_FONTS")) places.push_back(e);
    if (*FS_FONT_DIR) places.push_back(FS_FONT_DIR);
    std::error_code ec;
    const std::filesystem::path exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec) {
      places.push_back((exe.parent_path() / "fonts").string());
      places.push_back((exe.parent_path() / "../third_party/fonts").string());
    }
    places.push_back("third_party/fonts");
    for (const std::string& p : places)
      if (std::filesystem::exists(std::filesystem::path(p) / "NotoSans-400.ttf", ec)) return p;
    std::fprintf(stderr, "fahrsim: the canvas finds no fonts (third_party/fonts; FAHRSIM_FONTS names another folder)\n");
    return std::string();
  }();
  return dir;
}

std::map<std::string, Family>& families() {
  static auto* f = new std::map<std::string, Family>([] {
    std::map<std::string, Family> m;
    for (int w : {400, 600, 700, 800}) m["noto sans"].files.push_back({"NotoSans-" + std::to_string(w) + ".ttf", w, false, nullptr, false});
    for (int w : {400, 800}) m["noto sans"].files.push_back({"NotoSans-" + std::to_string(w) + "Italic.ttf", w, true, nullptr, false});
    for (int w : {400, 600, 700, 800}) m["roboto condensed"].files.push_back({"RobotoCondensed-" + std::to_string(w) + ".ttf", w, false, nullptr, false});
    return m;
  }());
  return *f;
}

/** which of our families stands for a family a CSS font names ('': none) */
std::string familyFor(const std::string& name) {
  static const std::map<std::string, std::string> alias = {
      // the narrow DIN-like faces of the signs, the instruments and the number plates
      {"roboto condensed", "roboto condensed"},
      {"din alternate", "roboto condensed"},
      {"din 1451 std", "roboto condensed"},
      {"din", "roboto condensed"},
      {"bahnschrift", "roboto condensed"},
      {"arial narrow", "roboto condensed"},
      {"liberation sans narrow", "roboto condensed"},
      // the rest
      {"noto sans", "noto sans"},
      {"system-ui", "noto sans"},
      {"ui-sans-serif", "noto sans"},
      {"-apple-system", "noto sans"},
      {"blinkmacsystemfont", "noto sans"},
      {"segoe ui", "noto sans"},
      {"roboto", "noto sans"},
      {"sans-serif", "noto sans"},
      {"arial", "noto sans"},
      {"helvetica", "noto sans"},
      {"helvetica neue", "noto sans"},
      {"liberation sans", "noto sans"},
      {"dejavu sans", "noto sans"},
      {"verdana", "noto sans"},
      {"tahoma", "noto sans"},
      {"serif", "noto sans"},
      {"monospace", "noto sans"},
      {"ui-monospace", "noto sans"},
  };
  const auto it = alias.find(name);
  return it != alias.end() ? it->second : std::string();
}

Face* loadFile(FaceFile& f) {
  if (!f.tried) {
    f.tried = true;
    const std::string dir = fontDir();
    if (!dir.empty()) {
      auto face = std::make_unique<Face>();
      face->weight = f.weight;
      face->italic = f.italic;
      if (face->load(dir + "/" + f.file, 0)) f.face = std::move(face);
      else std::fprintf(stderr, "fahrsim: the font %s/%s cannot be read\n", dir.c_str(), f.file.c_str());
    }
  }
  return f.face.get();
}

/** the face of a family nearest to a weight and style (CSS's font matching) */
Face* faceOf(Family& fam, int weight, bool italic) {
  FaceFile* best = nullptr;
  int bestScore = 1 << 30;
  for (FaceFile& f : fam.files) {
    // (the style first, then the weight as CSS orders it: for 400–500 the weights up to 500, then lighter, then heavier;
    // lighter ones first below 400, heavier ones first above 500)
    int score = f.italic == italic ? 0 : 100000;
    const int d = f.weight - weight;
    if (d == 0) score += 0;
    else if (weight >= 400 && weight <= 500) score += (d > 0 && f.weight <= 500) ? d : d < 0 ? 1000 - d : 5000 + d;
    else if (weight < 400) score += d < 0 ? -d : 5000 + d;
    else score += d > 0 ? d : 5000 - d;
    if (score < bestScore && loadFile(f)) {
      bestScore = score;
      best = &f;
    }
  }
  return best ? best->face.get() : nullptr;
}

// --- the machine's fonts, for characters ours lack -------------------------------------

std::map<std::string, std::string>& systemFiles() {
  static auto* m = new std::map<std::string, std::string>([] {
    std::map<std::string, std::string> files;
    std::vector<std::string> dirs = {"/usr/share/fonts", "/usr/local/share/fonts"};
    if (const char* home = std::getenv("HOME")) {
      dirs.push_back(std::string(home) + "/.local/share/fonts");
      dirs.push_back(std::string(home) + "/.fonts");
    }
    for (const std::string& d : dirs) {
      std::error_code ec;
      for (auto it = std::filesystem::recursive_directory_iterator(d, ec); !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        std::string name = it->path().filename().string();
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        files.emplace(name, it->path().string());
      }
    }
    return files;
  }());
  return *m;
}

std::map<std::string, std::unique_ptr<Face>>& systemFaces() {
  static auto* m = new std::map<std::string, std::unique_ptr<Face>>();
  return *m;
}

Face* systemFace(const std::string& name) {
  auto& faces = systemFaces();
  const auto have = faces.find(name);
  if (have != faces.end()) return have->second.get();
  std::unique_ptr<Face> face;
  const auto file = systemFiles().find(name);
  if (file != systemFiles().end()) {
    face = std::make_unique<Face>();
    if (!face->load(file->second, 0)) face.reset();
  }
  Face* out = face.get();
  faces[name] = std::move(face);
  return out;
}

/** the files to look in for a character, by its script */
std::vector<std::string> candidates(uint32_t cp, bool bold) {
  const auto pair = [bold](const std::string& base, const char* ext = ".ttf") {
    return bold ? std::vector<std::string>{base + "-bold" + ext, base + "-regular" + ext} : std::vector<std::string>{base + "-regular" + ext, base + "-bold" + ext};
  };
  std::vector<std::string> c;
  const auto add = [&c](const std::vector<std::string>& v) { c.insert(c.end(), v.begin(), v.end()); };
  if ((cp >= 0x0600 && cp <= 0x08ff) || (cp >= 0xfb50 && cp <= 0xfdff) || (cp >= 0xfe70 && cp <= 0xfeff)) {
    add(pair("notosansarabic"));
    add(pair("notonaskharabic"));
  } else if (cp >= 0x0590 && cp <= 0x05ff) {
    add(pair("notosanshebrew"));
  } else if (cp >= 0x0900 && cp <= 0x097f) {
    add(pair("notosansdevanagari"));
  } else if (cp >= 0x0980 && cp <= 0x09ff) {
    add(pair("notosansbengali"));
  } else if (cp >= 0x0e00 && cp <= 0x0e7f) {
    add(pair("notosansthai"));
  } else if ((cp >= 0x1100 && cp <= 0x11ff) || (cp >= 0x2e80 && cp <= 0x9fff) || (cp >= 0xac00 && cp <= 0xd7af) || (cp >= 0xf900 && cp <= 0xfaff) || (cp >= 0xff00 && cp <= 0xffef) ||
             cp >= 0x20000) {
    add(pair("notosanscjk", ".ttc"));
    add({"notosanscjksc-regular.otf", "wqy-microhei.ttc", "wqy-zenhei.ttc", "droidsansfallbackfull.ttf"});
  }
  add({"notosanssymbols2-regular.ttf", "notosanssymbols-regular.ttf", "notosansmath-regular.ttf"});
  add(bold ? std::vector<std::string>{"dejavusans-bold.ttf", "dejavusans.ttf"} : std::vector<std::string>{"dejavusans.ttf"});
  add({"notocoloremoji.ttf"});
  return c;
}

Face* fallbackFor(uint32_t cp, bool bold) {
  static auto* known = new std::unordered_map<uint64_t, Face*>();
  const uint64_t key = (static_cast<uint64_t>(cp) << 1) | (bold ? 1 : 0);
  const auto it = known->find(key);
  if (it != known->end()) return it->second;
  Face* found = nullptr;
  for (const std::string& name : candidates(cp, bold)) {
    Face* f = systemFace(name);
    if (f && f->glyph(cp)) {
      found = f;
      break;
    }
  }
  (*known)[key] = found;
  return found;
}

/** the next character of a UTF-8 text (U+FFFD for a broken one) */
uint32_t nextChar(const std::string& s, size_t& i) {
  const auto b = [&s](size_t k) { return static_cast<unsigned char>(s[k]); };
  const unsigned char c = b(i++);
  if (c < 0x80) return c;
  int more = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc0 ? 1 : -1;
  if (more < 0) return 0xfffd;
  uint32_t cp = c & (0x3f >> more);
  for (; more > 0; more--) {
    if (i >= s.size() || (b(i) & 0xc0) != 0x80) return 0xfffd;
    cp = (cp << 6) | (b(i++) & 0x3f);
  }
  return cp;
}

std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

std::string trimmed(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) a++;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) b--;
  return s.substr(a, b - a);
}

}  // namespace

bool parseFont(const std::string& css, FontSpec& out) {
  // [style] [variant] [weight] [stretch] size[/line-height] family[, family …]
  FontSpec f;
  size_t i = 0;
  const std::string s = css;
  bool sized = false;
  while (i < s.size()) {
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) i++;
    size_t j = i;
    while (j < s.size() && !std::isspace(static_cast<unsigned char>(s[j]))) j++;
    if (j == i) break;
    const std::string word = lower(s.substr(i, j - i));
    if (word == "italic" || word == "oblique") f.italic = true;
    else if (word == "normal" || word == "small-caps" || word == "condensed" || word == "semi-condensed" || word == "expanded") {
    } else if (word == "bold") f.weight = 700;
    else if (word == "bolder") f.weight = 700;
    else if (word == "lighter") f.weight = 300;
    else if (std::isdigit(static_cast<unsigned char>(word[0])) || word[0] == '.') {
      char* end = nullptr;
      const double v = std::strtod(word.c_str(), &end);
      const std::string unit = end ? std::string(end) : std::string();
      if (unit.empty() && v >= 1 && v <= 1000 && word.find('.') == std::string::npos) f.weight = static_cast<int>(v);
      else {
        const std::string u = unit.substr(0, unit.find('/'));
        if (u == "px") f.px = v;
        else if (u == "pt") f.px = v * 4.0 / 3.0;
        else if (u == "em" || u == "rem") f.px = v * 16;
        else if (u == "%") f.px = v * 0.16;
        else return false;
        sized = true;
        i = j;
        break;
      }
    } else return false;
    i = j;
  }
  if (!sized || !(f.px > 0)) return false;
  // (a line height given apart: "13px / 1.2 …")
  while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) i++;
  if (i < s.size() && s[i] == '/') {
    i++;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) i++;
    while (i < s.size() && !std::isspace(static_cast<unsigned char>(s[i]))) i++;
  }
  f.families.clear();
  std::string rest = s.substr(std::min(i, s.size()));
  size_t at = 0;
  while (at <= rest.size()) {
    const size_t comma = rest.find(',', at);
    std::string name = trimmed(rest.substr(at, comma == std::string::npos ? std::string::npos : comma - at));
    if (name.size() >= 2 && (name.front() == '"' || name.front() == '\'') && name.back() == name.front()) name = name.substr(1, name.size() - 2);
    if (!name.empty()) f.families.push_back(lower(name));
    if (comma == std::string::npos) break;
    at = comma + 1;
  }
  if (f.families.empty()) return false;
  out = f;
  return true;
}

bool layout(const FontSpec& font, const std::string& text, double letterSpacing, Layout& out) {
  out = Layout();
  out.px = font.px;
  // the first family we have, else Noto Sans
  std::string famName;
  for (const std::string& n : font.families) {
    famName = familyFor(n);
    if (!famName.empty()) break;
  }
  if (famName.empty()) famName = "noto sans";
  Face* primary = faceOf(families()[famName], font.weight, font.italic);
  if (!primary && famName != "noto sans") primary = faceOf(families()["noto sans"], font.weight, font.italic);
  if (!primary) return false;
  out.slant = font.italic && !primary->italic;
  const double px = font.px;
  out.ascent = primary->ascent * primary->unit * px;
  out.descent = -primary->descent * primary->unit * px;
  const bool bold = font.weight >= 600;
  double x = 0;
  Face* prevFace = nullptr;
  int prevGlyph = 0;
  bool inked = false;
  double l = 0, r = 0, up = 0, down = 0;
  for (size_t i = 0; i < text.size();) {
    uint32_t cp = nextChar(text, i);
    // (the canvas writes white space as a blank, and nothing for the other controls)
    if (cp == '\t' || cp == '\n' || cp == '\r' || cp == '\f') cp = ' ';
    else if (cp < 0x20 || cp == 0x7f || cp == 0x200b || cp == 0xfeff) continue;
    Face* face = primary;
    int g = primary->glyph(cp);
    if (!g) {
      if (Face* other = fallbackFor(cp, bold)) {
        face = other;
        g = other->glyph(cp);
      }
    }
    const double s = face->unit * px;
    if (face == prevFace && prevGlyph && g) x += stbtt_GetGlyphKernAdvance(&face->info, prevGlyph, g) * s;
    int advance = 0, lsb = 0;
    stbtt_GetGlyphHMetrics(&face->info, g, &advance, &lsb);
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    if (stbtt_GetGlyphBox(&face->info, g, &x0, &y0, &x1, &y1) && x1 > x0 && y1 > y0) {
      const double gl = x + x0 * s, gr = x + x1 * s, gu = y1 * s, gd = -y0 * s;
      if (!inked) {
        l = gl, r = gr, up = gu, down = gd;
        inked = true;
      } else {
        l = std::min(l, gl), r = std::max(r, gr), up = std::max(up, gu), down = std::max(down, gd);
      }
    }
    out.glyphs.push_back({face, g, x});
    x += advance * s + letterSpacing;
    prevFace = face;
    prevGlyph = g;
  }
  out.width = x;
  if (inked) {
    out.inkLeft = -l;
    out.inkRight = r;
    out.inkUp = up;
    out.inkDown = down;
  }
  return true;
}

void outline(const Glyph& g, double px, OutlineSink& sink) {
  if (!g.face) return;
  const std::vector<stbtt_vertex>& v = g.face->shape(g.index);
  const double s = g.face->unit * px;
  bool open = false;
  for (const stbtt_vertex& p : v) {
    const double x = p.x * s, y = -p.y * s;
    switch (p.type) {
      case STBTT_vmove:
        if (open) sink.close();
        sink.moveTo(x, y);
        open = true;
        break;
      case STBTT_vline:
        sink.lineTo(x, y);
        break;
      case STBTT_vcurve:
        sink.quadTo(p.cx * s, -p.cy * s, x, y);
        break;
      case STBTT_vcubic:
        sink.cubicTo(p.cx * s, -p.cy * s, p.cx1 * s, -p.cy1 * s, x, y);
        break;
      default:
        break;
    }
  }
  if (open) sink.close();
}

}  // namespace fs::canvasFonts
