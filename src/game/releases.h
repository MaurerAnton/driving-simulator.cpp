// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/releases.ts @ v1.7.7
/**
 * The versions of the game and what each of them brought – shown in the
 * game as "What's new", in every language (the texts are in src/i18n,
 * under news.<version>.*).  1.0.0 is the first release: its notes say what
 * there is in the game; those of later versions what changed.
 *
 * A new release: put an entry at the top of this list with the keys of its
 * notes, and add the notes to every language (tests/game_releases_test.cpp
 * checks that it all fits).  Its `at` stays '' until the release is
 * published; then the moment of it goes in (gh release view vX.Y.Z --json
 * publishedAt).  Every version in the list can be played again from
 * "What's new" (game/versions.ts).
 *
 * Each entry also says how big the game was then (`size`, counted by the
 * original's tools/release-stats.ts) – until it is counted the newest entry
 * has no `size`.
 *
 * Below 1.0.0 come the development versions (`dev`): the commits the game
 * grew through before its first release, numbered 0.1.0 – 0.9.0 afterwards.
 * They have no tag; `commit` names each (tools/old-versions.ts builds those
 * that can read today's map from it).
 */
#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "game/faultlog.h"

namespace fs {

/** How big the game was at a version: lines of text files, bytes of all files (the map not counted: tools/release-stats.ts). */
struct ReleaseSize {
  /** all lines of all text files */
  double lines = 0;
  /** of them: the code (src/, tools/, server/) */
  double code = 0;
  /** the tests (tests/, server/tests/) */
  double tests = 0;
  /** the translations (src/i18n) */
  double i18n = 0;
  /** the bytes of the game's own files in the repository (the map and the map parts not counted) */
  double bytes = 0;
  /**
   * the bytes of the additional maps in the repository (map-assets – the map itself is a file of a
   * release).  Absent in the releases from before there were any: they count the whole repository.
   */
  std::optional<double> maps = std::nullopt;
};

struct Release {
  std::string version;
  /** the day it was released (ISO) */
  std::string date;
  /**
   * the moment it was published (ISO, as GitHub has it:
   * gh release view vX.Y.Z --json publishedAt) – '' until it is
   */
  std::string at;
  /** a line before the list (i18n key), if there is one */
  std::optional<std::string> intro = std::nullopt;
  /** what it brought: i18n keys, one line each */
  std::vector<std::string> items;
  /** how big the game was then – null until it is counted (the newest, before its release) */
  std::optional<ReleaseSize> size = std::nullopt;
  /** a development version (before 1.0.0, without a tag of its own) */
  bool dev = false;
  /** the commit of a development version */
  std::optional<std::string> commit = std::nullopt;
};

inline const std::vector<Release> RELEASES = {
    {
        .version = "2.1.0",
        .date = "2026-11-26",
        .at = "",
        .intro = "news.2_1_0.intro",
        .items = {"news.2_1_0.walk", "news.2_1_0.weather", "news.2_1_0.transit", "news.2_1_0.roads", "news.2_1_0.traffic",
                  "news.2_1_0.fuel", "news.2_1_0.learn", "news.2_1_0.watch", "news.2_1_0.regions", "news.2_1_0.sdl"},
    },
    {
        .version = "2.0.0",
        .date = "2026-11-24",
        .at = "",
        .intro = "news.2_0_0.intro",
        .items = {"news.2_0_0.regions", "news.2_0_0.map", "news.2_0_0.aerial", "news.2_0_0.later"},
        .size = ReleaseSize{.lines = 298269, .code = 132370, .tests = 29165, .i18n = 85194, .bytes = 30847343},
    },
    {
        .version = "1.7.7",
        .date = "2026-11-23",
        .at = "",
        .items = {"news.1_7_7.regions", "news.1_7_7.archive", "news.1_7_7.controls"},
        .size = ReleaseSize{.lines = 300492, .code = 169557, .tests = 28963, .i18n = 70478, .bytes = 22064866, .maps = 17437101391.0},
    },
    {
        .version = "1.7.6",
        .date = "2026-11-22",
        .at = "",
        .items = {"news.1_7_6.tabs", "news.1_7_6.grass", "news.1_7_6.fixes"},
        .size = ReleaseSize{.lines = 300166, .code = 169416, .tests = 28891, .i18n = 70366, .bytes = 22014920, .maps = 5105667711.0},
    },
    {
        .version = "1.7.5",
        .date = "2026-11-22",
        .at = "",
        .items = {"news.1_7_5.steer", "news.1_7_5.mobile", "news.1_7_5.autopilot", "news.1_7_5.map", "news.1_7_5.windscreen", "news.1_7_5.more"},
        .size = ReleaseSize{.lines = 299418, .code = 169048, .tests = 28801, .i18n = 70114, .bytes = 21957983, .maps = 2835708591.0},
    },
    {
        .version = "1.7.4",
        .date = "2026-11-21",
        .at = "2026-11-21T19:27:22Z",
        .items = {"news.1_7_4.regions", "news.1_7_4.geo"},
        .size = ReleaseSize{.lines = 291468, .code = 164259, .tests = 28692, .i18n = 69806, .bytes = 21866704, .maps = 2835708591.0},
    },
    {
        .version = "1.7.3",
        .date = "2026-11-20",
        .at = "2026-11-20T22:43:21Z",
        .items = {"news.1_7_3.hesse", "news.1_7_3.devmap", "news.1_7_3.baunatal", "news.1_7_3.parts"},
        .size = ReleaseSize{.lines = 286597, .code = 161144, .tests = 28244, .i18n = 69554, .bytes = 21425876, .maps = 2835708591.0},
    },
    {
        .version = "1.7.2",
        .date = "2026-11-19",
        .at = "2026-11-19T12:23:10Z",
        .items = {"news.1_7_2.limiter", "news.1_7_2.drive", "news.1_7_2.turn", "news.1_7_2.bridge", "news.1_7_2.signs"},
        .size = ReleaseSize{.lines = 276212, .code = 155912, .tests = 27790, .i18n = 68826, .bytes = 20784083},
    },
    {
        .version = "1.7.1",
        .date = "2026-11-18",
        .at = "2026-11-18T19:13:54Z",
        .items = {"news.1_7_1.buttons", "news.1_7_1.solid", "news.1_7_1.grip", "news.1_7_1.nomercy"},
        .size = ReleaseSize{.lines = 274615, .code = 155256, .tests = 27407, .i18n = 68602, .bytes = 20688217},
    },
    {
        .version = "1.7.0",
        .date = "2026-11-18",
        .at = "2026-11-18T14:09:01Z",
        .items = {"news.1_7_0.streets", "news.1_7_0.learn", "news.1_7_0.crossings", "news.1_7_0.together", "news.1_7_0.history", "news.1_7_0.green", "news.1_7_0.watch", "news.1_7_0.arcade", "news.1_7_0.foot", "news.1_7_0.camera", "news.1_7_0.voices", "news.1_7_0.lessons", "news.1_7_0.moped", "news.1_7_0.view", "news.1_7_0.more"},
        .size = ReleaseSize{.lines = 274107, .code = 155088, .tests = 27322, .i18n = 68406, .bytes = 20610381},
    },
    {
        .version = "1.6.0",
        .date = "2026-11-17",
        .at = "2026-11-17T19:31:07Z",
        .items = {"news.1_6_0.town", "news.1_6_0.transit", "news.1_6_0.versions", "news.1_6_0.routes", "news.1_6_0.spawn", "news.1_6_0.forklift", "news.1_6_0.camera", "news.1_6_0.pause", "news.1_6_0.faster", "news.1_6_0.more"},
        .size = ReleaseSize{.lines = 203374, .code = 117529, .tests = 19831, .i18n = 47472, .bytes = 14433371},
    },
    {
        .version = "1.5.0",
        .date = "2026-11-17",
        .at = "2026-11-17T12:19:15Z",
        .items = {"news.1_5_0.vehicles", "news.1_5_0.doors", "news.1_5_0.night", "news.1_5_0.weather", "news.1_5_0.traffic", "news.1_5_0.change", "news.1_5_0.watch", "news.1_5_0.layers", "news.1_5_0.search", "news.1_5_0.favourites", "news.1_5_0.autopilot", "news.1_5_0.plane", "news.1_5_0.foot", "news.1_5_0.touch", "news.1_5_0.loading", "news.1_5_0.lights", "news.1_5_0.more"},
        .size = ReleaseSize{.lines = 189544, .code = 110831, .tests = 17412, .i18n = 43969, .bytes = 13582443},
    },
    {
        .version = "1.4.0",
        .date = "2026-11-17",
        .at = "2026-11-17T01:52:42Z",
        .items = {"news.1_4_0.menu", "news.1_4_0.cranes", "news.1_4_0.theory", "news.1_4_0.traffic", "news.1_4_0.weather", "news.1_4_0.arcade", "news.1_4_0.services", "news.1_4_0.signs", "news.1_4_0.plane", "news.1_4_0.maps", "news.1_4_0.transit", "news.1_4_0.cockpit", "news.1_4_0.sounds", "news.1_4_0.autopilot", "news.1_4_0.more"},
        .size = ReleaseSize{.lines = 169889, .code = 100504, .tests = 15781, .i18n = 36303, .bytes = 12176900},
    },
    {
        .version = "1.3.0",
        .date = "2026-11-16",
        .at = "2026-11-16T20:14:29Z",
        .items = {"news.1_3_0.menu", "news.1_3_0.replay", "news.1_3_0.timetables", "news.1_3_0.busLines", "news.1_3_0.pantograph", "news.1_3_0.coupled", "news.1_3_0.figure", "news.1_3_0.models"},
        .size = ReleaseSize{.lines = 104005, .code = 58190, .tests = 7255, .i18n = 21414, .bytes = 8605566},
    },
    {
        .version = "1.2.0",
        .date = "2026-11-16",
        .at = "2026-11-16T17:58:01Z",
        .items = {"news.1_2_0.plane", "news.1_2_0.onFoot", "news.1_2_0.stops", "news.1_2_0.tramLines", "news.1_2_0.busLine", "news.1_2_0.powers", "news.1_2_0.motorcycle", "news.1_2_0.farView", "news.1_2_0.tours", "news.1_2_0.loading", "news.1_2_0.carModel", "news.1_2_0.fixes"},
        .size = ReleaseSize{.lines = 95708, .code = 52650, .tests = 6129, .i18n = 19927, .bytes = 8141406},
    },
    {
        .version = "1.1.0",
        .date = "2026-11-16",
        .at = "2026-11-16T12:26:20Z",
        .items = {"news.1_1_0.tram", "news.1_1_0.classes", "news.1_1_0.graphics", "news.1_1_0.screen", "news.1_1_0.wheels", "news.1_1_0.onFoot"},
        .size = ReleaseSize{.lines = 77236, .code = 38011, .tests = 4879, .i18n = 17422, .bytes = 7180308},
    },
    {
        .version = "1.0.0",
        .date = "2026-11-16",
        .at = "2026-11-16T10:37:55Z",
        .intro = "news.1_0_0.intro",
        .items = {"news.1_0_0.map", "news.1_0_0.rules", "news.1_0_0.instructor", "news.1_0_0.lessons", "news.1_0_0.vehicles", "news.1_0_0.physics", "news.1_0_0.traffic", "news.1_0_0.lights", "news.1_0_0.plan", "news.1_0_0.voice", "news.1_0_0.tours", "news.1_0_0.onFoot", "news.1_0_0.views", "news.1_0_0.start", "news.1_0_0.controls", "news.1_0_0.settings", "news.1_0_0.languages", "news.1_0_0.free"},
        .size = ReleaseSize{.lines = 74044, .code = 36909, .tests = 4727, .i18n = 15508, .bytes = 6945188},
    },
    // the development versions (see above)
    {
        .version = "0.9.0",
        .date = "2026-11-16",
        .at = "2026-11-16T06:07:07+01:00",
        .items = {"news.0_9_0.driver", "news.0_9_0.plan"},
        .size = ReleaseSize{.lines = 67672, .code = 33471, .tests = 3833, .i18n = 13568, .bytes = 6484988},
        .dev = true,
        .commit = "9d378704154fba47b589a103f42b877b5cabdc19",
    },
    {
        .version = "0.8.0",
        .date = "2026-11-16",
        .at = "2026-11-16T04:12:24+01:00",
        .items = {"news.0_8_0.camera", "news.0_8_0.tours", "news.0_8_0.plan", "news.0_8_0.map", "news.0_8_0.lanes", "news.0_8_0.driver", "news.0_8_0.phone"},
        .size = ReleaseSize{.lines = 67631, .code = 33433, .tests = 3830, .i18n = 13568, .bytes = 6482369},
        .dev = true,
        .commit = "3420ad042e1da1fe9bb33d16da323a9c6c60dfa7",
    },
    {
        .version = "0.7.0",
        .date = "2026-11-15",
        .at = "2026-11-15T21:22:11+01:00",
        .items = {"news.0_7_0.kassel", "news.0_7_0.trams", "news.0_7_0.truck", "news.0_7_0.demo", "news.0_7_0.bigmap", "news.0_7_0.camera", "news.0_7_0.driving", "news.0_7_0.cockpit", "news.0_7_0.log", "news.0_7_0.more"},
        .size = ReleaseSize{.lines = 59966, .code = 27572, .tests = 2776, .i18n = 12868, .bytes = 6096113},
        .dev = true,
        .commit = "ff37c5deaf41f155446a6061fbe4287c6e992d31",
    },
    {
        .version = "0.6.0",
        .date = "2026-11-15",
        .at = "2026-11-15T10:43:33+01:00",
        .items = {"news.0_6_0.languages", "news.0_6_0.script", "news.0_6_0.fallback"},
        .size = ReleaseSize{.lines = 48123, .code = 20268, .tests = 1846, .i18n = 9465, .bytes = 4316027},
        .dev = true,
        .commit = "408014bcac92832396f372301a043d8fd6068720",
    },
    {
        .version = "0.5.0",
        .date = "2026-11-15",
        .at = "2026-11-15T09:09:29+01:00",
        .items = {"news.0_5_0.touch", "news.0_5_0.taps", "news.0_5_0.address", "news.0_5_0.notices", "news.0_5_0.keys"},
        .size = ReleaseSize{.lines = 38426, .code = 19954, .tests = 1653, .i18n = 415, .bytes = 3461749},
        .dev = true,
        .commit = "1c08919d4314825386acd4b07fabf406b3d20aeb",
    },
    {
        .version = "0.4.0",
        .date = "2026-11-15",
        .at = "2026-11-15T07:23:09+01:00",
        .items = {"news.0_4_0.reality", "news.0_4_0.own", "news.0_4_0.links"},
        .size = ReleaseSize{.lines = 37037, .code = 18878, .tests = 1422, .i18n = 371, .bytes = 3233331},
        .dev = true,
        .commit = "578e3973591203f0132fbef42d9fadd8b578d5e3",
    },
    {
        .version = "0.3.0",
        .date = "2026-11-15",
        .at = "2026-11-15T02:58:04+01:00",
        .items = {"news.0_3_0.brakes", "news.0_3_0.rules", "news.0_3_0.lanes", "news.0_3_0.mirrors", "news.0_3_0.signs"},
        .size = ReleaseSize{.lines = 33868, .code = 16853, .tests = 1006, .i18n = 336, .bytes = 3008656},
        .dev = true,
        .commit = "633e0cafe1735b1da5cd7270f21739f620fc5431",
    },
    {
        .version = "0.2.0",
        .date = "2026-11-15",
        .at = "2026-11-15T01:26:33+01:00",
        .items = {"news.0_2_0.signs", "news.0_2_0.icon"},
        .size = ReleaseSize{.lines = 33048, .code = 16646, .tests = 854, .i18n = 325, .bytes = 2880347},
        .dev = true,
        .commit = "0e593a7e00463f0648b4b5bbee571bd76a503b5b",
    },
    {
        .version = "0.1.0",
        .date = "2026-11-15",
        .at = "2026-11-15T01:22:00+01:00",
        .intro = "news.0_1_0.intro",
        .items = {"news.0_1_0.map", "news.0_1_0.rules", "news.0_1_0.car", "news.0_1_0.cockpit", "news.0_1_0.instructor", "news.0_1_0.lessons", "news.0_1_0.traffic", "news.0_1_0.controls", "news.0_1_0.free"},
        .size = ReleaseSize{.lines = 32417, .code = 16646, .tests = 854, .i18n = 325, .bytes = 2466845},
        .dev = true,
        .commit = "5b6ddc7d47e47f1db2aeb63fc30b756bad21a5ac",
    },
};

/**
 * The words of news.size for a version, in the number format of `locale`:
 * all lines in full ("203,374"), the parts short ("118K"), the bytes in MB
 * ("14.4 MB") – and the additional maps of the repository, counted apart
 * ("2.4 GB of map parts"), because they are data, not the game.
 * (lines, code, tests, texts, bytes, maps – each with its word: what tr() fills the text with.)
 */
std::vector<std::pair<std::string, std::string>> sizeWords(const ReleaseSize& s, const std::string& locale = "en");

/** the version running now */
inline const std::string VERSION = RELEASES[0].version;

/**
 * Should "What's new" be shown by itself now?  Once after an update, to
 * whoever had played an earlier version (someone who comes for the first
 * time has no use for a list of changes).  `played`: there are settings of
 * an earlier visit.  (`store`: null – without storage: never.)
 */
bool newsDue(bool played, KeyValueStore* store = &plat::localStorage());

}  // namespace fs
