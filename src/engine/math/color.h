// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Color, as three.js keeps colours: three numbers in the working colour space, which
 * is linear sRGB.  A hex number, a CSS text or a colour name is an sRGB colour and is
 * converted when it is set (`Color(0x808890)`, setHex, setStyle) and when it is asked
 * for (getHex, getHexString, getStyle); set(r, g, b) / setRGB / setHSL take and
 * getRGB / getHSL give working-space numbers unless a colour space is named.
 *
 * Origin: three.js r187 src/math/Color.js, src/math/ColorManagement.js (enabled, with
 * the default working colour space)
 */
#pragma once

#include <cstdlib>
#include <string>
#include <type_traits>

#include "engine/math/linear.h"

namespace fs {

/** THREE.NoColorSpace, THREE.SRGBColorSpace, THREE.LinearSRGBColorSpace */
enum class ColorSpace { None, SRGB, LinearSRGB };
inline constexpr ColorSpace NoColorSpace = ColorSpace::None;
inline constexpr ColorSpace SRGBColorSpace = ColorSpace::SRGB;
inline constexpr ColorSpace LinearSRGBColorSpace = ColorSpace::LinearSRGB;

inline double SRGBToLinear(double c) { return (c < 0.04045) ? c * 0.0773993808 : std::pow(c * 0.9478672986 + 0.0521327014, 2.4); }
inline double LinearToSRGB(double c) { return (c < 0.0031308) ? c * 12.92 : 1.055 * (std::pow(c, 0.41666)) - 0.055; }

/** what getHSL answers */
struct HSL {
  double h = 0, s = 0, l = 0;
};

struct Color {
  double r = 1, g = 1, b = 1;

  /** white */
  Color() = default;
  /** `new Color(r, g, b)`: working-space numbers, as setRGB */
  Color(double r_, double g_, double b_) : r(r_), g(g_), b(b_) {}
  /** `new Color(0xRRGGBB)`: an sRGB colour, as setHex */
  template <class T>
    requires std::is_arithmetic_v<T>
  Color(T hex) { setHex(static_cast<double>(hex)); }
  /** `new Color('#rrggbb' | 'rgb(…)' | 'hsl(…)' | 'skyblue')`, as setStyle */
  Color(const std::string& style) { setStyle(style); }
  Color(const char* style) { setStyle(style); }

