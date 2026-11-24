// SPDX-License-Identifier: AGPL-3.0-only
// LINK: world core platform geo
// Origin: tests/world/tilefmt.test.ts @ v1.7.7
/**
 * The container of the map tiles (src/world/tilefmt.h): every coding of a
 * section, the sections that follow from others, the set of squares.  Beside
 * what the original's test asks, the bytes written here are those the original
 * writes (their length and a sum over them, printed by the original under node).
 */
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "world/grid.h"
#include "world/tilefmt.h"
#include "world/worldjson.h"

using namespace fs;

namespace {

/** numbers that look like a mesh: neighbours are close, now and then a jump, values on both sides of zero */
std::vector<double> wavy(int n, int64_t range, uint32_t seed) {
  std::vector<double> out;
  int64_t v = 0;
  uint32_t a = seed;
  for (int i = 0; i < n; i++) {
    a = static_cast<uint32_t>(static_cast<uint64_t>(a) * 1664525u + 1013904223u);
    v += static_cast<int64_t>(a >> 24) - 127;
    if (a % 97 == 0) v = (a >> 8) % range;
    out.push_back(static_cast<double>(((v % range) + range) % range));
  }
  return out;
}

template <class T>
std::vector<T> from(const std::vector<double>& v, double add = 0) {
  std::vector<T> out;
  for (double x : v) out.push_back(static_cast<T>(static_cast<int64_t>(x + add)));
  return out;
}

double sum(const std::vector<uint8_t>& b) {
  double s = 0;
  for (size_t i = 0; i < b.size(); i++) s += b[i] * static_cast<double>((i % 251) + 1);
  return s;
}
double sum(const SectionArray& a) {
  double s = 0;
  for (size_t i = 0; i < a.size(); i++) s += a.at(i) * static_cast<double>((i % 251) + 1);
  return s;
}

bool sameNumbers(const SectionArray& a, const SectionArray& b) {
  if (a.type != b.type || a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); i++)
    if (a.at(i) != b.at(i)) return false;
  return true;
}

std::string headerOf(const std::vector<uint8_t>& packed) {
  const size_t n = packed[4] | (packed[5] << 8) | (packed[6] << 16) | (static_cast<size_t>(packed[7]) << 24);
  return std::string(packed.begin() + 8, packed.begin() + 8 + static_cast<long>(n));
}

}  // namespace

