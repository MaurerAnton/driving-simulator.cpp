// SPDX-License-Identifier: AGPL-3.0-only
/**
 * What an image file says about itself, read from its first bytes without
 * decoding the picture: size, GPS position, viewing direction, date and
 * whether it is a 360° panorama.
 *
 * Understands JPEG, PNG and WebP with EXIF (GPS IFD, DateTimeOriginal) and
 * XMP, in particular Google's Photo Sphere tags (GPano:ProjectionType,
 * GPano:PoseHeadingDegrees, …) which street-view download tools, phone
 * cameras and 360° cameras write.
 *
 * Origin: src/reality/imagemeta.ts (MaurerAnton/driving-simulator @ 578e397,
 * v0.4.0).
 */
#include "reality/imagemeta.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

namespace fs {

namespace {

std::string ascii(const std::vector<uint8_t>& b, size_t from, size_t to) {
  std::string s;
  size_t end = std::min(to, b.size());
  for (size_t i = from; i < end; i++) s += static_cast<char>(b[i]);
  return s;
}

bool startsWith(const std::vector<uint8_t>& b, size_t at, const std::string& text) {
  if (at + text.size() > b.size()) return false;
  for (size_t i = 0; i < text.size(); i++)
    if (b[at + i] != static_cast<uint8_t>(text[i])) return false;
  return true;
}

// ------------------------------------------------------------------ EXIF

struct ExifEntry {
  int type = 0;
  size_t count = 0;
  size_t off = 0;
};

/** Entries of one directory: tag → (type, count, offset of the value). */
std::map<int, ExifEntry> readDir(const std::vector<uint8_t>& b, size_t start, size_t off,
                                 size_t end) {
  std::map<int, ExifEntry> out;
  size_t at = start + off;
  if (off == 0 || at + 2 > end) return out;
  auto u16 = [&](size_t o) -> uint16_t {
    if (o + 2 > end) return 0;
    return static_cast<uint16_t>(b[o] | (b[o + 1] << 8));
  };
  auto u32 = [&](size_t o) -> uint32_t {
    if (o + 4 > end) return 0;
    return static_cast<uint32_t>(b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24));
  };
  int n = u16(at);
  for (int i = 0; i < n && at + 2 + (i + 1) * 12 <= end; i++) {
    size_t e = at + 2 + i * 12;
    ExifEntry entry;
    entry.type = u16(e + 2);
    entry.count = u32(e + 4);
    size_t size = (entry.type == 3 ? 2 : (entry.type == 4 || entry.type == 9 ? 4
                                        : (entry.type == 5 || entry.type == 10 ? 8 : 1))) *
                  entry.count;
    entry.off = size <= 4 ? e + 8 : start + u32(e + 8);
    out[u16(e)] = entry;
  }
  return out;
}

double rational(const std::vector<uint8_t>& b, size_t o, size_t end) {
  if (o + 8 > end) return NAN;
  auto u32 = [&](size_t p) -> uint32_t {
    if (p + 4 > end) return 0;
    return static_cast<uint32_t>(b[p] | (b[p + 1] << 8) | (b[p + 2] << 16) | (b[p + 3] << 24));
  };
  uint32_t den = u32(o + 4);
  return den == 0 ? NAN : static_cast<double>(u32(o)) / den;
}

/** The n rationals of an entry, or an empty vector when it does not fit. */
std::vector<double> rationals(const std::vector<uint8_t>& b, const ExifEntry* e, size_t n,
                              size_t end) {
  std::vector<double> out;
  if (!e || (e->type != 5 && e->type != 10) || e->count < n || e->off + n * 8 > end) return out;
  for (size_t i = 0; i < n; i++) out.push_back(rational(b, e->off + i * 8, end));
  return out;
}

std::string text(const std::vector<uint8_t>& b, const ExifEntry* e, size_t end) {
  if (!e || e->type != 2) return "";
  std::string s = ascii(b, e->off, std::min(e->off + e->count, end));
  size_t z = s.find('\0');
  if (z != std::string::npos) s.erase(z);
  return s;
}

void readExif(const std::vector<uint8_t>& b, size_t start, size_t end, ImageMeta& m) {
  if (end - start < 8) return;
  bool le = b[start] == 0x49 && b[start + 1] == 0x49;
  if (!le && !(b[start] == 0x4d && b[start + 1] == 0x4d)) return;
  auto u16 = [&](size_t o) -> uint16_t {
    if (o + 2 > end) return 0;
    return static_cast<uint16_t>(le ? (b[o] | (b[o + 1] << 8)) : ((b[o] << 8) | b[o + 1]));
  };
  auto u32 = [&](size_t o) -> uint32_t {
    if (o + 4 > end) return 0;
    uint32_t v = le ? static_cast<uint32_t>(b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24))
                    : static_cast<uint32_t>((b[o] << 24) | (b[o + 1] << 16) | (b[o + 2] << 8) | b[o + 3]);
    return v;
  };
  if (u16(start + 2) != 42) return;