  Color& set(const Color& value) { return copy(value); }
  template <class T>
    requires std::is_arithmetic_v<T>
  Color& set(T hex) { return setHex(static_cast<double>(hex)); }
  Color& set(const std::string& style) { return setStyle(style); }
  Color& set(const char* style) { return setStyle(style); }
  Color& set(double r_, double g_, double b_) { return setRGB(r_, g_, b_); }
  Color& setScalar(double scalar) { r = scalar; g = scalar; b = scalar; return *this; }
  inline Color& setHex(double hex, ColorSpace colorSpace = SRGBColorSpace);
  inline Color& setRGB(double r_, double g_, double b_, ColorSpace colorSpace = LinearSRGBColorSpace);
  /** hue, saturation, lightness: each 0 … 1 */
  inline Color& setHSL(double h, double s, double l, ColorSpace colorSpace = LinearSRGBColorSpace);
  /** 'rgb(255,0,0)', 'rgb(100%,0%,0%)', 'hsl(120,50%,50%)', '#f00', '#ff0000' or a CSS colour name (anything else: no change) */
  inline Color& setStyle(const std::string& style, ColorSpace colorSpace = SRGBColorSpace);
  /** one of the CSS colour names ('skyblue'; unknown: no change) */
  inline Color& setColorName(const std::string& style, ColorSpace colorSpace = SRGBColorSpace);
  Color clone() const { return *this; }
  Color& copy(const Color& color) { r = color.r; g = color.g; b = color.b; return *this; }
  Color& copySRGBToLinear(const Color& color) {
    r = SRGBToLinear(color.r);
    g = SRGBToLinear(color.g);
    b = SRGBToLinear(color.b);
    return *this;
  }
  Color& copyLinearToSRGB(const Color& color) {
    r = LinearToSRGB(color.r);
    g = LinearToSRGB(color.g);
    b = LinearToSRGB(color.b);
    return *this;
  }
  Color& convertSRGBToLinear() { return copySRGBToLinear(*this); }
  Color& convertLinearToSRGB() { return copyLinearToSRGB(*this); }
  /** 0xRRGGBB */
  inline uint32_t getHex(ColorSpace colorSpace = SRGBColorSpace) const;
  /** 'rrggbb' */
  inline std::string getHexString(ColorSpace colorSpace = SRGBColorSpace) const;
  inline HSL& getHSL(HSL& target, ColorSpace colorSpace = LinearSRGBColorSpace) const;
  inline Color& getRGB(Color& target, ColorSpace colorSpace = LinearSRGBColorSpace) const;
  /** 'rgb(255,0,0)' */
  inline std::string getStyle(ColorSpace colorSpace = SRGBColorSpace) const;
  Color& offsetHSL(double h, double s, double l) {
    HSL hsl;
    getHSL(hsl);
    return setHSL(hsl.h + h, hsl.s + s, hsl.l + l);
  }
  Color& add(const Color& color) { r += color.r; g += color.g; b += color.b; return *this; }
  Color& addColors(const Color& color1, const Color& color2) {
    r = color1.r + color2.r;
    g = color1.g + color2.g;
    b = color1.b + color2.b;
    return *this;
  }
  Color& addScalar(double s) { r += s; g += s; b += s; return *this; }
  /** never below 0 */
  Color& sub(const Color& color) {
    r = jsMax(0, r - color.r);
    g = jsMax(0, g - color.g);
    b = jsMax(0, b - color.b);
    return *this;
  }
  Color& multiply(const Color& color) { r *= color.r; g *= color.g; b *= color.b; return *this; }
  Color& multiplyScalar(double s) { r *= s; g *= s; b *= s; return *this; }
  Color& lerp(const Color& color, double alpha) {
    r += (color.r - r) * alpha;
    g += (color.g - g) * alpha;
    b += (color.b - b) * alpha;
    return *this;
  }
  Color& lerpColors(const Color& color1, const Color& color2, double alpha) {
    r = color1.r + (color2.r - color1.r) * alpha;
    g = color1.g + (color2.g - color1.g) * alpha;
    b = color1.b + (color2.b - color1.b) * alpha;
    return *this;
  }
  Color& lerpHSL(const Color& color, double alpha) {
    HSL hslA, hslB;
    getHSL(hslA);
    color.getHSL(hslB);
    const double h = MathUtils::lerp(hslA.h, hslB.h, alpha);
    const double s = MathUtils::lerp(hslA.s, hslB.s, alpha);
    const double l = MathUtils::lerp(hslA.l, hslB.l, alpha);
    setHSL(h, s, l);
    return *this;
  }
  Color& setFromVector3(const Vector3& v) { r = v.x; g = v.y; b = v.z; return *this; }
  Color& applyMatrix3(const Matrix3& m) {
    const double r_ = r, g_ = g, b_ = b;
    const double* e = m.elements;
    r = e[0] * r_ + e[3] * g_ + e[6] * b_;
    g = e[1] * r_ + e[4] * g_ + e[7] * b_;
    b = e[2] * r_ + e[5] * g_ + e[8] * b_;
    return *this;
  }
  bool equals(const Color& c) const { return c.r == r && c.g == g && c.b == b; }
  template <class A>
  Color& fromArray(const A& array, size_t offset = 0) {
    r = array[offset];
    g = array[offset + 1];
    b = array[offset + 2];
    return *this;
  }
  std::array<double, 3> toArray() const { return {r, g, b}; }
  template <class A>
  A& toArray(A& array, size_t offset = 0) const {
    array[offset] = r;
    array[offset + 1] = g;
    array[offset + 2] = b;
    return array;
  }
  template <class A>
  Color& fromBufferAttribute(const A& attribute, size_t index) {
    r = attribute.getX(index);
    g = attribute.getY(index);
    b = attribute.getZ(index);
    return *this;
  }
  /** the hex number of a CSS colour name in small letters (false: there is no such name) */
  static inline bool nameToHex(const std::string& name, uint32_t& hex);

