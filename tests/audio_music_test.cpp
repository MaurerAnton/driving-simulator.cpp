// SPDX-License-Identifier: AGPL-3.0-only
// LINK: audio platform core
/**
 * The music made up while driving (mirror of the original's tests/game/music.test.ts): every piece in
 * its key and mode, its tune leaning on the notes of its chords, of a length to listen to, varied from
 * piece to piece, quiet against the vehicles – and played on the audio clock a moment ahead, with pauses
 * of minutes between pieces, nothing at all while it is off.
 *
 * And beyond the original's tests: the pieces are the original's, note for note and bit for bit (thirty
 * seeds, made up with node from v1.7.7), and the player makes the same nodes and sends the same settings
 * at the same moments as the original's does on its stand-in for the Web Audio API.
 */
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "audio/compose.h"
#include "audio/music.h"
#include "audio/profiles.h"
#include "audio/timers.h"
#include "core/random.h"
#include "platform/audio.h"
#include "platform/native/audio_native.h"
#include "platform/native/native.h"
#include "platform/plat.h"

using namespace fs;

namespace {

void check(bool ok, const std::string& what) {
  if (ok) return;
  std::fprintf(stderr, "audio_music_test: %s\n", what.c_str());
  std::abort();
}

std::vector<Piece> makePieces() {
  std::vector<Piece> out;
  for (uint32_t i = 0; i < 24; i++) out.push_back(composePiece((i + 1) * 104729u));
  return out;
}
const std::vector<Piece>& pieces() {
  static const std::vector<Piece> all = makePieces();
  return all;
}

std::vector<MusicNote> notesOf(const Piece& p, NoteRole role) {
  std::vector<MusicNote> out;
  for (const MusicNote& n : p.notes)
    if (n.role == role) out.push_back(n);
  return out;
}

std::string of(const Piece& p) { return "seed " + std::to_string(p.seed); }

// ---------------------------------------------------------------- the pieces of the original

/**
 * What the original makes of a seed (node, v1.7.7): tempo, beats, key, mode, its chords and its notes
 * (hashed over every bit), its length, how loud it is, its gain.  (The hashes are FNV-1a as in
 * piecesOfTheOriginal() below, worked out the same way in TypeScript over composePiece(seed): of each
 * note the eight bytes of t and of d, its pitch, the bytes of v, the number of its instrument and of
 * its role; of the last six seeds only the notes were taken.)
 */
struct RefPiece {
  uint32_t seed;
  double bpm;
  int beatsPerBar, key, mode;
  size_t chords;
  uint32_t chordsHash;
  size_t notes;
  uint32_t notesHash;
  double length, loudness, gain;
};
const RefPiece REF_PIECES[] = {
    {104729u, 65, 4, 9, 0, 36, 658886704u, 530, 2777913323u, 136.9230769231, 0.01518691594428, 0.4997371693007},
    {209458u, 82, 4, 6, 0, 35, 3837865562u, 294, 770045287u, 106.4390243902, 0.02602371025258, 0.2916366002673},
    {314187u, 80, 3, 11, 2, 35, 3091067813u, 399, 1670646718u, 82.75, 0.01756999090929, 0.4319561930104},
    {418916u, 60, 4, 2, 0, 35, 558820568u, 463, 1915700569u, 144, 0.0159464968046, 0.4759331455305},
    {523645u, 66, 4, 4, 0, 36, 4232339640u, 224, 819023480u, 134.9090909091, 0.01697700558054, 0.4470438764008},
    {628374u, 64, 4, 3, 2, 36, 2370719908u, 535, 1604168654u, 139, 0.02096793862735, 0.3619557706309},
    {733103u, 81, 3, 5, 0, 36, 1108111158u, 336, 3345848529u, 84, 0.01854090565802, 0.4093363357965},
    {837832u, 83, 3, 7, 4, 36, 4043270689u, 224, 2833775232u, 82.07228915663, 0.02257568565646, 0.3361787765783},
    {942561u, 65, 4, 6, 0, 36, 513581382u, 270, 3065062696u, 136.9230769231, 0.02047539500611, 0.3706627580146},
    {1047290u, 61, 4, 3, 0, 35, 1356165210u, 458, 2255082096u, 141.7049180328, 0.02219748509034, 0.3419065877741},
    {1152019u, 78, 3, 3, 0, 35, 3702549876u, 408, 1288228481u, 84.76923076923, 0.02031000625389, 0.3736811446304},
    {1256748u, 84, 4, 5, 0, 36, 3478694444u, 264, 539933298u, 106.8571428571, 0.02445151686401, 0.3103883667673},
    {1361477u, 63, 3, 4, 2, 35, 536780441u, 336, 2839183234u, 104, 0.02189424232173, 0.3466421113314},
    {1466206u, 76, 3, 7, 0, 35, 1322061160u, 380, 3492626108u, 86.89473684211, 0.02153317222446, 0.3524546362835},
    {1570935u, 76, 3, 8, 1, 36, 2160472480u, 367, 540389562u, 89.26315789474, 0.02928734434504, 0.2591380869154},
    {1675664u, 78, 4, 2, 3, 36, 2687735016u, 407, 2058730584u, 114.7692307692, 0.0229899085736, 0.3301216427246},
    {1780393u, 78, 4, 0, 4, 36, 3585394135u, 507, 2066807129u, 114.7692307692, 0.02228693051248, 0.3405343943686},
    {1885122u, 73, 4, 7, 2, 35, 1663913521u, 300, 90976123u, 119.0684931507, 0.02430472185577, 0.3122630421134},
    {1989851u, 78, 4, 2, 3, 35, 30765169u, 237, 1194442816u, 111.6923076923, 0.01806496260487, 0.4201207913022},
    {2094580u, 79, 4, 5, 3, 36, 2346042090u, 219, 2816750406u, 113.3670886076, 0.02363409268462, 0.3211236617238},
    {2199309u, 83, 3, 2, 0, 35, 181878827u, 338, 3213326499u, 79.90361445783, 0.01633990062738, 0.4644744516798},
    {2304038u, 74, 4, 8, 4, 36, 2023950454u, 303, 3417775422u, 120.7567567568, 0.01942524726876, 0.3907011467808},
    {2408767u, 83, 3, 11, 2, 36, 1354008230u, 439, 1918441160u, 82.07228915663, 0.01847970495336, 0.4106919674074},
    {2513496u, 60, 4, 5, 0, 36, 96022212u, 434, 233355438u, 148, 0.01818400472975, 0.4173704581141},
    {0u, 65, 3, 0, 0, 35, 0u, 236, 3966033715u, 100.9230769231, 0, 0},
    {1u, 73, 4, 0, 3, 36, 0u, 338, 954174403u, 122.3561643836, 0, 0},
    {42u, 80, 4, 5, 3, 36, 0u, 464, 1670957390u, 112, 0, 0},
    {43u, 73, 3, 3, 4, 36, 0u, 460, 449860503u, 92.76712328767, 0, 0},
    {4294967295u, 77, 4, 2, 4, 36, 0u, 295, 1551998400u, 116.2077922078, 0, 0},
    {123456789u, 79, 3, 11, 0, 35, 0u, 233, 3591936354u, 83.74683544304, 0, 0},
};

uint32_t fnv(uint32_t h, uint32_t x) { return (h ^ x) * 16777619u; }
uint32_t fnvDouble(uint32_t h, double d) {
  unsigned char bytes[8];
  std::memcpy(bytes, &d, 8);
  for (unsigned char b : bytes) h = fnv(h, b);
  return h;
}

void piecesOfTheOriginal() {
  for (const RefPiece& r : REF_PIECES) {
    const Piece p = composePiece(r.seed);
    const std::string name = of(p);
    check(p.seed == r.seed && p.bpm == r.bpm && p.beatsPerBar == r.beatsPerBar && p.key == r.key && static_cast<int>(p.mode) == r.mode, name + ": the tempo, the beat, the key and the mode of the original");
    check(p.chords.size() == r.chords && p.notes.size() == r.notes, name + ": as many chords and notes as the original");
    uint32_t h = 2166136261u;
    for (const MusicNote& n : p.notes) {
      h = fnvDouble(h, n.t);
      h = fnvDouble(h, n.d);
      h = fnv(h, static_cast<uint32_t>(n.p & 255));
      h = fnvDouble(h, n.v);
      h = fnv(h, static_cast<uint32_t>(n.i));
      h = fnv(h, static_cast<uint32_t>(n.role));
    }
    check(h == r.notesHash, name + ": every note as in the original, to the last bit");
    check(std::abs(p.length - r.length) < 1e-9, name + ": as long as the original");
    if (r.loudness == 0) continue;
    uint32_t hc = 2166136261u;
    for (int c : p.chords) hc = fnv(hc, static_cast<uint32_t>(c));
    check(hc == r.chordsHash, name + ": the chords of the original");
    check(std::abs(pieceLoudness(p, 1) - r.loudness) < 1e-9 * r.loudness, name + ": as loud as the original");
    check(std::abs(pieceGain(p, MusicMode::Quiet, 0.8) - r.gain) < 1e-9 * r.gain, name + ": the gain of the original");
  }
}

// ---------------------------------------------------------------- tests/game/music.test.ts

void sameSeedSamePiece() {
  // the same seed makes the same piece
  assert(composePiece(4 * 104729u) == pieces()[3]);
}

void inKeyInRangeInOrder() {
  // every note in the key and mode, in its instrument's range, in order, within the piece
  /** where each instrument may play (MIDI): bell, pluck, marimba, flute, pad, bass */
  const int RANGE[INSTRUMENT_COUNT][2] = {{55, 88}, {50, 88}, {52, 88}, {55, 88}, {55, 67}, {36, 58}};
  for (const Piece& p : pieces()) {
    double last = 0;
    for (const MusicNote& n : p.notes) {
      check(scaleDegree(p, n.p) >= 0, of(p) + ": every note is in the key and mode");
      const int lo = RANGE[static_cast<size_t>(n.i)][0], hi = RANGE[static_cast<size_t>(n.i)][1];
      check(n.p >= lo && n.p <= hi, of(p) + ": " + instrumentName(n.i) + " at " + std::to_string(n.p));
      assert(n.t >= last - 1e-9);   // in order
      last = n.t;
      assert(n.t >= 0 && n.t + n.d <= p.length);   // within the piece
      assert(n.d > 0 && n.v > 0 && n.v <= 1);
    }
  }
}

void tuneLeansOnTheChord() {
  // the tune leans on the chord: where it falls on the bar and where it is held, it is a note of the chord
  for (const Piece& p : pieces()) {
    const double spb = 60 / p.bpm;
    const double bar = p.beatsPerBar * spb;
    const std::vector<MusicNote> mel = notesOf(p, NoteRole::Melody);
    check(mel.size() > 20, of(p) + ": a tune");
    int leaning = 0;
    for (const MusicNote& n : mel) {
      // (the notes are a little off the beat, as played: up to 8 ms)
      const int b = static_cast<int>(std::floor((n.t + 0.02) / bar));
      const double beat = (n.t + 0.02 - b * bar) / spb;
      const double held = n.d / 0.92 / spb;
      if (beat > 0.05 && held < 1.45) continue;
      leaning++;
      const int root = p.chords[static_cast<size_t>(b)];
      const int deg = scaleDegree(p, n.p);
      const int rel = (((deg - root) % 7) + 7) % 7;
      check(rel == 0 || rel == 2 || rel == 4, of(p) + ": bar " + std::to_string(b) + " on degree " + std::to_string(root) + ", tune on degree " + std::to_string(deg));
    }
    assert(leaning > 8);
    // and it moves mostly by step: no leap beyond an octave and a step (that only where a section comes back), most moves a step or two
    int steps = 0;
    for (size_t i = 1; i < mel.size(); i++) {
      const int d = std::abs(mel[i].p - mel[i - 1].p);
      check(d <= 14, of(p) + ": a leap of " + std::to_string(d));
      if (d <= 4) steps++;
    }
    check(steps / static_cast<double>(mel.size() - 1) > 0.6, of(p) + ": mostly steps");
  }
}

void endsAtHome() {
  // a piece ends at home: its last chord and its last tune note on the tonic, the bass on its root
  for (const Piece& p : pieces()) {
    assert(p.chords.back() == 0);
    check(scaleDegree(p, notesOf(p, NoteRole::Melody).back().p) == 0, of(p) + ": the tune ends on the tonic");
    assert(scaleDegree(p, notesOf(p, NoteRole::Bass).back().p) == 0);
  }
}

void aMinuteOrTwo() {
  // pieces of a minute or two, calm, and different from each other
  std::set<int> keys;
  std::set<ModeName> modes;
  std::set<std::string> tunes, instruments;
  bool three = false, four = false;
  for (const Piece& p : pieces()) {
    check(p.length > 60 && p.length < 200, of(p) + ": a minute or two");
    assert(p.bpm >= 60 && p.bpm <= 84);
    assert(p.beatsPerBar == 3 || p.beatsPerBar == 4);
    assert(std::find(MODE_NAMES.begin(), MODE_NAMES.end(), p.mode) != MODE_NAMES.end());
    keys.insert(p.key);
    modes.insert(p.mode);
    const std::vector<MusicNote> mel = notesOf(p, NoteRole::Melody);
    std::string tune;
    for (size_t i = 0; i < 8 && i < mel.size(); i++) tune += std::to_string(mel[i].p - p.key) + ",";
    tunes.insert(tune);
    std::set<std::string> by;
    for (const MusicNote& n : mel) by.insert(instrumentName(n.i));
    std::string combination;
    for (const std::string& i : by) combination += i + "+";
    instruments.insert(combination);
    three = three || p.beatsPerBar == 3;
    four = four || p.beatsPerBar == 4;
  }
  assert(keys.size() >= 8);
  assert(modes.size() >= 4);
  assert(tunes.size() == pieces().size());   // no two tunes alike
  assert(instruments.size() >= 4);
  assert(three && four);
}

void cheapToPlay() {
  // cheap to play: never many notes sounding at once
  auto ring = [](const MusicNote& n) -> double {
    switch (n.i) {
      case Instrument::Bell: return 0.9 * 4;
      case Instrument::Marimba: return 0.3 * 4;
      case Instrument::Pluck: return 0.45 * 4;
      case Instrument::Flute: return n.d + 0.3;
      case Instrument::Pad: return n.d + 1.5;
      case Instrument::Bass: return n.d + 0.4;
    }
    return 0;
  };
  for (const Piece& p : pieces()) {
    std::vector<std::pair<double, int>> edges;
    for (const MusicNote& n : p.notes) {
      edges.emplace_back(n.t, 1);
      edges.emplace_back(n.t + ring(n), -1);
    }
    std::sort(edges.begin(), edges.end());
    int now = 0, most = 0;
    for (const auto& e : edges) most = std::max(most, now += e.second);
    check(most <= 22, of(p) + ": " + std::to_string(most) + " notes at once");
  }
}

void quietAgainstTheVehicles() {
  // quiet against the vehicles: every piece as loud as its mode asks, well under the car's engine
  for (const Piece& p : pieces()) {
    for (MusicMode mode : {MusicMode::Quiet, MusicMode::On}) {
      const double db = 20 * std::log10(pieceLoudness(p, pieceGain(p, mode, 1)) / REF_LOUDNESS);
      check(std::abs(db - MUSIC_DB[mode]) < 0.01, of(p) + ", " + musicModeName(mode) + ": as loud as asked");
    }
    assert(pieceGain(p, MusicMode::On, 0.5) < pieceGain(p, MusicMode::On, 1));   // the volume turns it down
    assert(pieceGain(p, MusicMode::Off, 1) == 0);
  }
  assert(MUSIC_DB[MusicMode::Quiet] < MUSIC_DB[MusicMode::On] && MUSIC_DB[MusicMode::On] <= -3);   // quiet quieter, both under the engine
  for (MusicMode m : {MusicMode::Off, MusicMode::Quiet, MusicMode::On}) assert(musicModeOf(musicModeName(m)) == m);
  assert(!musicModeOf("loud"));
}

void pauses() {
  // pauses: a few minutes now and then, under a minute when on
  for (double r : {0.0, 0.5, 0.999}) {
    const double q = pauseAfter(MusicMode::Quiet, r);
    const double o = pauseAfter(MusicMode::On, r);
    assert(q >= 120 && q <= 360);
    assert(o >= 20 && o <= 75);
  }
}

void thePlayer() {
  // the player: nothing while off; then a piece soon, scheduled a moment ahead; a pause; off again
  const AudioContext ctx = AudioContext::create();
  const GainNode out = ctx.createGain();
  MusicPlayer music(ctx, out, MusicOptions{.timer = false, .seed = 42});
  music.tick();
  assert(audioNative::created(ctx) == 1);   // off: nothing made
  music.set(MusicMode::On, 1);
  const int graph = audioNative::created(ctx);
  assert(graph > 5 && graph < 40);   // a few shared nodes
  auto step = [&](double s) {
    plat::native::advance(s * 1000);
    music.tick();
  };
  step(0.25);
  assert(!music.playing());   // not at once
  for (int i = 0; i < 40 && !music.playing(); i++) step(0.25);
  assert(music.playing());   // within a few seconds
  assert(ctx.currentTime() < 8);
  // a note is made only a moment before it is due: never more than the look-ahead in advance
  const double begun = ctx.currentTime();
  int made = audioNative::made(ctx, "osc");
  std::vector<double> due;
  for (int i = 0; i < 80; i++) {
    step(0.25);
    const int now = audioNative::made(ctx, "osc");
    if (now > made) due.push_back(ctx.currentTime());
    made = now;
  }
  assert(due.size() > 10);   // notes keep coming
  for (const audioNative::NodeInfo* n : audioNative::nodes(ctx))
    if (n->kind == "osc" && n->started > 0) assert(n->started <= ctx.currentTime() + 1.3);   // scheduled at most a little ahead
  assert(ctx.currentTime() - begun > 15);
  // to the end of the piece, then a pause of under a minute ('on'), then the next piece
  for (int i = 0; i < 4 * 240 && music.playing(); i++) step(0.25);
  assert(!music.playing());   // the piece has ended
  assert(music.played == 1);
  const double pauseFrom = ctx.currentTime();
  for (int i = 0; i < 4 * 90 && !music.playing(); i++) step(0.25);
  const double pause = ctx.currentTime() - pauseFrom;
  assert(music.playing() && pause >= 20 && pause <= 75);
  assert(music.played == 2);
  // off: no more notes
  music.set(MusicMode::Off, 1);
  const int after = audioNative::created(ctx);
  for (int i = 0; i < 40; i++) step(0.25);
  assert(audioNative::created(ctx) == after);   // off: nothing more is made
}

// ---------------------------------------------------------------- everything that is asked of the Web Audio API

/** a number as the digest writes it: whole numbers as they are, the others to nine places (JavaScript's toExponential(8)) */
std::string digestNumber(double x) {
  char b[48];
  if (x == std::floor(x) && std::abs(x) < 1e15) {
    if (x == 0) return "0";
    std::snprintf(b, sizeof b, "%.0f", x);
    return b;
  }
  std::snprintf(b, sizeof b, "%.8e", x);
  std::string s = b;
  const size_t e = s.find('e');
  std::string exponent = s.substr(e + 2);
  while (exponent.size() > 1 && exponent[0] == '0') exponent.erase(0, 1);
  return s.substr(0, e + 2) + exponent;
}

/**
 * The nodes of a context from number `from` up to `to` (0: the loudspeakers, then in the order they were
 * made), each as a line: its kind and type, how it was started and stopped, what it feeds, and of every
 * setting that was touched its value and the last `last` things asked of it (what, value, time, time
 * constant).  The original's stand-in for the Web Audio API writes the same lines of what the original
 * asks of it; a line is compared by its hash (FNV-1a).
 */
std::vector<std::string> digestLines(const AudioContext& ctx, size_t from, size_t to, size_t last) {
  const std::vector<const audioNative::NodeInfo*> nodes = audioNative::nodes(ctx);
  auto indexOf = [&nodes](int id) -> std::string {
    for (size_t i = 0; i < nodes.size(); i++)
      if (nodes[i]->id == id) return std::to_string(i);
    return "?";
  };
  static const char* const NAMES[] = {"gain", "frequency", "detune", "Q", "pan", "delayTime", "threshold", "knee", "ratio", "attack", "release"};
  std::vector<std::string> out;
  for (size_t i = from; i < std::min(to, nodes.size()); i++) {
    const audioNative::NodeInfo& n = *nodes[i];
    // (an oscillator that was given a wave: the stand-in keeps the type it had, a browser and this host say 'custom')
    std::string line = std::to_string(i) + "|" + n.kind + "|" + (n.wave ? std::string("sine") : n.type) + "|" + (n.loop ? "1" : "0") + "|" + (n.buffer ? "1" : "0") + "|" +
                       digestNumber(n.started) + "|" + digestNumber(n.stopped) + "|" + digestNumber(n.offset) + "|" + digestNumber(n.duration) + "|";
    std::string outs;
    for (int o : n.outputs) outs += (outs.empty() ? "" : ",") + indexOf(o);
    for (const auto& o : n.paramOutputs) outs += (outs.empty() ? "" : ",") + indexOf(o.first) + "." + o.second;
    line += outs;
    for (const char* name : NAMES) {
      const audioNative::ParamInfo& p = n.param(name);
      // (touched: set, or sent somewhere – a setting whose plans were only cancelled is not)
      bool touched = false;
      std::vector<const audioNative::ParamEvent*> asked;
      for (const audioNative::ParamEvent& e : p.events) {
        if (e.what != "cancel") touched = true;
        if (e.what != "value") asked.push_back(&e);
      }
      if (!touched) continue;
      line += std::string("|") + name + "=" + digestNumber(p.value) + ":";
      for (size_t k = asked.size() > last ? asked.size() - last : 0; k < asked.size(); k++) {
        const audioNative::ParamEvent& e = *asked[k];
        line += (line.back() == ':' ? "" : ";") + e.what + "," + digestNumber(e.value) + "," + digestNumber(e.time) + "," + digestNumber(e.timeConstant);
      }
    }
    out.push_back(line);
  }
  return out;
}

uint32_t digestHash(const std::string& s) {
  uint32_t h = 2166136261u;
  for (unsigned char c : s) h = (h ^ c) * 16777619u;
  return h;
}

/** the lines against the original's hashes: where they part, the line is shown */
void sameDigest(const std::string& what, const std::vector<std::string>& lines, const std::vector<uint32_t>& want) {
  if (lines.size() != want.size()) std::fprintf(stderr, "audio_music_test: %s: %zu nodes here, %zu in the original\n", what.c_str(), lines.size(), want.size());
  for (size_t i = 0; i < lines.size() && i < want.size(); i++) {
    if (digestHash(lines[i]) == want[i]) continue;
    std::fprintf(stderr, "audio_music_test: %s: line %zu is not the original's:\n%s\n", what.c_str(), i, lines[i].c_str());
    std::abort();
  }
  check(lines.size() == want.size(), what + ": as many nodes as in the original");
}

// ---------------------------------------------------------------- the player of the original

/**
 * The original's player on its stand-in for the Web Audio API (node, v1.7.7, tests/game/audiomock.ts;
 * seed 42, `Math.random = seeded(7)` of waves.ts, the clock in quarters of a second from nought, this
 * function line by line): what it had made and sent at each of these moments – the comments name the
 * original's numbers.
 */
void asTheOriginalsPlayer() {
  plat::native::manualClock(true);
  rndSeed(7);
  const AudioContext ctx = AudioContext::create();
  const GainNode dest = ctx.createGain();
  MusicPlayer music(ctx, dest, MusicOptions{.timer = false, .seed = 42});
  using audioNative::created;
  using audioNative::made;
  using audioNative::paramCalls;
  music.tick();
  music.set(MusicMode::On, 1);
  assert(created(ctx) == 30);   // music.graph 30
  auto step = [&](double s) {
    plat::native::advance(s * 1000);
    music.tick();
  };
  int n = 0;
  for (; n < 60 && !music.playing(); n++) step(0.25);
  assert(n == 13 && ctx.currentTime() * 4 == 13);   // music.begun 13 13
  for (int i = 0; i < 80; i++) step(0.25);
  if (created(ctx) != 200 || paramCalls(ctx) != 286) std::fprintf(stderr, "audio_music_test: after 80 steps %d nodes, %d oscillators, %d gains, %ld settings (200, 86, 102, 286 in the original)\n", created(ctx), made(ctx, "osc"), made(ctx, "gain"), paramCalls(ctx));
  assert(created(ctx) == 200 && made(ctx, "osc") == 86 && made(ctx, "gain") == 102 && paramCalls(ctx) == 286);   // music.after80 200 86 102 286
  // … and every one of these nodes is the original's: what it is, what it feeds, when it starts and stops, what is asked of its settings
  const std::vector<uint32_t> ORIGINAL = {
      282407515u, 2962159108u, 1034673337u, 561914607u, 2793959594u, 2211708893u, 2658696605u, 1168028332u, 2764879760u, 1816358507u, 2293906049u, 2511034986u, 2865996325u,
      583464975u, 3114260933u, 2028503108u, 1405813196u, 2814076609u, 521648815u, 2118196474u, 1412630290u, 4017196367u, 1845102945u, 3702230131u, 1877739361u, 417985466u,
      3817285871u, 1923160014u, 3594251252u, 468142433u, 3986129393u, 1004862757u, 4113503122u, 4012014252u, 2223843605u, 92738665u, 1055295170u, 2452312776u, 899444875u,
      325336177u, 1519280703u, 2415073138u, 2138140234u, 2466568518u, 1456365800u, 2251159280u, 3079967885u, 3472215071u, 3673678730u, 3311916626u, 4088425688u, 1806987731u,
      3012429029u, 807934628u, 2058806906u, 875702102u, 3458121574u, 958192264u, 3030378616u, 87275118u, 1627527538u, 2697921818u, 338683929u, 282881813u, 3434314878u, 923213666u,
      1064172193u, 2178924582u, 1618504082u, 1489862116u, 1226628172u, 1935720969u, 2313560579u, 1090802432u, 2486486537u, 3190380929u, 3103665976u, 2574110856u, 688687573u,
      1219432256u, 990005860u, 3499859794u, 2690797045u, 973756116u, 2361428400u, 1275518742u, 3010904564u, 735764064u, 2174524461u, 2926916751u, 2711994044u, 257657897u,
      3578546953u, 1045547415u, 3937854408u, 1792185752u, 371604011u, 2170492884u, 2151210043u, 443798952u, 3875293446u, 1952244202u, 1380141136u, 1820754556u, 759714193u,
      3785244459u, 61742780u, 923317731u, 322337423u, 4246346595u, 1719127462u, 2855750641u, 2071386735u, 3794346673u, 642630923u, 4236606811u, 42527201u, 1577386492u,
      4071284875u, 771811233u, 1484733380u, 3613940002u, 2931371180u, 2636536861u, 2889759492u, 384522349u, 3692882911u, 2708445947u, 3612874977u, 1780575223u, 3556394723u,
      2429514610u, 3934532058u, 1080072136u, 2822084885u, 2772142300u, 3178871948u, 89606772u, 282236310u, 699854585u, 3464065090u, 1172167831u, 2169994335u, 1827037676u,
      3979955216u, 2221999073u, 3978291896u, 1857909097u, 3837908463u, 955105388u, 3876591223u, 3065485511u, 1815385195u, 595864455u, 781905744u, 3532518613u, 2914744139u,
      1285992646u, 2426780850u, 3989789009u, 647578183u, 1015989459u, 3038262878u, 1435995762u, 3459595632u, 1661050355u, 2937695420u, 3010962766u, 199257099u, 1590209580u,
      3059391715u, 425191380u, 902289989u, 788939772u, 157088690u, 1219331247u, 3573566329u, 3781268974u, 281909153u, 1512670994u, 3666777588u, 1707911023u, 1235940754u,
      3980068351u, 3706094187u, 2189841339u, 3782829796u, 4237878685u, 351383343u, 1108384975u, 3337452148u, 424151811u, 1767161978u, 2402566389u, 3848361310u, 4030238321u,
      281681451u, 965837341u, 985483468u, 3549494166u,
  };
  sameDigest("the music's first seconds", digestLines(ctx, 1, 100000, 20), ORIGINAL);
  int k = 0;
  for (; k < 4 * 240 && music.playing(); k++) step(0.25);
  if (k != 369 || created(ctx) != 1006) std::fprintf(stderr, "audio_music_test: the piece ended after %d steps, %d nodes, %ld settings (369, 1006, 1579 in the original)\n", k, created(ctx), paramCalls(ctx));
  assert(k == 369 && music.played == 1 && created(ctx) == 1006 && made(ctx, "osc") == 489 && paramCalls(ctx) == 1579);   // music.ended 369 1 1006 489 1579
  int q = 0;
  for (; q < 4 * 90 && !music.playing(); q++) step(0.25);
  assert(q == 153 && music.played == 2);   // music.second 153 2
  for (int i = 0; i < 40; i++) step(0.25);
  assert(created(ctx) == 1128 && made(ctx, "osc") == 550 && paramCalls(ctx) == 1765);   // music.second40 1128 550 1765
  music.set(MusicMode::Quiet, 0.5);
  for (int i = 0; i < 40; i++) step(0.25);
  assert(created(ctx) == 1264 && paramCalls(ctx) == 1966);   // music.quiet40 1264 1966
  music.set(MusicMode::Off, 1);
  const int after = created(ctx);
  for (int i = 0; i < 40; i++) step(0.25);
  assert(created(ctx) == after && paramCalls(ctx) == 1968);   // music.off 0 1968
}

// ---------------------------------------------------------------- the player by its own timer

void byItsOwnTimer() {
  // (as in the game: four times a second the player looks ahead by itself)
  rndSeed(11);
  // (what the players before left to end by itself has ended first)
  plat::native::advance(20000);
  plat::native::drain();
  assert(audioDetail::endedCount() == 0);
  const int waiting = timers::pending();
  const int alive0 = audioNative::alive();
  const AudioContext ctx = AudioContext::create();
  const GainNode dest = ctx.createGain();
  {
    MusicPlayer music(ctx, dest, MusicOptions{.seed = 1000});
    plat::native::advance(1000);
    plat::native::drain();
    assert(audioNative::created(ctx) == 1 && timers::pending() == waiting);   // off: nothing made, nothing runs
    music.set(MusicMode::On, 1);
    assert(timers::pending() == waiting + 1);
    for (int i = 0; i < 4 * 30 && music.played == 0; i++) {
      plat::native::advance(250);
      plat::native::drain();
    }
    assert(music.playing() && music.played == 1);
    for (int i = 0; i < 4 * 20; i++) {
      plat::native::advance(250);
      plat::native::drain();
    }
    assert(audioNative::made(ctx, "osc") > 20);
    // the notes that have died away are let go of, and what they hung on the shared vibrato is undone
    const audioNative::NodeInfo* vibratoDepth = nullptr;
    for (const audioNative::NodeInfo* n : audioNative::nodes(ctx))
      if (n->kind == "gain" && n->param("gain").value == 9) vibratoDepth = n;
    assert(vibratoDepth);
    int sounding = 0;
    for (const audioNative::NodeInfo* n : audioNative::nodes(ctx))
      if (n->kind == "osc" && n->stopped > ctx.currentTime()) sounding++;
    assert(static_cast<int>(vibratoDepth->paramOutputs.size()) <= sounding);
    // switched off: faded out, then taken apart – and nothing runs any more
    music.dispose();
    assert(!music.playing() && timers::pending() == waiting + 1);   // (only the taking apart is still to come)
    const int made = audioNative::created(ctx);
    plat::native::advance(2000);
    plat::native::drain();
    assert(timers::pending() == waiting && audioNative::created(ctx) == made);
    for (const audioNative::NodeInfo* n : audioNative::nodes(ctx))
      if (n->kind == "panner" || n->kind == "delay") assert(n->outputs.empty() && n->released);
    assert(!audioNative::info(dest)->released);
    // on again, and then the player itself goes while its timer runs: the timer goes with it
    music.set(MusicMode::Quiet, 0.5);
    assert(timers::pending() == waiting + 1);
  }
  assert(timers::pending() == waiting);
  // (every note has ended by now: nothing is left of the music but the context and the gain it played into)
  plat::native::advance(20000);
  plat::native::drain();
  assert(audioNative::alive() == alive0 + 2 && audioDetail::endedCount() == 0);
}

}  // namespace

int main() {
  // (first: it begins as the original's clock does, at nought)
  asTheOriginalsPlayer();
  piecesOfTheOriginal();
  sameSeedSamePiece();
  inKeyInRangeInOrder();
  tuneLeansOnTheChord();
  endsAtHome();
  aMinuteOrTwo();
  cheapToPlay();
  quietAgainstTheVehicles();
  pauses();
  thePlayer();
  byItsOwnTimer();
  std::printf("audio_music_test: ok\n");
  return 0;
}