  auto ifd0 = readDir(b, start, u32(start + 4), end);
  std::map<int, ExifEntry> exif;
  auto exifPtr = ifd0.find(0x8769);
  if (exifPtr != ifd0.end()) exif = readDir(b, start, u32(exifPtr->second.off), end);

  const ExifEntry* e9003 = nullptr, *e9004 = nullptr, *e0132 = nullptr;
  auto a = exif.find(0x9003);
  if (a != exif.end()) e9003 = &a->second;
  auto b4 = exif.find(0x9004);
  if (b4 != exif.end()) e9004 = &b4->second;
  auto c = ifd0.find(0x0132);
  if (c != ifd0.end()) e0132 = &c->second;
  std::string stamp = text(b, e9003, end);
  if (stamp.empty()) stamp = text(b, e9004, end);
  if (stamp.empty()) stamp = text(b, e0132, end);
  int Y, Mo, D, H, Mi, S;
  if (std::sscanf(stamp.c_str(), "%d:%d:%d %d:%d:%d", &Y, &Mo, &D, &H, &Mi, &S) == 6) {
    // days since 1970 (the JS Date.UTC of the camera's local time read as UTC)
    static const int kDays[13] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365};
    int days = (Y - 1970) * 365 + ((Y - 1969) / 4) - ((Y - 1901) / 100) + ((Y + 2099) / 400) +
               kDays[Mo > 0 && Mo < 13 ? Mo : 1] + (Mo > 2 && Y % 4 == 0 && (Y % 100 != 0 || Y % 400 == 0) ? 1 : 0) + (D - 1);
    m.time = static_cast<double>(days) * 86400000.0 + H * 3600000.0 + Mi * 60000.0 + S * 1000.0;
  }

  auto gpsPtr = ifd0.find(0x8825);
  if (gpsPtr == ifd0.end()) return;
  auto gps = readDir(b, start, u32(gpsPtr->second.off), end);
  auto dms = [&](int tag) -> double {
    const ExifEntry* e = nullptr;
    auto it = gps.find(tag);
    if (it != gps.end()) e = &it->second;
    auto r = rationals(b, e, 3, end);
    if (r.size() < 3) return NAN;
    return r[0] + r[1] / 60 + r[2] / 3600;
  };
  double lat = dms(2), lon = dms(4);
  if (std::isfinite(lat) && std::isfinite(lon) && !(lat == 0 && lon == 0)) {
    auto refLat = gps.find(1);
    auto refLon = gps.find(3);
    std::string slat = refLat == gps.end() ? "" : text(b, &refLat->second, end);
    std::string slon = refLon == gps.end() ? "" : text(b, &refLon->second, end);
    if (!slat.empty() && (slat[0] == 'S' || slat[0] == 's')) lat = -lat;
    if (!slon.empty() && (slon[0] == 'W' || slon[0] == 'w')) lon = -lon;
    m.lat = lat;
    m.lon = lon;
  }
  // the pose of a panorama (XMP) describes the middle of the picture and wins
  // wherever it stands in the file
  auto dirnIt = gps.find(17);
  const ExifEntry* dirnE = dirnIt == gps.end() ? nullptr : &dirnIt->second;
  auto dirn = rationals(b, dirnE, 1, end);
  if (!dirn.empty() && std::isfinite(dirn[0]) && std::isnan(m.heading))
    m.heading = std::fmod(dirn[0], 360.0) + (dirn[0] < 0 ? 360 : 0);
}

// ------------------------------------------------------------------- XMP

/** attribute form  GPano:Foo="…"  or element form  <GPano:Foo>…</GPano:Foo> */
std::string xmpValue(const std::string& xmp, const std::string& name) {
  size_t p = xmp.find(name);
  if (p == std::string::npos) return "";
  size_t eq = xmp.find('=', p + name.size());
  if (eq != std::string::npos && eq - p - name.size() < 4) {
    // attribute form
    size_t q = xmp.find_first_of("\"'", eq + 1);
    if (q != std::string::npos) {
      char quote = xmp[q];
      size_t endq = xmp.find(quote, q + 1);
      if (endq != std::string::npos) {
        std::string v = xmp.substr(q + 1, endq - q - 1);
        size_t s = v.find_first_not_of(" \t\r\n");
        size_t e2 = v.find_last_not_of(" \t\r\n");
        return s == std::string::npos ? "" : v.substr(s, e2 - s + 1);
      }
    }
  }
  // element form
  size_t gt = xmp.find('>', p + name.size());
  if (gt == std::string::npos) return "";
  size_t close = xmp.find("</" + name, gt);
  if (close == std::string::npos) return "";
  std::string v = xmp.substr(gt + 1, close - gt - 1);
  size_t s = v.find_first_not_of(" \t\r\n");
  size_t e2 = v.find_last_not_of(" \t\r\n");
  return s == std::string::npos ? "" : v.substr(s, e2 - s + 1);
}