  bool operator==(const Color& o) const { return equals(o); }
  bool operator!=(const Color& o) const { return !equals(o); }
};

/** three.js's ColorManagement with its defaults: enabled, the working colour space linear sRGB */
namespace ColorManagement {
inline constexpr bool enabled = true;
inline constexpr ColorSpace workingColorSpace = LinearSRGBColorSpace;
inline Color& convert(Color& color, ColorSpace sourceColorSpace, ColorSpace targetColorSpace) {
  if (sourceColorSpace == targetColorSpace || sourceColorSpace == NoColorSpace || targetColorSpace == NoColorSpace) return color;
  if (sourceColorSpace == SRGBColorSpace) {
    color.r = SRGBToLinear(color.r);
    color.g = SRGBToLinear(color.g);
    color.b = SRGBToLinear(color.b);
  }
  // (the two spaces have the same primaries: no matrix between them)
  if (targetColorSpace == SRGBColorSpace) {
    color.r = LinearToSRGB(color.r);
    color.g = LinearToSRGB(color.g);
    color.b = LinearToSRGB(color.b);
  }
  return color;
}
inline Color& workingToColorSpace(Color& color, ColorSpace targetColorSpace) { return convert(color, workingColorSpace, targetColorSpace); }
inline Color& colorSpaceToWorking(Color& color, ColorSpace sourceColorSpace) { return convert(color, sourceColorSpace, workingColorSpace); }
}  // namespace ColorManagement

namespace colordetail {

inline double hue2rgb(double p, double q, double t) {
  if (t < 0) t += 1;
  if (t > 1) t -= 1;
  if (t < 1.0 / 6) return p + (q - p) * 6 * t;
  if (t < 1.0 / 2) return q;
  if (t < 2.0 / 3) return p + (q - p) * 6 * (2.0 / 3 - t);
  return p;
}

struct NamedColor {
  const char* name;
  uint32_t hex;
};
inline constexpr NamedColor colorKeywords[] = {
    {"aliceblue", 0xF0F8FF}, {"antiquewhite", 0xFAEBD7}, {"aqua", 0x00FFFF}, {"aquamarine", 0x7FFFD4}, {"azure", 0xF0FFFF},
    {"beige", 0xF5F5DC}, {"bisque", 0xFFE4C4}, {"black", 0x000000}, {"blanchedalmond", 0xFFEBCD}, {"blue", 0x0000FF}, {"blueviolet", 0x8A2BE2},
    {"brown", 0xA52A2A}, {"burlywood", 0xDEB887}, {"cadetblue", 0x5F9EA0}, {"chartreuse", 0x7FFF00}, {"chocolate", 0xD2691E}, {"coral", 0xFF7F50},
    {"cornflowerblue", 0x6495ED}, {"cornsilk", 0xFFF8DC}, {"crimson", 0xDC143C}, {"cyan", 0x00FFFF}, {"darkblue", 0x00008B}, {"darkcyan", 0x008B8B},
    {"darkgoldenrod", 0xB8860B}, {"darkgray", 0xA9A9A9}, {"darkgreen", 0x006400}, {"darkgrey", 0xA9A9A9}, {"darkkhaki", 0xBDB76B}, {"darkmagenta", 0x8B008B},
    {"darkolivegreen", 0x556B2F}, {"darkorange", 0xFF8C00}, {"darkorchid", 0x9932CC}, {"darkred", 0x8B0000}, {"darksalmon", 0xE9967A}, {"darkseagreen", 0x8FBC8F},
    {"darkslateblue", 0x483D8B}, {"darkslategray", 0x2F4F4F}, {"darkslategrey", 0x2F4F4F}, {"darkturquoise", 0x00CED1}, {"darkviolet", 0x9400D3},
    {"deeppink", 0xFF1493}, {"deepskyblue", 0x00BFFF}, {"dimgray", 0x696969}, {"dimgrey", 0x696969}, {"dodgerblue", 0x1E90FF}, {"firebrick", 0xB22222},
    {"floralwhite", 0xFFFAF0}, {"forestgreen", 0x228B22}, {"fuchsia", 0xFF00FF}, {"gainsboro", 0xDCDCDC}, {"ghostwhite", 0xF8F8FF}, {"gold", 0xFFD700},
    {"goldenrod", 0xDAA520}, {"gray", 0x808080}, {"green", 0x008000}, {"greenyellow", 0xADFF2F}, {"grey", 0x808080}, {"honeydew", 0xF0FFF0}, {"hotpink", 0xFF69B4},
    {"indianred", 0xCD5C5C}, {"indigo", 0x4B0082}, {"ivory", 0xFFFFF0}, {"khaki", 0xF0E68C}, {"lavender", 0xE6E6FA}, {"lavenderblush", 0xFFF0F5}, {"lawngreen", 0x7CFC00},
    {"lemonchiffon", 0xFFFACD}, {"lightblue", 0xADD8E6}, {"lightcoral", 0xF08080}, {"lightcyan", 0xE0FFFF}, {"lightgoldenrodyellow", 0xFAFAD2}, {"lightgray", 0xD3D3D3},
    {"lightgreen", 0x90EE90}, {"lightgrey", 0xD3D3D3}, {"lightpink", 0xFFB6C1}, {"lightsalmon", 0xFFA07A}, {"lightseagreen", 0x20B2AA}, {"lightskyblue", 0x87CEFA},
    {"lightslategray", 0x778899}, {"lightslategrey", 0x778899}, {"lightsteelblue", 0xB0C4DE}, {"lightyellow", 0xFFFFE0}, {"lime", 0x00FF00}, {"limegreen", 0x32CD32},
    {"linen", 0xFAF0E6}, {"magenta", 0xFF00FF}, {"maroon", 0x800000}, {"mediumaquamarine", 0x66CDAA}, {"mediumblue", 0x0000CD}, {"mediumorchid", 0xBA55D3},
    {"mediumpurple", 0x9370DB}, {"mediumseagreen", 0x3CB371}, {"mediumslateblue", 0x7B68EE}, {"mediumspringgreen", 0x00FA9A}, {"mediumturquoise", 0x48D1CC},
    {"mediumvioletred", 0xC71585}, {"midnightblue", 0x191970}, {"mintcream", 0xF5FFFA}, {"mistyrose", 0xFFE4E1}, {"moccasin", 0xFFE4B5}, {"navajowhite", 0xFFDEAD},
    {"navy", 0x000080}, {"oldlace", 0xFDF5E6}, {"olive", 0x808000}, {"olivedrab", 0x6B8E23}, {"orange", 0xFFA500}, {"orangered", 0xFF4500}, {"orchid", 0xDA70D6},
    {"palegoldenrod", 0xEEE8AA}, {"palegreen", 0x98FB98}, {"paleturquoise", 0xAFEEEE}, {"palevioletred", 0xDB7093}, {"papayawhip", 0xFFEFD5}, {"peachpuff", 0xFFDAB9},
    {"peru", 0xCD853F}, {"pink", 0xFFC0CB}, {"plum", 0xDDA0DD}, {"powderblue", 0xB0E0E6}, {"purple", 0x800080}, {"rebeccapurple", 0x663399}, {"red", 0xFF0000}, {"rosybrown", 0xBC8F8F},
    {"royalblue", 0x4169E1}, {"saddlebrown", 0x8B4513}, {"salmon", 0xFA8072}, {"sandybrown", 0xF4A460}, {"seagreen", 0x2E8B57}, {"seashell", 0xFFF5EE},
    {"sienna", 0xA0522D}, {"silver", 0xC0C0C0}, {"skyblue", 0x87CEEB}, {"slateblue", 0x6A5ACD}, {"slategray", 0x708090}, {"slategrey", 0x708090}, {"snow", 0xFFFAFA},
    {"springgreen", 0x00FF7F}, {"steelblue", 0x4682B4}, {"tan", 0xD2B48C}, {"teal", 0x008080}, {"thistle", 0xD8BFD8}, {"tomato", 0xFF6347}, {"turquoise", 0x40E0D0},
    {"violet", 0xEE82EE}, {"wheat", 0xF5DEB3}, {"white", 0xFFFFFF}, {"whitesmoke", 0xF5F5F5}, {"yellow", 0xFFFF00}, {"yellowgreen", 0x9ACD32}};

// --- the regular expressions of setStyle, by hand ------------------------------------

struct Cursor {
  const char* p;
  const char* end;
  bool done() const { return p >= end; }
  char peek(size_t ahead = 0) const { return p + ahead < end ? p[ahead] : '\0'; }
};
inline bool isDigit(char c) { return c >= '0' && c <= '9'; }
inline bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }
inline bool isWord(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || isDigit(c) || c == '_'; }
inline int hexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}
/** \s* */
inline void spaces(Cursor& c) {
  while (!c.done() && isSpace(*c.p)) c.p++;
}
/** one character */
inline bool take(Cursor& c, char ch) {
  if (c.done() || *c.p != ch) return false;
  c.p++;
  return true;
}
/** (\d+), read as parseInt reads it */
inline bool integer(Cursor& c, double& out) {
  const char* start = c.p;
  while (!c.done() && isDigit(*c.p)) c.p++;
  if (c.p == start) return false;
  out = std::strtod(std::string(start, c.p).c_str(), nullptr);
  return true;
}
/** (\d+(?:\.\d+)?|\.\d+), with a sign before it when `sign` – read as parseFloat reads it */
inline bool number(Cursor& c, double& out, bool sign = false) {
  const char* start = c.p;
  if (sign && !c.done() && (*c.p == '+' || *c.p == '-')) c.p++;
  if (!c.done() && isDigit(*c.p)) {
    while (!c.done() && isDigit(*c.p)) c.p++;
    if (c.peek() == '.' && isDigit(c.peek(1))) {
      c.p++;
      while (!c.done() && isDigit(*c.p)) c.p++;
    }
  } else if (c.peek() == '.' && isDigit(c.peek(1))) {
    c.p++;
    while (!c.done() && isDigit(*c.p)) c.p++;
  } else {
    c.p = start;
    return false;
  }
  out = std::strtod(std::string(start, c.p).c_str(), nullptr);
  return true;
}
/** \s*(?:,\s*(number)\s*)?$ – the alpha, which is ignored, and the end of the text */
inline bool alphaAndEnd(Cursor& c) {
  spaces(c);
  if (take(c, ',')) {
    spaces(c);
    double alpha;
    if (!number(c, alpha)) return false;
    spaces(c);
  }
  return c.done();
}
/** ^\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*(?:,\s*(number)\s*)?$ */
inline bool rgbIntegers(const std::string& s, double v[3]) {
  Cursor c{s.data(), s.data() + s.size()};
  for (int i = 0; i < 3; i++) {
    spaces(c);
    if (i > 0) {
      if (!take(c, ',')) return false;
      spaces(c);
    }
    if (!integer(c, v[i])) return false;
  }
  return alphaAndEnd(c);
}
/** ^\s*(number)\%\s*,\s*(number)\%\s*,\s*(number)\%\s*(?:,\s*(number)\s*)?$ */
inline bool rgbPercent(const std::string& s, double v[3]) {
  Cursor c{s.data(), s.data() + s.size()};
  for (int i = 0; i < 3; i++) {
    spaces(c);
    if (i > 0) {
      if (!take(c, ',')) return false;
      spaces(c);
    }
    if (!number(c, v[i]) || !take(c, '%')) return false;
  }
  return alphaAndEnd(c);
}
/** ^\s*([+-]?number)\s*,\s*(number)\%\s*,\s*(number)\%\s*(?:,\s*(number)\s*)?$ */
inline bool hslNumbers(const std::string& s, double v[3]) {
  Cursor c{s.data(), s.data() + s.size()};
  spaces(c);
  if (!number(c, v[0], true)) return false;
  for (int i = 1; i < 3; i++) {
    spaces(c);
    if (!take(c, ',')) return false;
    spaces(c);
    if (!number(c, v[i]) || !take(c, '%')) return false;
  }
  return alphaAndEnd(c);
}

}  // namespace colordetail

