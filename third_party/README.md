# Third-party code and fonts (native build only)

The native 2D canvas (`src/platform/native/canvas_native.cpp`, `canvas_fonts.cpp`) uses:

| Folder | What | Version | Licence |
|---|---|---|---|
| `plutovg/` | 2D vector rasteriser (with stb_truetype, stb_image, stb_image_write inside; FreeType-derived raster/stroker parts, see `source/FTL.TXT`) | v1.3.3, github.com/sammycage/plutovg | MIT (`LICENSE`); FTL for the FreeType parts |
| `plutosvg/` | SVG renderer on plutovg (`source/plutosvg.c`, `.h` only) | v0.0.8, github.com/sammycage/plutosvg | MIT (`LICENSE`) |
| `fonts/NotoSans-*.ttf` | Noto Sans 400/600/700/800 and 400/800 italic: static instances of the variable font of github.com/google/fonts (`ofl/notosans`), subset to Latin, Greek, Cyrillic, punctuation, arrows and symbols | google/fonts main, 2026-11 | SIL OFL 1.1 (`fonts/OFL-NotoSans.txt`) |
| `fonts/RobotoCondensed-*.ttf` | Roboto Condensed 400/600/700/800 (the narrow, DIN-like face of signs, plates and instruments), made the same way (`ofl/robotocondensed`) | google/fonts main, 2026-11 | SIL OFL 1.1 (`fonts/OFL-RobotoCondensed.txt`) |

The fonts were made with fontTools: `fonttools varLib.instancer <font> wght=<w> [wdth=100]`, then
`fonttools subset --unicodes=U+0000-036F,U+0370-03FF,U+0400-052F,U+1E00-1EFF,U+2000-206F,U+20A0-20CF,U+2100-215F,U+2190-21FF,U+2200-22FF,U+2300-23FF,U+25A0-25FF,U+2600-27BF,U+FB00-FB06,U+FFFD --layout-features=kern,liga,calt`.
Characters they lack (CJK, Arabic, Devanagari, Bengali …) are taken from the machine's fonts where there are any.
The browser build uses none of this (the browser's canvas and fonts).