/** XMP writes positions as "51,15.3174N" or "51,15,19.04N". */
double xmpCoordinate(const std::string& v) {
  // <deg>[,<min>[,<sec>]]<N|S|E|W>  — any number of parts, the rest is zero
  double parts[3] = {0, 0, 0};
  char hemi = 0;
  int n = std::sscanf(v.c_str(), "%lf,%lf,%lf%c", &parts[0], &parts[1], &parts[2], &hemi);
  if (n < 1) return NAN;
  double d = parts[0] + (n >= 2 ? parts[1] : 0) / 60 + (n >= 3 ? parts[2] : 0) / 3600;
  if (hemi == 'S' || hemi == 's' || hemi == 'W' || hemi == 'w') d = -d;
  return d;
}

void readXmp(const std::string& xmp, ImageMeta& m) {
  std::string projection = xmpValue(xmp, "GPano:ProjectionType");
  std::transform(projection.begin(), projection.end(), projection.begin(), ::tolower);
  if (!projection.empty()) m.pano = projection == "equirectangular" ? 1 : 0;
  if (m.pano == 1) {
    // a cropped photo sphere (a strip of the horizon) is no full panorama
    double full = std::atof(xmpValue(xmp, "GPano:FullPanoWidthPixels").c_str());
    double crop = std::atof(xmpValue(xmp, "GPano:CroppedAreaImageWidthPixels").c_str());
    if (full > 0 && crop > 0 && crop < full * 0.95) m.pano = 0;
  }
  std::string pose = xmpValue(xmp, "GPano:PoseHeadingDegrees");
  if (!pose.empty()) {
    double h = std::atof(pose.c_str());
    if (std::isfinite(h)) m.heading = std::fmod(h, 360.0) + (h < 0 ? 360 : 0);
  }
  if (std::isnan(m.lat)) {
    double lat = xmpCoordinate(xmpValue(xmp, "exif:GPSLatitude"));
    double lon = xmpCoordinate(xmpValue(xmp, "exif:GPSLongitude"));
    if (std::isfinite(lat) && std::isfinite(lon)) {
      m.lat = lat;
      m.lon = lon;
    }
  }
  if (std::isnan(m.time)) {
    // the camera's local time, read as UTC (as Date.parse does)
    std::string t = xmpValue(xmp, "GPano:FirstPhotoDate");
    if (t.empty()) t = xmpValue(xmp, "xmp:CreateDate");
    if (t.empty()) t = xmpValue(xmp, "exif:DateTimeOriginal");
    // "YYYY-MM-DDTHH:MM:SS" → the same day count as above
    int Y, Mo, D, H = 0, Mi = 0, S = 0;
    if (std::sscanf(t.c_str(), "%d-%d-%dT%d:%d:%d", &Y, &Mo, &D, &H, &Mi, &S) >= 3) {
      static const int kDays[13] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365};
      int days = (Y - 1970) * 365 + ((Y - 1969) / 4) - ((Y - 1901) / 100) + ((Y + 2099) / 400) +
                 kDays[Mo > 0 && Mo < 13 ? Mo : 1] + (Mo > 2 && Y % 4 == 0 && (Y % 100 != 0 || Y % 400 == 0) ? 1 : 0) + (D - 1);
      m.time = static_cast<double>(days) * 86400000.0 + H * 3600000.0 + Mi * 60000.0 + S * 1000.0;
    }
  }
}

// ------------------------------------------------------------------ files

/** the XMP packet's namespace, with the trailing NUL the spec requires */
const std::string XMP_JPEG = std::string("http://ns.adobe.com/xap/1.0/") + '\0';



void readJpeg(const std::vector<uint8_t>& b, ImageMeta& m) {
  m.kind = "jpeg";
  size_t p = 2;
  while (p + 4 <= b.size()) {
    if (b[p] != 0xff) {
      p++;
      continue;
    }
    uint8_t marker = b[p + 1];
    if (marker == 0xff) {
      p++;
      continue;
    }
    if (marker == 0xd8 || (marker >= 0xd0 && marker <= 0xd7) || marker == 0x01) {
      p += 2;
      continue;
    }
    if (marker == 0xda || marker == 0xd9) break;  // image data follows
    size_t len = (static_cast<size_t>(b[p + 2]) << 8) | b[p + 3];
    size_t body = p + 4;
    size_t end = std::min(p + 2 + len, b.size());
    if (marker == 0xe1) {
      if (startsWith(b, body, "Exif\0\0")) readExif(b, body + 6, end, m);
      else if (startsWith(b, body, XMP_JPEG))
        readXmp(ascii(b, body + XMP_JPEG.size(), end), m);
    } else if (marker >= 0xc0 && marker <= 0xcf && marker != 0xc4 && marker != 0xc8 &&
               marker != 0xcc && body + 5 <= b.size()) {
      m.height = (static_cast<int>(b[body + 1]) << 8) | b[body + 2];
      m.width = (static_cast<int>(b[body + 3]) << 8) | b[body + 4];
    }
    if (len < 2) break;
    p += 2 + len;
  }
}