inline Color& Color::setHex(double hex, ColorSpace colorSpace) {
  const int32_t h = jsInt32(std::floor(hex));
  r = (h >> 16 & 255) / 255.0;
  g = (h >> 8 & 255) / 255.0;
  b = (h & 255) / 255.0;
  ColorManagement::colorSpaceToWorking(*this, colorSpace);
  return *this;
}

inline Color& Color::setRGB(double r_, double g_, double b_, ColorSpace colorSpace) {
  r = r_;
  g = g_;
  b = b_;
  ColorManagement::colorSpaceToWorking(*this, colorSpace);
  return *this;
}

inline Color& Color::setHSL(double h, double s, double l, ColorSpace colorSpace) {
  // h,s,l ranges are in 0.0 - 1.0
  h = MathUtils::euclideanModulo(h, 1);
  s = MathUtils::clamp(s, 0, 1);
  l = MathUtils::clamp(l, 0, 1);
  if (s == 0) {
    r = g = b = l;
  } else {
    const double p = l <= 0.5 ? l * (1 + s) : l + s - (l * s);
    const double q = (2 * l) - p;
    r = colordetail::hue2rgb(q, p, h + 1.0 / 3);
    g = colordetail::hue2rgb(q, p, h);
    b = colordetail::hue2rgb(q, p, h - 1.0 / 3);
  }
  ColorManagement::colorSpaceToWorking(*this, colorSpace);
  return *this;
}

