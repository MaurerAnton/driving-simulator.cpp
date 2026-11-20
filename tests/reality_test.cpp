// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The reality view: image metadata, the free services' answers, the player's
 * own pictures, the drawn panorama and the panel's ranking.
 */
#include "../src/reality/imagemeta.h"
#include <cmath>
#include "../src/reality/testpano.h"
#include "../src/ui/reality.h"
#include <cassert>
#include <cstdio>

using namespace fs;

namespace {
class TestHost : public HudHost {
 public:
  HudNode* root = nullptr;
  void setRoot(HudNode* r) override { root = r; }
  void setText(const std::string&, const std::string&) override {}
  void setClass(const std::string&, const std::string&) override {}
  void setVisible(const std::string&, bool) override {}
  void paint(const std::string&, const Canvas2D&) override {}
};

class TestGame : public RealityHost {
 public:
  Settings s;
  Settings& settings() override { return s; }
  void saveSettings() override {}
  void close() override { closed++; }
  bool plausible(double lat, double lon) const override {
    return lat > 51 && lat < 52 && lon > 9 && lon < 10;
  }
  int maxTexture() const override { return 8192; }
  void openAerial() override {}
  int closed = 0;
};

class TestHttp : public HttpBackend {
 public:
  std::string body;
  std::string get(const std::string& url, bool& ok, int& status) override {
    lastUrl = url;
    ok = true;
    status = 200;
    return body;
  }
  std::string lastUrl;
};

/** A 2×2 JPEG-ish file: the metadata reader only needs the first bytes. */
std::vector<uint8_t> fakeJpeg() {
  return {0xff, 0xd8, 0xff, 0xe0, 0x00, 0x10, 'J', 'F', 'I', 'F', 0, 1, 1, 0, 0, 1, 0, 1, 0, 0,
          0xff, 0xc0, 0x00, 0x11, 0x08, 0x01, 0x00, 0x02, 0x00, 0x03, 0x01, 0x11, 0x00,
          0x02, 0x11, 0x01, 0x03, 0x11, 0x01, 0xff, 0xd9};
}
}  // namespace

