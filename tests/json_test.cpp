// SPDX-License-Identifier: AGPL-3.0-only
// LINK: world core platform geo
/**
 * JSON read and written as JavaScript does it (src/platform/json.h).  The
 * numbers, texts and orders expected here were printed by the original's
 * engine (node: JSON.parse, JSON.stringify, Object.keys).
 */
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "platform/json.h"
#include "world/types.h"
#include "world/worldjson.h"

using namespace fs;

namespace {

uint64_t bits(double v) {
  uint64_t b;
  std::memcpy(&b, &v, 8);
  return b;
}

struct Inner {
  double a = 7;
  std::optional<int> b;
  std::string c = "dflt";
};
FS_JSON_STRUCT(Inner, a, b, c)

struct Outer {
  int n = -1;
  std::vector<Inner> list;
  std::pair<int, std::string> pair;
  std::tuple<std::string, double, int> triple;
  std::array<double, 3> xyz{};
  std::optional<Inner> maybe;
  std::shared_ptr<const Inner> shared;
  bool flag = false;
  std::vector<std::vector<int>> grid;
};
FS_JSON_STRUCT(Outer, n, list, pair, triple, xyz, maybe, shared, flag, grid)

}  // namespace

int main() {
  // --- numbers: to the nearest double, as JSON.parse reads them (the bits as node has them)
  {
    const struct {
      const char* text;
      uint64_t bits;
    } cases[] = {
        {"0", 0x0000000000000000ull},
        {"-0", 0x8000000000000000ull},
        {"1", 0x3ff0000000000000ull},
        {"-1", 0xbff0000000000000ull},
        {"6828.75", 0x40baacc000000000ull},
        {"-0.0339", 0xbfa15b573eab367aull},
        {"152.02", 0x406300a3d70a3d71ull},
        {"0.1", 0x3fb999999999999aull},
        {"0.2", 0x3fc999999999999aull},
        {"0.3", 0x3fd3333333333333ull},
        {"1e21", 0x444b1ae4d6e2ef50ull},
        {"1e-7", 0x3e7ad7f29abcaf48ull},
        {"123456789012345678", 0x437b69b4ba630f35ull},
        {"0.000001", 0x3eb0c6f7a0b5ed8dull},
        {"9007199254740993", 0x4340000000000000ull},
        {"1.7976931348623157e308", 0x7fefffffffffffffull},
        {"1e999", 0x7ff0000000000000ull},
        {"-1e999", 0xfff0000000000000ull},
        {"5e-324", 0x0000000000000001ull},
        {"2.2250738585072014e-308", 0x0010000000000000ull},
        {"4.9406564584124654e-324", 0x0000000000000001ull},
        {"0.30000000000000004", 0x3fd3333333333334ull},
        {"123456.789e3", 0x419d6f3454000000ull},
        {"1E5", 0x40f86a0000000000ull},
        {"1e+5", 0x40f86a0000000000ull},
        {"-2.5e-3", 0xbf647ae147ae147bull},
        {"8.5", 0x4021000000000000ull},
        {"1234567.125", 0x4132d68720000000ull},
        {"0.1234567890123456789", 0x3fbf9add3746f65full},
        {"100000000000000000000", 0x4415af1d78b58c40ull},
        {"17976931348623157e292", 0x7fefffffffffffffull},
        {"3.141592653589793", 0x400921fb54442d18ull},
        {"9.999999999999999e22", 0x44b52d02c7e14af6ull},
        {"1e23", 0x44b52d02c7e14af6ull},
        {"0.049999999999999996", 0x3fa9999999999999ull},
        {"2.4703282292062328e-324", 0x0000000000000001ull},
        {"4503599627370496.5", 0x4330000000000000ull},
        {"9007199254740992", 0x4340000000000000ull},
        {"9007199254740991", 0x433fffffffffffffull},
        {"0.000000000000000000000000000000001", 0x3914c4e977ba1f5cull},
        {"1.0000000000000002", 0x3ff0000000000001ull},
        {"72057594037927945", 0x4370000000000001ull},
        {"0.9999999999999999", 0x3fefffffffffffffull},
        {"6.02214076e23", 0x44dfe185ca57c517ull},
        {"1e22", 0x4480f0cf064dd592ull},
        {"1e-22", 0x3b5e392010175ee6ull},
        {"12345678901234567890123", 0x4484ea15b273b38aull},
        {"-7684.16", 0xc0be0428f5c28f5cull},
        {"25765021", 0x41789249d0000000ull},
        {"12660148189", 0x420794d43ee80000ull},
    };
    for (const auto& c : cases) {
      JsonReader r(c.text);
      double v = 99;
      const bool read = r.number(v);
      if (!(read && r.ok() && r.atEnd() && bits(v) == c.bits)) {
        std::fprintf(stderr, "number %s: read %d, bits %016llx, expected %016llx\n", c.text, read, static_cast<unsigned long long>(bits(v)), static_cast<unsigned long long>(c.bits));
        assert(false);
      }
      // … and the same inside a document
      Json doc;
      assert(Json::parse(std::string("[") + c.text + "]", doc) && doc.array.size() == 1 && bits(doc.array[0].number) == c.bits);
    }
  }

  // --- what is not JSON is not read (each of these throws in JSON.parse)
  for (const char* bad : {"", "{", "[1,]", "{\"a\":1,}", "01", "+1", ".5", "1.", "nul", "'a'", "{\"a\" 1}", "[1 2]", "\"\\x\"", "\"a\nb\"", "1 2", "NaN", "[1]]", "\"\\u12\"", "{1:2}", "-", "1e", "tru", "\xEF\xBB\xBF" "1"}) {
    Json doc;
    if (Json::parse(bad, doc)) {
      std::fprintf(stderr, "read as JSON: %s\n", bad);
      assert(false);
    }
  }
  {
    // (nested deeper than anything real: turned away, not followed down the stack)
    std::string deep(100000, '[');
    Json doc;
    assert(!Json::parse(deep, doc));
  }

  // --- written as JSON.stringify writes
  {
    Json list = Json::makeArray();
    for (double v : {0.0, -0.0, 1.0, -1.0, 0.1, 1e21, 1e-7, 123456789012345680000.0, 5e-324, 1.5, -2.5e-8, 1.0 / 3, 100.0, 1e300, 123.456, 0.000001234, std::nan(""), HUGE_VAL}) list.push(v);
    assert(list.stringify() == "[0,0,1,-1,0.1,1e+21,1e-7,123456789012345680000,5e-324,1.5,-2.5e-8,0.3333333333333333,100,1e+300,123.456,0.000001234,null,null]");

    // an object keeps its members in JavaScript's order: whole numbers first (rising), then as they came
    Json o = Json::makeObject();
    o.set("b", 1);
    Json a = Json::makeArray();
    a.push(true);
    a.push(false);
    a.push(nullptr);
    a.push("x\ny\"z\\ \x01 \xC3\xA4\xE2\x82\xAC \xF0\x9F\x98\x80 \xE2\x80\xA8");
    o.set("a", std::move(a));
    o.set("2", "two");
    o.set("1", "one");
    o.set("01", "x");
    Json c = Json::makeObject();
    c.set("d", Json::makeArray());
    c.set("e", Json::makeObject());
    o.set("c", std::move(c));
    o.set("-1", 0);
    o.set("4294967295", 1);
    o.set("4294967294", 2);
    const std::string expected =
        "{\"1\":\"one\",\"2\":\"two\",\"4294967294\":2,\"b\":1,\"a\":[true,false,null,\"x\\ny\\\"z\\\\ \\u0001 \xC3\xA4\xE2\x82\xAC \xF0\x9F\x98\x80 \xE2\x80\xA8\"],\"01\":\"x\",\"c\":{\"d\":[],\"e\":{}},\"-1\":0,\"4294967295\":1}";
    assert(o.stringify() == expected);
    // … and read back it is written the same again
    Json back;
    assert(Json::parse(expected, back));
    assert(back.stringify() == expected);
    assert(back.at("a").array[3].str == "x\ny\"z\\ \x01 \xC3\xA4\xE2\x82\xAC \xF0\x9F\x98\x80 \xE2\x80\xA8");
    assert(back.at("c").at("d").isArray() && back.at("c").at("e").isObject() && back.at("nothing").isNull() && !back.has("nothing") && back.has("01"));
    assert(back.at("b").asNum() == 1 && back.at("b").asStr() == "1" && back.at("2").asStr() == "two" && std::isnan(back.at("c").asNum()));
  }
  {
    // escapes (two \u halves make one character), a name given twice (the later value in the earlier place)
    Json doc;
    assert(Json::parse("{\"x\":\"\\ud83d\\ude00 \\u00e4 \\/ \\b\\f\\n\\r\\t\",\"dup\":1,\"dup\":2}", doc));
    assert(doc.stringify() == "{\"x\":\"\xF0\x9F\x98\x80 \xC3\xA4 / \\b\\f\\n\\r\\t\",\"dup\":2}");
    Json keys;
    assert(Json::parse("{\"b\":1,\"10\":2,\"9\":3,\"a\":4,\"1.5\":5,\"-0\":6,\"0\":7}", keys));
    std::string order;
    for (const auto& kv : keys.object) order += kv.first + ",";
    assert(order == "0,9,10,b,a,1.5,-0,");
    // many members: found by their names, still in their order
    Json many = Json::makeObject();
    for (int i = 0; i < 500; i++) many.set("k" + std::to_string(i), i);
    for (int i = 499; i >= 0; i--) assert(many.at("k" + std::to_string(i)).number == i);
    assert(many.object.size() == 500 && many.object.begin()->first == "k0" && !many.has("k500"));
    assert(many.object.erase("k7") && !many.has("k7") && many.at("k8").number == 8 && many.object.size() == 499);
  }

  // --- structs read straight from the text
  {
    const std::string text =
        " {\"n\": 3, \"extra\": {\"deep\": [1, {\"x\": null}]}, \"list\": [{\"a\": 1.5, \"b\": 2}, {\"c\": \"z\", \"b\": null}, 5],\n"
        "  \"pair\": [9, \"nine\"], \"triple\": [\"t\", 2.5, -4], \"xyz\": [1, 2, 3, 4], \"maybe\": {\"a\": 4}, \"shared\": {\"c\": \"s\"},\n"
        "  \"flag\": true, \"grid\": [[1, 2], [], [3]], \"n2\": 8 } ";
    Outer o;
    assert(jsonParse(text, o));
    assert(o.n == 3 && o.list.size() == 3);
    assert(o.list[0].a == 1.5 && o.list[0].b == 2 && o.list[0].c == "dflt");
    assert(o.list[1].a == 7 && !o.list[1].b && o.list[1].c == "z");   // (null leaves what it was)
    assert(o.list[2].a == 7 && o.list[2].c == "dflt");                 // (a number where an object should be: passed over)
    assert(o.pair.first == 9 && o.pair.second == "nine");
    assert(std::get<0>(o.triple) == "t" && std::get<1>(o.triple) == 2.5 && std::get<2>(o.triple) == -4);
    assert(o.xyz[0] == 1 && o.xyz[1] == 2 && o.xyz[2] == 3);
    assert(o.maybe && o.maybe->a == 4 && o.shared && o.shared->c == "s" && o.flag);
    assert(o.grid.size() == 3 && o.grid[0].size() == 2 && o.grid[1].empty() && o.grid[2][0] == 3);
    // written and read again: the same
    const std::string written = jsonText(o);
    Outer again;
    assert(jsonParse(written, again));
    assert(jsonText(again) == written);
    assert(written.find("\"b\":2") != std::string::npos && written.find("\"maybe\":{\"a\":4,\"c\":\"dflt\"}") != std::string::npos);
    // broken text: said so
    Outer broken;
    assert(!jsonParse("{\"n\": 3, \"list\": [}", broken));
    assert(!jsonParse("{\"n\": 3} x", broken));
  }

  // --- the records of the map: a chunk as the map build writes it
  {
    const std::string text =
        "{\"v\":1,\"nodes\":[[155,{\"x\":6828.75,\"y\":152.02,\"z\":-7684.16,\"t\":2,\"arms\":[{\"e\":73695,\"r\":1,\"sl\":10.06},{\"e\":115,\"r\":5,\"g\":1}],\"o\":25765021,\"gx\":-0.0339,\"gz\":0.0016,\"fr\":[4.81,0],\"sc\":10}]],"
        "\"edges\":[[115,{\"a\":155,\"b\":156,\"c\":11,\"p\":[],\"lf\":1,\"lb\":1,\"lw\":2.75,\"w\":5.5,\"mf\":30,\"mb\":30,\"f\":320,\"ca\":4.2,\"cb\":0,\"o\":12660148189,\"q\":[682875,15202,-768416,4061,-147,1300],"
        "\"n\":\"K\\u00f6nigsstra\\u00dfe\",\"sl\":2,\"z\":30,\"x\":[{\"s\":12.5,\"k\":2,\"w\":4,\"isl\":true,\"sc\":3,\"g\":0},{\"s\":30,\"k\":1,\"w\":4}],\"bn\":[155,73695],"
        "\"xl\":{\"p\":2,\"k\":1,\"pw\":2.3},\"cm\":[20,1,4,0.08],\"bo\":[10,1,5,0,2],\"mk\":1,\"gl\":0,\"rt\":[[\"BUS\",12.5,-1]],\"zs\":[1,2,3]}]],"
        "\"signals\":[[10,{\"x\":1,\"z\":2,\"groups\":2,\"phases\":[{\"d\":28,\"s\":\"GR\"},{\"d\":3,\"s\":\"AR\"}],\"off\":7,\"ped\":true}]]}";
    WorldChunkJson chunk;
    assert(parseWorldChunkJson(text, chunk));
    assert(chunk.v == 1 && chunk.nodes.size() == 1 && chunk.edges.size() == 1 && chunk.signals.size() == 1);
    const NodeJson& n = chunk.nodes[0].second;
    assert(chunk.nodes[0].first == 155 && n.x == 6828.75 && n.y == 152.02 && n.z == -7684.16 && n.t == 2 && n.o == 25765021 && n.gx == -0.0339 && n.gz == 0.0016 && n.sc == 10 && n.tc == 0);
    assert(n.arms.size() == 2 && n.arms[0].e == 73695 && n.arms[0].r == 1 && n.arms[0].sl == 10.06 && n.arms[0].g == -1 && n.arms[1].g == 1 && n.arms[1].sl == 0);
    assert(n.fr.size() == 2 && n.fr[0] == 4.81);
    const EdgeJson& e = chunk.edges[0].second;
    assert(chunk.edges[0].first == 115 && e.a == 155 && e.b == 156 && e.c == 11 && e.p.empty() && e.q && e.q->size() == 6 && (*e.q)[2] == -768416);
    assert(e.lf == 1 && e.lb == 1 && e.lw == 2.75 && e.w == 5.5 && e.mf == 30 && e.f == 320 && e.ca == 4.2 && e.cb == 0 && e.o == 12660148189ll && e.sl == 2 && e.sr == 0 && e.z == 30 && e.mg == 0);
    assert(e.n == "K\xC3\xB6nigsstra\xC3\x9F" "e" && e.r.empty());
    assert(e.x.size() == 2 && e.x[0].s == 12.5 && e.x[0].k == 2 && e.x[0].isl && e.x[0].sc == 3 && e.x[0].g == 0 && !e.x[1].isl && !e.x[1].sc && !e.x[1].g);
    assert(e.bn.size() == 2 && e.bn[1] == 73695);
    assert(e.xl && e.xl->p == 2 && e.xl->k == 1 && e.xl->pw == 2.3 && !e.xl->c && !e.xr);
    assert(e.cm && e.cm->size() == 4 && e.bo && e.bo->size() == 5 && e.mk == 1 && e.gl && *e.gl == 0 && e.zz == 0);
    assert(e.rt.size() == 1 && std::get<0>(e.rt[0]) == "BUS" && std::get<1>(e.rt[0]) == 12.5 && std::get<2>(e.rt[0]) == -1 && e.zs.size() == 3);
    const SignalControllerJson& sc = chunk.signals[0].second;
    assert(chunk.signals[0].first == 10 && sc.groups == 2 && sc.phases.size() == 2 && sc.phases[1].s == "AR" && sc.phases[0].d == 28 && sc.off == 7 && sc.ped);
    // written and read again
    WorldChunkJson again;
    assert(parseWorldChunkJson(jsonText(chunk), again));
    assert(jsonText(again) == jsonText(chunk));
  }

  std::printf("json: all checks passed\n");
  return 0;
}