inline Color& Color::setStyle(const std::string& style, ColorSpace colorSpace) {
  using namespace colordetail;
  // /^(\w+)\(([^\)]*)\)/
  size_t nameEnd = 0;
  while (nameEnd < style.size() && isWord(style[nameEnd])) nameEnd++;
  const size_t close = nameEnd > 0 && nameEnd < style.size() && style[nameEnd] == '(' ? style.find(')', nameEnd + 1) : std::string::npos;
  if (close != std::string::npos) {
    // rgb / hsl
    const std::string name = style.substr(0, nameEnd);
    const std::string components = style.substr(nameEnd + 1, close - nameEnd - 1);
    double v[3];
    if (name == "rgb" || name == "rgba") {
      if (rgbIntegers(components, v)) {
        // rgb(255,0,0) rgba(255,0,0,0.5)
        return setRGB(jsMin(255, v[0]) / 255, jsMin(255, v[1]) / 255, jsMin(255, v[2]) / 255, colorSpace);
      }
      if (rgbPercent(components, v)) {
        // rgb(100%,0%,0%) rgba(100%,0%,0%,0.5)
        return setRGB(jsMin(100, v[0]) / 100, jsMin(100, v[1]) / 100, jsMin(100, v[2]) / 100, colorSpace);
      }
    } else if (name == "hsl" || name == "hsla") {
      if (hslNumbers(components, v)) {
        // hsl(120,50%,50%) hsla(120,50%,50%,0.5)
        return setHSL(v[0] / 360, v[1] / 100, v[2] / 100, colorSpace);
      }
    }
    // (an unknown colour model, or numbers that are not as they must be: the colour stays)
    return *this;
  }
  // /^\#([A-Fa-f\d]+)$/
  bool hexText = style.size() > 1 && style[0] == '#';
  for (size_t i = 1; hexText && i < style.size(); i++) hexText = hexDigit(style[i]) >= 0;
  if (hexText) {
    // hex color
    const size_t size = style.size() - 1;
    if (size == 3) {
      // #ff0
      return setRGB(hexDigit(style[1]) / 15.0, hexDigit(style[2]) / 15.0, hexDigit(style[3]) / 15.0, colorSpace);
    } else if (size == 6) {
      // #ff0000
      double hex = 0;
      for (size_t i = 1; i <= 6; i++) hex = hex * 16 + hexDigit(style[i]);
      return setHex(hex, colorSpace);
    }
    // (an invalid hex colour: the colour stays)
    return *this;
  }
  if (!style.empty()) return setColorName(style, colorSpace);
  return *this;
}

