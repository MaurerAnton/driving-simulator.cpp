// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The fonts of the native 2D canvas (canvas_native.cpp): a CSS font ("600 13px
 * system-ui, sans-serif") is matched to a font file, a text is laid out glyph by
 * glyph (with the font's kerning, letter spacing, a fallback font for characters
 * the first one lacks), and each glyph's outline is handed over to be filled.
 *
 * The fonts: third_party/fonts (Noto Sans for the system and Arial-like families,
 * Roboto Condensed for the DIN-like families of the signs and instruments – both
 * SIL OFL 1.1).  Characters they lack (Chinese, Arabic, Hindi …) are looked for in
 * the machine's fonts (Noto, DejaVu – /usr/share/fonts), when there are any.
 * There is no text shaping: Arabic letters are not joined and right-to-left text
 * is laid out left to right.
 */
#pragma once

#include <string>
#include <vector>

namespace fs::canvasFonts {

struct Face;

/** a CSS font, read */
struct FontSpec {
  /** the families, lower case, without quotes */
  std::vector<std::string> families = {"sans-serif"};
  int weight = 400;
  bool italic = false;
  /** the size in pixels */
  double px = 10;
};

/** read the CSS `font` shorthand; false: it is not one (the font stays as it was, as in a browser) */
bool parseFont(const std::string& css, FontSpec& out);

/** one glyph laid out: its face, its index there, where its origin is (pixels from the text's start, along the baseline) */
struct Glyph {
  Face* face = nullptr;
  int index = 0;
  double x = 0;
};

struct Layout {
  std::vector<Glyph> glyphs;
  /** the advance of the whole text (what measureText's width is) */
  double width = 0;
  /** the inked box, from the text's start on the baseline: left (positive: left of the start), right, up (above the baseline), down */
  double inkLeft = 0, inkRight = 0, inkUp = 0, inkDown = 0;
  /** the font's ascent and descent (pixels, both positive) */
  double ascent = 0, descent = 0;
  /** the face has no italic: slant the glyphs (as browsers do) */
  bool slant = false;
  /** the size the glyphs are drawn at */
  double px = 10;
};

/** lay out a UTF-8 text (`letterSpacing`: pixels added after each character); false: no font at all on this machine */
bool layout(const FontSpec& font, const std::string& text, double letterSpacing, Layout& out);

/** where an outline goes (coordinates in pixels, y downwards, the glyph's origin at 0, 0) */
struct OutlineSink {
  virtual ~OutlineSink() = default;
  virtual void moveTo(double x, double y) = 0;
  virtual void lineTo(double x, double y) = 0;
  virtual void quadTo(double cx, double cy, double x, double y) = 0;
  virtual void cubicTo(double c1x, double c1y, double c2x, double c2y, double x, double y) = 0;
  virtual void close() = 0;
};

/** the outline of one glyph at `px` pixels */
void outline(const Glyph& g, double px, OutlineSink& sink);

}  // namespace fs::canvasFonts