int main() {
  // --- image metadata ------------------------------------------------------
  {
    ImageMeta m = readImageMeta(fakeJpeg());
    assert(m.kind == "jpeg");
    assert(m.width == 512 && m.height == 256);   // 0x0200 × 0x0100
    assert(std::isnan(m.lat));                   // no GPS in the file
    // a damaged file simply tells less
    ImageMeta bad = readImageMeta({1, 2, 3});
    assert(bad.kind.empty() && bad.width == 0);
  }
  {
    // a panorama without metadata is recognised by its 2:1 shape
    ImageMeta m;
    m.width = 4000;
    m.height = 2000;
    assert(looksLikePanorama(m));
    m.height = 1500;
    assert(!looksLikePanorama(m));
    m.pano = 1;
    assert(looksLikePanorama(m));   // the metadata wins
    m.pano = 0;
    assert(!looksLikePanorama(m));
  }

  // --- the free services ---------------------------------------------------
  {
    SearchArea a{51.2551, 9.4119, 90};
    std::string u = panoramaxUrl(a);
    assert(u.rfind("https://api.panoramax.xyz/api/search?bbox=", 0) == 0);
    assert(u.find("&limit=60") != std::string::npos);
    std::string k = kartaviewUrl(a);
    assert(k.rfind("https://api.kartaview.org/2.0/photo/?lat=51.255100", 0) == 0);
    std::string m = mapillaryUrl(a, "MLY|token");
    assert(m.rfind("https://graph.mapillary.com/images?access_token=MLY%7Ctoken", 0) == 0 ||
           m.find("MLY|token") != std::string::npos);
    assert(m.find("bbox=") != std::string::npos);

    // a Panoramax answer: one feature with an sd asset
    std::string json =
        R"({"features":[{"id":"abc","geometry":{"coordinates":[9.4119,51.2551]},
        "assets":{"sd":{"href":"https://cdn.example/sd.jpg"},"hd":{"href":"https://cdn.example/hd.jpg"}},
        "properties":{"view:azimuth":123.4,"datetime":"2025-08-04T17:05:50",
        "license":"cc-by-sa-4.0","geovisio:producer":"Someone"}}]})";
    auto imgs = parsePanoramax(json);
    assert(imgs.size() == 1);
    assert(imgs[0].source == ImageSource::Panoramax);
    assert(std::fabs(imgs[0].lat - 51.2551) < 1e-6);
    assert(std::fabs(imgs[0].heading - 123.4) < 1e-6);
    assert(imgs[0].url == "https://cdn.example/sd.jpg");
    assert(imgs[0].full == "https://cdn.example/hd.jpg");
    assert(imgs[0].licence == "CC BY-SA 4.0");
    assert(imgs[0].author == "Someone");
    assert(imgs[0].located);

    // a refused request reports what the service said
    assert(serviceError(R"({"message":"slow down"})", 429) == "slow down");
    assert(serviceError("", 500) == "HTTP 500");

    // a KartaView answer
    std::string kv = R"({"result":{"data":[{"id":"k1","lat":51.2,"lng":9.4,
      "imageThUrl":"https://k.example/t.jpg","imageLthUrl":"https://k.example/l.jpg",
      "heading":45,"projection":"sphere","shotDate":"2025-08-04 17:05:50.000",
      "sequenceId":"s1","sequenceIndex":"2"}]}})";
    auto kvi = parseKartaView(kv);
    assert(kvi.size() == 1);
    assert(kvi[0].source == ImageSource::KartaView);
    assert(kvi[0].pano);                     // SPHERE → panorama
    assert(std::isfinite(kvi[0].time));
    assert(kvi[0].page.find("kartaview.org/details/s1/2/track-info") != std::string::npos);

    // a Mapillary answer
    std::string ml = R"({"data":[{"id":"m1","geometry":{"coordinates":[9.4,51.2]},
      "computed_compass_angle":200.5,"is_pano":true,"captured_at":1754317550000,
      "thumb_1024_url":"https://m.example/1024.jpg","thumb_256_url":"https://m.example/256.jpg",
      "creator":{"username":"who"}}]})";
    auto mli = parseMapillary(ml);
    assert(mli.size() == 1);
    assert(mli[0].source == ImageSource::Mapillary);
    assert(mli[0].pano);
    assert(mli[0].author == "who");
    assert(std::fabs(mli[0].heading - 200.5) < 1e-6);

    // place links for the same spot
    auto links = placeLinks(51.2551, 9.4119, 123.4);
    assert(links.size() == 7);
    assert(links[0].label == "KartaView");
    bool hasGoogle = false, hasOsm = false;
    for (const auto& l : links) {
      if (l.label == "Google Street View") hasGoogle = true;
      if (l.label == "OpenStreetMap") hasOsm = true;
      assert(l.url.rfind("https://", 0) == 0);
    }
    assert(hasGoogle && hasOsm);
  }

  // --- the player's own pictures ------------------------------------------
  {
    NameInfo info = parseFileName("C:/pics/51.2553_9.4111_h090.jpg");
    assert(std::fabs(info.lat - 51.2553) < 1e-6);
    assert(std::fabs(info.lon - 9.4111) < 1e-6);
    assert(std::fabs(info.heading - 90) < 1e-6);
    assert(info.id == "51.2553_9.4111_h090");

    // a named heading wins over the trailing one
    NameInfo named = parseFileName("street_heading=45.jpg");
    assert(std::fabs(named.heading - 45) < 1e-6);

    // "<panorama id>_090": one picture per direction
    NameInfo pano = parseFileName("ACf9xamplePano_270.jpg");
    assert(std::fabs(pano.heading - 270) < 1e-6);
    assert(pano.id == "ACf9xamplePano");

    // a side-car list in three shapes
    std::vector<ListEntry> csv = parseList("name,lat,lon\na.jpg,51.2,9.4\nb.jpg,51.3,9.5\n");
    assert(csv.size() == 2);
    assert(csv[0].key == "a");   // keys are stored lower case, without the extension
    assert(std::fabs(csv[0].lat - 51.2) < 1e-9);
    assert(csv.size() == 2);

    std::vector<ListEntry> jsonList = parseList(
        R"([{"file":"c.jpg","lat":51.4,"lon":9.6,"heading":10}])");
    assert(jsonList.size() == 1);
    assert(std::fabs(jsonList[0].lat - 51.4) < 1e-9);

    std::vector<ListEntry> geo = parseList(
        R"({"type":"FeatureCollection","features":[{"type":"Feature",
        "geometry":{"type":"Point","coordinates":[9.7,51.5]},"properties":{"file":"d.jpg"}}]})");
    assert(geo.size() == 1);
    assert(std::fabs(geo[0].lat - 51.5) < 1e-9);
    assert(geo[0].key == "d");

    // findInList matches the file name, or a longer key it contains
    NameInfo none;
    const ListEntry* hit = findInList(csv, "A.JPG", none);   // matched case-insensitively
    assert(hit != nullptr && hit->key == "a");
    assert(findInList(csv, "zzz.jpg", none) == nullptr);

    // describing a picture: the file name places it
    LocalSource src;
    src.name = "51.2553_9.4111.jpg";
    src.meta = readImageMeta(fakeJpeg());
    src.url = "blob:1";
    StreetImage img = describeLocal(src, {}, [](double la, double lo) {
      return la > 51 && la < 52 && lo > 9 && lo < 10;
    });
    assert(img.source == ImageSource::Local);
    assert(img.located);
    assert(std::fabs(img.lat - 51.2553) < 1e-6);
    assert(img.url == "blob:1" && img.thumb == "blob:1");

    // an index of self-hosted pictures
    std::vector<StreetImage> idx = imagesFromIndex(
        R"({"images":[{"file":"panos/a b.jpg","lat":51.2,"lon":9.4,"pano":true,"time":1}]})",
        "https://example.test/panos/");
    assert(idx.size() == 1);
    assert(idx[0].located && idx[0].pano);
    // the original joins baseUrl with the file exactly as written
    assert(idx[0].url == "https://example.test/panos/panos/a%20b.jpg");
    assert(idx[0].title == "a b.jpg");
    // a traversal attempt is refused
    assert(imagesFromIndex(R"({"images":[{"file":"../etc/passwd"}]})", "x/").empty());
    // no matching extension and no position: nothing to show
    assert(imagesFromIndex(R"({"images":[{"file":"note.txt"}]})", "x/").empty());
  }

  // --- the drawn panorama --------------------------------------------------
  {
    TestPanorama p = makeTestPanorama(51.2551, 9.4119, 45);
    assert(p.name == "testpano_51.25510_9.41190_h45.jpg");
    assert(p.jpeg.size() > 1000);
    assert(p.jpeg[0] == 0xff && p.jpeg[1] == 0xd8);   // JPEG SOI
    assert(p.jpeg[p.jpeg.size() - 2] == 0xff && p.jpeg[p.jpeg.size() - 1] == 0xd9);
    // and the metadata reader recognises it as a picture of that size
    ImageMeta m = readImageMeta(p.jpeg);
    assert(m.kind == "jpeg");
    assert(m.width == 2048 && m.height == 1024);
  }

  // --- the panel -----------------------------------------------------------
  {
    TestHost host;
    TestGame game;
    game.s.reality.panoramax = true;
    RealityPanel panel(host, game, nullptr);   // no network in this test
    assert(!panel.isOpen());

    RealityPlace place{51.2551, 9.4119, 123.4, "Carl-Bantzer-Straße"};
    panel.open(place);
    assert(panel.isOpen());
    assert(host.root != nullptr);
    // the panel carries the place, the links and the sources
    std::string all;
    std::function<void(HudNode*)> walk = [&](HudNode* n) {
      if (!n) return;
      all += n->text + " " + n->html + " ";
      for (auto& c : n->children) walk(c.get());
    };
    walk(host.root);
    assert(all.find("Carl-Bantzer") != std::string::npos);
    assert(all.find("Realansicht") != std::string::npos);   // German, as the settings say
    assert(all.find("OpenStreetMap") != std::string::npos);
    assert(all.find("Panoramax") != std::string::npos);

    // an own picture can be imported and shows up as unplaced or placed
    std::vector<std::pair<std::string, std::vector<uint8_t>>> files = {
        {"51.2553_9.4111.jpg", fakeJpeg()}};
    panel.importFiles(files);
    assert(panel.current() != nullptr || !panel.debugInfo().empty());
    assert(panel.debugInfo().find("\"own\":1") != std::string::npos);

    // a service that cannot be asked reports an error instead of crashing
    panel.search(ImageSource::Panoramax);
    std::string info = panel.debugInfo();
    assert(info.find("open") != std::string::npos);

    // closing drops the panel
    panel.close();
    assert(!panel.isOpen());
  }

  std::printf("reality: all checks passed\n");
  return 0;
}