inline bool Color::nameToHex(const std::string& name, uint32_t& hex) {
  for (const colordetail::NamedColor& k : colordetail::colorKeywords) {
    if (name == k.name) {
      hex = k.hex;
      return true;
    }
  }
  return false;
}

inline Color& Color::setColorName(const std::string& style, ColorSpace colorSpace) {
  // color keywords
  std::string lower = style;
  for (char& c : lower)
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  uint32_t hex;
  if (nameToHex(lower, hex)) {
    // red
    setHex(hex, colorSpace);
  }
  // (an unknown colour: the colour stays)
  return *this;
}

inline uint32_t Color::getHex(ColorSpace colorSpace) const {
  Color color = *this;
  ColorManagement::workingToColorSpace(color, colorSpace);
  return jsUint32(jsRound(MathUtils::clamp(color.r * 255, 0, 255)) * 65536 + jsRound(MathUtils::clamp(color.g * 255, 0, 255)) * 256 +
                  jsRound(MathUtils::clamp(color.b * 255, 0, 255)));
}

inline std::string Color::getHexString(ColorSpace colorSpace) const {
  static const char digits[] = "0123456789abcdef";
  const uint32_t hex = getHex(colorSpace);
  std::string s(6, '0');
  for (int i = 0; i < 6; i++) s[static_cast<size_t>(5 - i)] = digits[(hex >> (4 * i)) & 15];
  return s;
}