int main() {
  // --- every coding of a section gives the numbers back as they were
  {
    struct Case {
      const char* what;
      SectionArray arr;
      int stride;
      // what the original has: a sum over the numbers; per coding (RAW, PLANES, DELTA) the length of the container and a sum over its bytes
      double numbers;
      double packed[3][2];
    };
    const Case cases[] = {
        {"i16 x 3", SectionArray(from<int16_t>(wavy(3 * 400, 60000, 1), -30000)), 3, -201496022, {{2464, 37726258}, {2476, 38088317}, {2476, 19105703}}},
        {"u16 x 3", SectionArray(from<uint16_t>(wavy(3 * 333, 65536, 2))), 3, 5872875826, {{2060, 39129919}, {2072, 39825776}, {2072, 14275243}}},
        {"u32 x 3", SectionArray(from<uint32_t>(wavy(3 * 250, 200000, 3))), 3, 6401505957, {{3060, 21826659}, {3072, 23452472}, {3072, 11262458}}},
        {"i16 x 4", SectionArray(from<int16_t>(wavy(4 * 100, 65536, 4), -32768)), 4, -366754663, {{860, 16330660}, {872, 17111876}, {872, 6018262}}},
        {"u8 x 1", SectionArray(from<uint8_t>(wavy(500, 256, 5))), 1, 8042893, {{560, 8065502}, {572, 8060470}, {572, 8014695}}},
        {"extremes", SectionArray(std::vector<int16_t>{-32768, 32767, -32768, 0, 32767, -1, 1, 0, 0}), 3, 98298, {{80, 229737}, {92, 298961}, {92, 420197}}},
        {"u32 extremes", SectionArray(std::vector<uint32_t>{0, 4294967295u, 1, 4294967295u, 0, 2147483648u}), 3, 38654705661, {{84, 281157}, {96, 348745}, {96, 267476}}},
    };
    Json header = Json::makeObject();
    header.set("v", 1);
    for (const Case& c : cases) {
      assert(sum(c.arr) == c.numbers);
      int k = 0;
      for (int coding : {CODING_RAW, CODING_PLANES, CODING_DELTA}) {
        const std::vector<uint8_t> packed = packSections(header, {{"a", encodeSection(c.arr, coding, c.stride)}});
        if (!(static_cast<double>(packed.size()) == c.packed[k][0] && sum(packed) == c.packed[k][1])) {
          std::fprintf(stderr, "%s, coding %d: %zu bytes with the sum %.0f; the original writes %.0f with %.0f\n", c.what, coding, packed.size(), sum(packed), c.packed[k][0], c.packed[k][1]);
          assert(false);
        }
        UnpackedSections u = unpackSections(packed);
        const SectionArray* back = u.get("a");
        assert(back);
        assert(back->type == c.arr.type);       // the same kind of array
        assert(sameNumbers(*back, c.arr));
        assert(u.get("a") == back);             // made once
        k++;
      }
    }
    // a grid of heights: rows of 7
    const SectionArray grid(from<uint16_t>(wavy(7 * 9, 65536, 6)));
    assert(sum(grid) == 76857921);
    const std::vector<uint8_t> packed = packSections(header, {{"hgt", encodeSection(grid, CODING_GRID, 7)}, {"other", SectionArray(std::vector<float>{1.5f, -2.25f})}});
    assert(packed.size() == 248 && sum(packed) == 1621964);
    assert(headerOf(packed) == "{\"v\":1,\"sections\":{\"hgt\":{\"off\":0,\"n\":63,\"t\":\"u16\",\"c\":3,\"s\":7},\"other\":{\"off\":128,\"n\":2,\"t\":\"f32\"}}}   ");
    UnpackedSections u = unpackSections(packed);
    assert(sameNumbers(*u.get("hgt"), grid));
    const SectionArray* other = u.get("other");
    assert(other && other->type == SectionType::F32 && other->f32 == (std::vector<float>{1.5f, -2.25f}));
    assert(u.get("nothing") == nullptr);
    // (the numbers can be taken out of the array – the renderer hands them to the graphics card as they are)
    std::vector<float> moved = std::move(u.get("other")->f32);
    assert(moved.size() == 2 && u.get("other")->f32.empty());
    // what is no container is not read
    bool threw = false;
    try {
      unpackSections(std::vector<uint8_t>{'F', 'S', 'B', '2', 0, 0, 0, 0});
    } catch (const std::exception& e) {
      threw = std::string(e.what()) == "not a map tile (bad magic)";
    }
    assert(threw);
  }

  // --- trees and outlines stored as whole numbers come back as the game expects them
  {
    // a tile whose ground rises by 10 m from west to east and by 5 m from north to south (heights in steps of 4 cm)
    const int N = 3;
    std::vector<uint16_t> hgt(N * N);
    for (int j = 0; j < N; j++)
      for (int i = 0; i < N; i++) hgt[static_cast<size_t>(j * N + i)] = static_cast<uint16_t>(jsRound((i * 5 + j * 2.5) / 0.04));
    TileHeader header;
    header.v = 2;
    header.ix = 10;
    header.iz = 20;
    header.cx = 1250;
    header.cz = -750;
    header.yBase = 200;
    header.size = 500;
    header.terrainN = N;
    header.terrainMin = 190;
    header.terrainScale = 0.04;
    header.coverN = 1;
    auto ground = [](double x, double z) { return 190 + ((x - 1000) / 250) * 5 + ((z + 1000) / 250) * 2.5; };
    // x, z, height, kind
    const std::vector<double> stand = {1250 + 12.3, -750 - 100, 14.2, 1, 1250 - 249.9, -750 + 249.9, 6, 3, 1000, -1000, 21.8, 0};
    const size_t n = stand.size() / 4;
    std::vector<int16_t> xz(n * 2);
    std::vector<uint8_t> hk(n * 2);
    for (size_t i = 0; i < n; i++) {
      xz[i * 2] = static_cast<int16_t>(jsRound((stand[i * 4] - header.cx) / TREE_SCALE));
      xz[i * 2 + 1] = static_cast<int16_t>(jsRound((stand[i * 4 + 1] - header.cz) / TREE_SCALE));
      hk[i] = static_cast<uint8_t>(jsRound(stand[i * 4 + 2] / TREE_HEIGHT_SCALE));
      hk[n + i] = static_cast<uint8_t>(stand[i * 4 + 3]);
    }
    assert((xz == std::vector<int16_t>{123, -1000, -2499, 2499, -2500, -2500}) && (hk == std::vector<uint8_t>{71, 30, 109, 1, 3, 0}) && (hgt == std::vector<uint16_t>{0, 125, 250, 63, 188, 313, 125, 250, 375}));
    const std::vector<double> ring = {1250 + 300.5, -750 - 12.25, 1250 - 40, -750 + 8};
    std::vector<int16_t> collQ;
    for (size_t i = 0; i < ring.size(); i++) collQ.push_back(static_cast<int16_t>(jsRound((ring[i] - (i % 2 ? header.cz : header.cx)) / RING_SCALE)));
    const std::vector<uint8_t> packed = packTile(header, {
                                                             {"hgt", encodeSection(SectionArray(hgt), CODING_GRID, N)},
                                                             {"treeXZ", encodeSection(SectionArray(xz), CODING_DELTA, 2)},
                                                             {"treeHK", SectionArray(hk)},
                                                             {"collQ", encodeSection(SectionArray(collQ), CODING_PLANES, 2)},
                                                             {"collCnt", SectionArray(std::vector<uint16_t>{2})},
                                                         });
    // (the very bytes the original writes for this tile)
    assert(headerOf(packed) ==
           "{\"v\":2,\"ix\":10,\"iz\":20,\"cx\":1250,\"cz\":-750,\"yBase\":200,\"size\":500,\"terrainN\":3,\"terrainMin\":190,\"terrainScale\":0.04,\"coverN\":1,\"signs\":[],\"objects\":[],\"sections\":{\"hgt\":{\"off\":0,\"n\":9,\"t\":\"u16\",\"c\":3,\"s\":3},\"treeXZ\":{\"off\":20,\"n\":6,\"t\":\"i16\",\"c\":2,\"s\":2},\"treeHK\":{\"off\":32,\"n\":6,\"t\":\"u8\"},\"collQ\":{\"off\":40,\"n\":4,\"t\":\"i16\",\"c\":1,\"s\":2},\"collCnt\":{\"off\":48,\"n\":1,\"t\":\"u16\"}}} ");
    assert(packed.size() == 436 && sum(packed) == 3304933);
    UnpackedTile tile = unpackTile(packed);
    assert(tile.header.v == 2 && tile.header.ix == 10 && tile.header.iz == 20 && tile.header.cx == 1250 && tile.header.terrainScale == 0.04 && !tile.header.clr && tile.header.sections.size() == 5 &&
           tile.header.signs.empty());
    const SectionArray* back = tile.get("trees");
    assert(back && back->type == SectionType::F32 && back->size() == n * 5);
    for (size_t i = 0; i < n; i++) {
      const double x = stand[i * 4], z = stand[i * 4 + 1], tall = stand[i * 4 + 2], kind = stand[i * 4 + 3];
      assert(std::fabs(back->at(i * 5) - x) < 0.001 && std::fabs(back->at(i * 5 + 2) - z) < 0.001);  // the tree stands where it was put
      // (it stands on the tile's ground: a plane here, so the height is that of the plane to the step of the grid)
      assert(std::fabs(back->at(i * 5 + 1) - ground(x, z)) < 0.03);
      assert(std::fabs(back->at(i * 5 + 3) - tall) <= 0.11);
      assert(back->at(i * 5 + 4) == kind);
    }
    // … to the last digit what the original makes of them
    assert((back->f32 == std::vector<float>{1262.300048828125f, 196.75799560546875f, -850, 14.199999809265137f, 1, 1000.0999755859375f, 195.00100708007812f, -500.1000061035156f, 6, 3, 1000, 190, -1000, 21.799999237060547f, 0}));
    assert(std::fabs(tileGround(header, hgt, 1125, -875) - ground(1125, -875)) < 0.03);  // the ground between the points of the grid
    assert(tileGround(header, hgt, 1125, -875) == 193.76 && tileGround(header, hgt, 1000, -1000) == 190 && tileGround(header, hgt, 1499.99, -500.01) == 204.999252 && tileGround(header, hgt, 5000, 5000) == 204.999252 &&
           tileGround(header, hgt, 1300, -700.5) == 199.01104);
    const SectionArray* pts = tile.get("collPts");
    assert(pts && pts->size() == ring.size());
    for (size_t i = 0; i < ring.size(); i++) assert(std::fabs(pts->at(i) - ring[i]) <= 0.011);
    assert((pts->f32 == std::vector<float>{1550.5f, -762.239990234375f, 1210, -742}));
    assert(tile.get("trees") == back);  // made once
    assert(tile.get("bldg_a16") == nullptr && tile.get("nothing") == nullptr && tile.get("collCnt")->u16 == std::vector<uint16_t>{2});

    // the first tiles of this kind: x, y, z per tree in steps of 2 cm
    const std::vector<double> old = {1250 + 12.34, 200 + 3.5, -750 - 100.02, 14.2, 1, 1250 - 249.98, 200 - 20, -750 + 249.9, 6, 3};
    std::vector<int16_t> q(6);
    std::vector<uint8_t> hk2(4);
    for (size_t i = 0; i < 2; i++) {
      q[i * 3] = static_cast<int16_t>(jsRound((old[i * 5] - header.cx) / 0.02));
      q[i * 3 + 1] = static_cast<int16_t>(jsRound((old[i * 5 + 1] - header.yBase) / 0.02));
      q[i * 3 + 2] = static_cast<int16_t>(jsRound((old[i * 5 + 2] - header.cz) / 0.02));
      hk2[i] = static_cast<uint8_t>(jsRound(old[i * 5 + 3] / TREE_HEIGHT_SCALE));
      hk2[2 + i] = static_cast<uint8_t>(old[i * 5 + 4]);
    }
    UnpackedTile oldTile = unpackTile(packTile(header, {{"treeQ", encodeSection(SectionArray(q), CODING_DELTA, 3)}, {"treeHK", SectionArray(hk2)}}));
    const SectionArray* before = oldTile.get("trees");
    assert(before && before->size() == old.size());
    for (size_t i = 0; i < old.size(); i++) assert(std::fabs(before->at(i) - old[i]) <= (i % 5 == 3 ? 0.11 : 0.011));
  }

  // --- the set of squares that exist survives being written down
  {
    const std::vector<std::pair<int, int>> squares = {{400, 380}, {401, 380}, {403, 395}, {399, 381}};
    const GridSet set = GridSet::of(squares);
    assert(set.count() == 4);
    const std::string text = jsonText(set.toJSON());
    assert(text == "{\"x0\":399,\"z0\":380,\"w\":5,\"h\":16,\"bits\":\"JgAAAAAAAAAAgA==\"}");
    GridBits bits;
    assert(jsonParse(text, bits));
    const GridSet again = GridSet::from(bits);
    for (const auto& [x, z] : squares) assert(again.has(x, z));
    assert(!again.has(402, 380));
    assert(!again.has(0, 0));
    assert(again.count() == 4);
    assert(GridSet::of({}).count() == 0 && !GridSet::of({}).has(0, 0));
    assert(gridIndex(0, TILE) == 400 && gridIndex(-0.01, TILE) == 399 && gridStart(400, TILE) == 0 && gridIndex(6500, CHUNK) == 51 && tileIndex(-0.5, -200, 500) == 0 && tileKey(3, -4) == "3_-4");
  }

  std::printf("tilefmt: all checks passed\n");
  return 0;
}
