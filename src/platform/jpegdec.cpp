// SPDX-License-Identifier: AGPL-3.0-only
/** Baseline JPEG, read into pixels (see jpegdec.h). */
#include "platform/jpegdec.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace fs {

namespace {

/** zigzag order: natural index → position in the scan (Annex K, as the encoder writes it) */
const int kZigzag[64] = {0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,
                         12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6,  7,  14, 21, 28,
                         35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
                         58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

/** one Huffman table: the code of each symbol by its length (1…16) */
struct Huff {
  uint16_t minCode[17] = {0}, maxCode[17] = {0};
  int valPtr[17] = {0};
  uint8_t counts[16] = {0};
  std::vector<uint8_t> values;
  bool set = false;

  void build(const uint8_t countsIn[16]) {
    for (int i = 0; i < 16; i++) counts[i] = countsIn[i];
    int code = 0;
    int k = 0;
    for (int len = 1; len <= 16; len++) {
      if (countsIn[len - 1]) {
        valPtr[len] = k;
        minCode[len] = static_cast<uint16_t>(code);
        code += countsIn[len - 1];
        k += countsIn[len - 1];
        maxCode[len] = static_cast<uint16_t>(code - 1);
      } else {
        // no codes of this length: the range test must never match one
        valPtr[len] = k;
        minCode[len] = 0;
        maxCode[len] = 0xffff;
      }
      code <<= 1;
    }
    set = true;
  }
};

struct Component {
  int id = 0;
  int h = 1, v = 1;      // sampling factors
  int tq = 0;            // quantisation table
  int td = 0, ta = 0;    // Huffman tables of the scan
  int dcPred = 0;
  int blocksWide = 0, blocksHigh = 0;
  std::vector<int16_t> coefs;  // blocksWide × blocksHigh × 64 (natural order)
};

/** the bit reader over the entropy-coded scan (byte stuffing undone) */
struct Bits {
  const std::vector<uint8_t>& data;
  size_t pos = 0;
  uint32_t buf = 0;
  int have = 0;
  bool eof = false;

  explicit Bits(const std::vector<uint8_t>& d, size_t from) : data(d), pos(from) {}

  int bit() {
    if (eof) return 0;
    if (have == 0) {
      if (pos >= data.size()) {
        eof = true;
        return 0;
      }
      uint8_t b = data[pos++];
      if (b == 0xff) {
        // stuffed zero, or a marker: a marker ends the scan (pad: the rest reads as zero)
        if (pos < data.size() && data[pos] == 0x00) pos++;
        else {
          eof = true;
          return 0;
        }
      }
      buf = b;
      have = 8;
    }
    have--;
    return static_cast<int>((buf >> have) & 1);
  }

  int receive(int n) {
    int v = 0;
    for (int i = 0; i < n; i++) v = (v << 1) | bit();
    return v;
  }

  /** the value of `cat` bits as the JPEG writes it (ones' complement for negatives) */
  int receiveExtend(int cat) {
    if (cat == 0) return 0;
    int v = receive(cat);
    return v < (1 << (cat - 1)) ? v - (1 << cat) + 1 : v;
  }

};

/** the symbol a Huffman table stands for at the bits' current place (-1: none) */
int huffDecode(const Huff& h, Bits& bits) {
  int code = 0;
  for (int len = 1; len <= 16; len++) {
    code = (code << 1) | bits.bit();
    if (bits.eof && len > 1) return -1;
    if (h.counts[len - 1] && code >= h.minCode[len] && code <= h.maxCode[len]) {
      int idx = h.valPtr[len] + code - h.minCode[len];
      if (idx < 0 || idx >= static_cast<int>(h.values.size())) return -1;
      return h.values[static_cast<size_t>(idx)];
    }
  }
  return -1;
}

/** one 8×8 inverse DCT (the separable form, as the encoder does it forward) */
void idct8(const int16_t* coef, const uint16_t* quant, uint8_t* out, int outStride) {
  static const double kC[8][8] = {
      {0.35355339, 0.35355339, 0.35355339, 0.35355339, 0.35355339, 0.35355339, 0.35355339, 0.35355339},
      {0.49039264, 0.41573481, 0.27778512, 0.09754516, -0.09754516, -0.27778512, -0.41573481, -0.49039264},
      {0.46193977, 0.19134171, -0.19134171, -0.46193977, -0.46193977, -0.19134171, 0.19134171, 0.46193977},
      {0.41573481, -0.09754516, -0.49039264, -0.27778512, 0.27778512, 0.49039264, 0.09754516, -0.41573481},
      {0.35355339, -0.35355339, -0.35355339, 0.35355339, 0.35355339, -0.35355339, -0.35355339, 0.35355339},
      {0.27778512, -0.49039264, 0.09754516, 0.41573481, -0.41573481, -0.09754516, 0.49039264, -0.27778512},
      {0.19134171, -0.46193977, 0.46193977, -0.19134171, -0.19134171, 0.46193977, -0.46193977, 0.19134171},
      {0.09754516, -0.41573481, 0.49039264, -0.27778512, -0.27778512, 0.49039264, -0.41573481, 0.09754516},
  };
  double tmp[64];
  // kC is laid out as [frequency][position]: the basis of frequency u at
  // position x is kC[u][x].  First along the columns: for a fixed row r,
  // the column frequencies c → output column x.
  for (int r = 0; r < 8; r++) {
    for (int x = 0; x < 8; x++) {
      double sum = 0;
      for (int c = 0; c < 8; c++) sum += coef[r * 8 + c] * quant[r * 8 + c] * kC[c][x];
      tmp[r * 8 + x] = sum;
    }
  }
  // then along the rows: for a fixed column x, the row frequencies v → row y.
  for (int x = 0; x < 8; x++) {
    for (int y = 0; y < 8; y++) {
      double sum = 0;
      for (int v = 0; v < 8; v++) sum += tmp[v * 8 + x] * kC[v][y];
      int val = static_cast<int>(std::lround(sum)) + 128;
      out[y * outStride + x] = static_cast<uint8_t>(val < 0 ? 0 : (val > 255 ? 255 : val));
    }
  }
}

}  // namespace

bool decodeJpeg(const std::vector<uint8_t>& data, std::vector<uint8_t>& out, int& width,
                int& height) {
  if (data.size() < 4 || data[0] != 0xff || data[1] != 0xd8) return false;

  uint16_t quant[4][64];
  std::memset(quant, 0, sizeof quant);
  Huff huff[8];   // [class << 2 | id]: 0/1 = DC luma/chroma, 4/5 = AC luma/chroma
  Component comp[4];
  int nComp = 0;
  int width_ = 0, height_ = 0;
  int restartInterval = 0;
  size_t pos = 2;
  bool sawSOF = false, sawSOS = false;
  size_t scanStart = 0;
  int scanComp[3] = {0, 0, 0};
  int nScan = 0;

  auto u16 = [&](size_t i) -> int {
    return (i + 1 < data.size() ? data[i] : 0) * 256 + (i + 2 < data.size() ? data[i + 1] : 0);
  };

  while (pos + 1 < data.size() && !sawSOS) {
    if (data[pos] != 0xff) {
      pos++;
      continue;
    }
    while (pos < data.size() && data[pos] == 0xff) pos++;
    if (pos >= data.size()) break;
    int marker = data[pos++];
    if (marker == 0xd8 || marker == 0xd9 || (marker >= 0xd0 && marker <= 0xd7)) continue;
    if (pos + 1 >= data.size()) break;
    int len = u16(pos);
    if (len < 2 || pos + static_cast<size_t>(len) > data.size()) return false;
    size_t payload = pos + 2;
    size_t end = pos + static_cast<size_t>(len);

    if (marker == 0xdb) {  // DQT
      size_t p = payload;
      while (p < end) {
        int pq = data[p] >> 4, tq = data[p] & 15;
        p++;
        if (tq > 3) return false;
        for (int i = 0; i < 64; i++) {
          if (p >= end) return false;
          if (pq) {
            if (p + 1 >= end) return false;
            quant[tq][i] = static_cast<uint16_t>(data[p] * 256 + data[p + 1]);
            p += 2;
          } else {
            quant[tq][i] = data[p++];
          }
        }
      }
    } else if (marker == 0xc4) {  // DHT
      size_t p = payload;
      while (p < end) {
        int cls = data[p] >> 4, id = data[p] & 15;
        p++;
        if (cls > 1 || id > 3 || p + 16 > end) return false;
        uint8_t counts[16];
        int total = 0;
        for (int i = 0; i < 16; i++) {
          counts[i] = data[p++];
          total += counts[i];
        }
        if (p + static_cast<size_t>(total) > end) return false;
        Huff& h = huff[cls * 4 + id];
        h.values.assign(data.begin() + static_cast<long>(p),
                        data.begin() + static_cast<long>(p + total));
        h.build(counts);
        p += static_cast<size_t>(total);
      }
    } else if (marker == 0xc0 || marker == 0xc1) {  // SOF0 / SOF1 (baseline)
      if (end - payload < 6) return false;
      int precision = data[payload];
      height_ = u16(payload + 1);
      width_ = u16(payload + 3);
      nComp = data[payload + 5];
      if (precision != 8 || nComp < 1 || nComp > 4) return false;
      for (int c = 0; c < nComp; c++) {
        size_t q = payload + 6 + static_cast<size_t>(c) * 3;
        if (q + 2 >= end) return false;
        comp[c].id = data[q];
        comp[c].h = data[q + 1] >> 4;
        comp[c].v = data[q + 1] & 15;
        comp[c].tq = data[q + 2];
        if (comp[c].h < 1 || comp[c].h > 4 || comp[c].v < 1 || comp[c].v > 4 ||
            comp[c].tq > 3)
          return false;
      }
      sawSOF = true;
    } else if (marker == 0xdd) {  // DRI
      restartInterval = u16(payload);
    } else if (marker == 0xda) {  // SOS
      if (!sawSOF) return false;
      nScan = data[payload];
      if (nScan < 1 || nScan > 3) return false;
      for (int i = 0; i < nScan; i++) {
        size_t q = payload + 1 + static_cast<size_t>(i) * 2;
        int id = data[q];
        int t = data[q + 1];
        scanComp[i] = -1;
        for (int c = 0; c < nComp; c++)
          if (comp[c].id == id) scanComp[i] = c;
        if (scanComp[i] < 0) return false;
        comp[scanComp[i]].td = t >> 4;
        comp[scanComp[i]].ta = t & 15;
        if (comp[scanComp[i]].td > 3 || comp[scanComp[i]].ta > 3) return false;
      }
      sawSOS = true;
      scanStart = payload + 1 + static_cast<size_t>(nScan) * 2 + 3;
      break;
    }
    pos = end;
  }

  if (!sawSOS || width_ <= 0 || height_ <= 0) return false;

  // component planes, in the sampling grid of the biggest sampler
  int maxH = 1, maxV = 1;
  for (int c = 0; c < nComp; c++) {
    maxH = std::max(maxH, comp[c].h);
    maxV = std::max(maxV, comp[c].v);
    comp[c].blocksWide = (width_ * comp[c].h + maxH * 8 - 1) / (maxH * 8);
    comp[c].blocksHigh = (height_ * comp[c].v + maxV * 8 - 1) / (maxV * 8);
    // (recompute after the true maximum is known)
  }
  maxH = maxV = 1;
  for (int c = 0; c < nComp; c++) {
    maxH = std::max(maxH, comp[c].h);
    maxV = std::max(maxV, comp[c].v);
  }
  for (int c = 0; c < nComp; c++) {
    comp[c].blocksWide = (width_ * comp[c].h + maxH * 8 - 1) / (maxH * 8);
    comp[c].blocksHigh = (height_ * comp[c].v + maxV * 8 - 1) / (maxV * 8);
    comp[c].coefs.assign(static_cast<size_t>(comp[c].blocksWide) * comp[c].blocksHigh * 64, 0);
  }

  // the scan: interleaved MCUs, one block run per component of the scan
  Bits bits(data, scanStart);
  int mcusX = (width_ + maxH * 8 - 1) / (maxH * 8);
  int mcusY = (height_ + maxV * 8 - 1) / (maxV * 8);
  int mcu = 0;
  bool truncated = false;
  for (int cy = 0; cy < mcusY && !bits.eof; cy++) {
    for (int cx = 0; cx < mcusX && !bits.eof; cx++) {
      if (restartInterval && mcu > 0 && mcu % restartInterval == 0) {
        // byte-align and skip the RSTn marker
        bits.have = 0;
        while (bits.pos + 1 < data.size() &&
               !(data[bits.pos] == 0xff && data[bits.pos + 1] >= 0xd0 &&
                 data[bits.pos + 1] <= 0xd7))
          bits.pos++;
        if (bits.pos + 1 < data.size()) bits.pos += 2;
        for (int c = 0; c < nComp; c++) comp[c].dcPred = 0;
      }
      for (int i = 0; i < nScan; i++) {
        Component& cc = comp[scanComp[i]];
        const Huff& dcH = huff[cc.td];
        const Huff& acH = huff[4 | cc.ta];
        if (!dcH.set || !acH.set) return false;
        int blocks = (i == 0) ? cc.h * cc.v : 1;
        // where this component's blocks sit in the MCU (raster of h × v)
        for (int b = 0; b < blocks; b++) {
          int bx = cx * cc.h + (b % cc.h);
          int by = cy * cc.v + (b / cc.h);
          if (bx >= comp[scanComp[i]].blocksWide || by >= comp[scanComp[i]].blocksHigh) {
            // outside the plane: still read the bits to stay in step
            (void)0;
          }
          int16_t blk[64] = {0};
          int s = huffDecode(dcH, bits);
          if (s < 0) {
            if (bits.eof) {
              truncated = true;
              break;
            }
            return false;
          }
          int diff = bits.receiveExtend(s);
          cc.dcPred += diff;
          blk[0] = static_cast<int16_t>(cc.dcPred);
          int k = 1;
          while (k < 64) {
            int r = huffDecode(acH, bits);
            if (r < 0) {
              if (bits.eof) {
                truncated = true;
                break;
              }
              return false;
            }
            if (r == 0x00) break;  // EOB
            int run = r >> 4;
            int cat = r & 15;
            k += run;
            if (k > 63) break;
            if (cat == 0) {
              blk[k] = -1;  // ZRL landed past the end: nothing to write
              k++;
              continue;
            }
            blk[k] = static_cast<int16_t>(bits.receiveExtend(cat));
            k++;
          }
          if (bx < comp[scanComp[i]].blocksWide && by < comp[scanComp[i]].blocksHigh) {
            size_t off = (static_cast<size_t>(by) * comp[scanComp[i]].blocksWide + bx) * 64;
            for (int n = 0; n < 64; n++) {
              int nat = kZigzag[n];  // scan order → natural
              comp[scanComp[i]].coefs[off + static_cast<size_t>(nat)] = blk[n];
            }
          }
        }
      }
      mcu++;
      if (truncated) break;
    }
    if (truncated) break;
  }

  // the planes, upsampled to the picture's size
  std::vector<std::vector<uint8_t>> plane(static_cast<size_t>(nComp));
  for (int c = 0; c < nComp; c++) {
    int pw = comp[c].blocksWide * 8, ph = comp[c].blocksHigh * 8;
    plane[static_cast<size_t>(c)].assign(static_cast<size_t>(pw) * ph, 0);
    for (int by = 0; by < comp[c].blocksHigh; by++) {
      for (int bx = 0; bx < comp[c].blocksWide; bx++) {
        size_t off = (static_cast<size_t>(by) * comp[c].blocksWide + bx) * 64;
        idct8(&comp[c].coefs[off], quant[comp[c].tq],
              &plane[static_cast<size_t>(c)][static_cast<size_t>(by) * 8 * pw + bx * 8], pw);
      }
    }
  }

  out.assign(static_cast<size_t>(width_) * height_ * 4, 0);
  for (int y = 0; y < height_; y++) {
    for (int x = 0; x < width_; x++) {
      double yv = 0, cb = 0, cr = 0;
      for (int c = 0; c < nComp; c++) {
        int pw = comp[c].blocksWide * 8;
        int sx = std::min(pw - 1, x * comp[c].h / maxH);
        int sy = std::min(comp[c].blocksHigh * 8 - 1, y * comp[c].v / maxV);
        double v = plane[static_cast<size_t>(c)][static_cast<size_t>(sy) * pw + sx];
        if (c == 0) yv = v;
        else if (c == 1) cb = v;
        else cr = v;
      }
      if (nComp == 1) cb = cr = 128;
      double r = yv + 1.402 * (cr - 128);
      double g = yv - 0.344136 * (cb - 128) - 0.714136 * (cr - 128);
      double b = yv + 1.772 * (cb - 128);
      size_t o = (static_cast<size_t>(y) * width_ + x) * 4;
      auto clamp8 = [](double v) {
        return static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
      };
      out[o] = clamp8(r);
      out[o + 1] = clamp8(g);
      out[o + 2] = clamp8(b);
      out[o + 3] = 0xff;
    }
  }
  width = width_;
  height = height_;
  return true;
}

}  // namespace fs