inline HSL& Color::getHSL(HSL& target, ColorSpace colorSpace) const {
  // h,s,l ranges are in 0.0 - 1.0
  Color color = *this;
  ColorManagement::workingToColorSpace(color, colorSpace);
  const double r_ = color.r, g_ = color.g, b_ = color.b;
  const double max = mathdetail::max3(r_, g_, b_);
  const double min = mathdetail::min3(r_, g_, b_);
  double hue = 0, saturation;
  const double lightness = (min + max) / 2.0;
  if (min == max) {
    hue = 0;
    saturation = 0;
  } else {
    const double delta = max - min;
    saturation = lightness <= 0.5 ? delta / (max + min) : delta / (2 - max - min);
    if (max == r_) hue = (g_ - b_) / delta + (g_ < b_ ? 6 : 0);
    else if (max == g_) hue = (b_ - r_) / delta + 2;
    else if (max == b_) hue = (r_ - g_) / delta + 4;
    hue /= 6;
  }
  target.h = hue;
  target.s = saturation;
  target.l = lightness;
  return target;
}

inline Color& Color::getRGB(Color& target, ColorSpace colorSpace) const {
  Color color = *this;
  ColorManagement::workingToColorSpace(color, colorSpace);
  target.r = color.r;
  target.g = color.g;
  target.b = color.b;
  return target;
}

inline std::string Color::getStyle(ColorSpace colorSpace) const {
  Color color = *this;
  ColorManagement::workingToColorSpace(color, colorSpace);
  const double r_ = color.r, g_ = color.g, b_ = color.b;
  if (colorSpace != SRGBColorSpace) {
    // Requires CSS Color Module Level 4 (https://www.w3.org/TR/css-color-4/).
    const char* name = colorSpace == LinearSRGBColorSpace ? "srgb-linear" : "";
    return std::string("color(") + name + " " + toFixed(r_, 3) + " " + toFixed(g_, 3) + " " + toFixed(b_, 3) + ")";
  }
  return "rgb(" + jsStr(jsRound(r_ * 255)) + "," + jsStr(jsRound(g_ * 255)) + "," + jsStr(jsRound(b_ * 255)) + ")";
}

inline Vector3& Vector3::setFromColor(const Color& c) {
  x = c.r;
  y = c.g;
  z = c.b;
  return *this;
}

}  // namespace fs