void readPng(const std::vector<uint8_t>& b, ImageMeta& m) {
  m.kind = "png";
  auto u32 = [&](size_t o) -> uint32_t {
    if (o + 4 > b.size()) return 0;
    return static_cast<uint32_t>((b[o] << 24) | (b[o + 1] << 16) | (b[o + 2] << 8) | b[o + 3]);
  };
  size_t p = 8;
  while (p + 12 <= b.size()) {
    uint32_t len = u32(p);
    std::string type = ascii(b, p + 4, p + 8);
    size_t body = p + 8;
    size_t end = std::min(body + len, b.size());
    if (type == "IHDR" && body + 8 <= b.size()) {
      m.width = static_cast<int>(u32(body));
      m.height = static_cast<int>(u32(body + 4));
    } else if (type == "eXIf") {
      readExif(b, body, end, m);
    } else if (type == "iTXt" && startsWith(b, body, "XML:com.adobe.xmp\0")) {
      // keyword\0 compression flag, method, language\0, translated keyword\0, text
      size_t q = body + 18;
      bool compressed = b[q] == 1;
      q += 2;
      while (q < end && b[q] != 0) q++;
      q++;
      while (q < end && b[q] != 0) q++;
      q++;
      if (!compressed && q < end) readXmp(ascii(b, q, end), m);
    } else if (type == "IDAT" || type == "IEND") {
      break;
    }
    p = body + len + 4;
  }
}

void readWebp(const std::vector<uint8_t>& b, ImageMeta& m) {
  m.kind = "webp";
  auto u32 = [&](size_t o) -> uint32_t {
    if (o + 4 > b.size()) return 0;
    return static_cast<uint32_t>(b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24));
  };
  size_t p = 12;
  while (p + 8 <= b.size()) {
    std::string type = ascii(b, p, p + 4);
    uint32_t len = u32(p + 4);
    size_t body = p + 8;
    size_t end = std::min(body + len, b.size());
    if (type == "VP8X" && body + 10 <= b.size()) {
      m.width = 1 + (b[body + 4] | (b[body + 5] << 8) | (b[body + 6] << 16));
      m.height = 1 + (b[body + 7] | (b[body + 8] << 8) | (b[body + 9] << 16));
    } else if (type == "VP8 " && m.width == 0 && body + 10 <= b.size() &&
               b[body + 3] == 0x9d && b[body + 4] == 0x01 && b[body + 5] == 0x2a) {
      m.width = (b[body + 6] | (b[body + 7] << 8)) & 0x3fff;
      m.height = (b[body + 8] | (b[body + 9] << 8)) & 0x3fff;
    } else if (type == "VP8L" && m.width == 0 && body + 5 <= b.size() && b[body] == 0x2f) {
      uint32_t bits = u32(body + 1);
      m.width = 1 + (bits & 0x3fff);
      m.height = 1 + ((bits >> 14) & 0x3fff);
    } else if (type == "EXIF") {
      readExif(b, startsWith(b, body, "Exif\0\0") ? body + 6 : body, end, m);
    } else if (type == "XMP ") {
      readXmp(ascii(b, body, end), m);
    }
    p = body + len + (len & 1);
  }
}

}  // namespace

ImageMeta readImageMeta(const std::vector<uint8_t>& bytes) {
  ImageMeta m;
  try {
    if (bytes.size() > 3 && bytes[0] == 0xff && bytes[1] == 0xd8) {
      readJpeg(bytes, m);
    } else if (startsWith(bytes, 0, std::string("\x89PNG\r\n\x1a\n", 8))) {
      readPng(bytes, m);
    } else if (startsWith(bytes, 0, "RIFF") && startsWith(bytes, 8, "WEBP")) {
      readWebp(bytes, m);
    }
  } catch (...) {
    /* a damaged file simply tells less */
  }
  return m;
}

bool looksLikePanorama(const ImageMeta& meta) {
  if (meta.pano != -1) return meta.pano == 1;
  return meta.width >= 2000 && meta.height > 0 &&
         std::fabs(static_cast<double>(meta.width) / meta.height - 2) < 0.02;
}

}  // namespace fs
