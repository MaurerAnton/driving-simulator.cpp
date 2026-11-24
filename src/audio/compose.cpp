// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/audio/compose.ts @ v1.7.7
#include "audio/compose.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

#include "core/js.h"

namespace fs {

namespace {

/** four-chord progressions per mode (degrees; the mode's diminished triad is left out) */
const std::vector<std::array<int, 4>>& PROGRESSIONS(ModeName mode) {
  static const std::array<std::vector<std::array<int, 4>>, 5> all = {{
      // major
      {
          {0, 4, 5, 3},
          {0, 5, 3, 4},
          {0, 3, 0, 4},
          {0, 2, 3, 4},
          {5, 3, 0, 4},
          {0, 3, 5, 4},
          {0, 3, 1, 4},
          {3, 4, 2, 5},
          {0, 1, 3, 4},
      },
      // lydian
      {
          {0, 1, 0, 1},
          {0, 1, 5, 4},
          {0, 1, 6, 5},
          {0, 1, 0, 4},
          {0, 6, 1, 0},
      },
      // mixolydian
      {
          {0, 6, 3, 0},
          {0, 6, 0, 3},
          {0, 3, 6, 0},
          {0, 4, 6, 3},
          {0, 5, 6, 3},
      },
      // dorian
      {
          {0, 3, 0, 3},
          {0, 2, 3, 0},
          {0, 6, 3, 0},
          {0, 3, 6, 0},
          {0, 1, 2, 3},
      },
      // minor
      {
          {0, 5, 2, 6},
          {0, 3, 5, 4},
          {5, 6, 0, 0},
          {0, 6, 5, 6},
          {0, 5, 3, 4},
          {0, 2, 6, 5},
          {0, 3, 6, 2},
      },
  }};
  return all[static_cast<size_t>(mode)];
}

/** the last two chords of a section that comes home: [before, home] */
const std::vector<std::array<int, 2>>& CADENCES(ModeName mode) {
  static const std::array<std::vector<std::array<int, 2>>, 5> all = {{
      // major
      {
          {4, 0},
          {3, 0},
      },
      // lydian
      {
          {1, 0},
          {4, 0},
      },
      // mixolydian
      {
          {6, 0},
          {3, 0},
      },
      // dorian
      {
          {3, 0},
          {6, 0},
      },
      // minor
      {
          {6, 0},
          {3, 0},
          {4, 0},
      },
  }};
  return all[static_cast<size_t>(mode)];
}

/** [beat, beats held] */
using Hit = std::array<double, 2>;
using Rhythm = std::vector<Hit>;

/** a bar of melody: [beat, beats held] */
const std::vector<Rhythm>& RHYTHMS(int bpb) {
  static const std::vector<Rhythm> four = {
      Rhythm{Hit{0, 1}, Hit{1, 1}, Hit{2, 2}},
      Rhythm{Hit{0, 1.5}, Hit{1.5, 0.5}, Hit{2, 2}},
      Rhythm{Hit{0, 2}, Hit{2, 1}, Hit{3, 1}},
      Rhythm{Hit{0, 0.5}, Hit{0.5, 0.5}, Hit{1, 1}, Hit{2, 2}},
      Rhythm{Hit{0, 1}, Hit{1, 0.5}, Hit{1.5, 0.5}, Hit{2, 1}, Hit{3, 1}},
      Rhythm{Hit{0, 3}, Hit{3, 1}},
      Rhythm{Hit{1, 1}, Hit{2, 1}, Hit{3, 1}},
      Rhythm{Hit{0, 1}, Hit{1, 1}, Hit{2, 1}, Hit{3, 1}},
  };
  static const std::vector<Rhythm> three = {
      Rhythm{Hit{0, 2}, Hit{2, 1}},
      Rhythm{Hit{0, 1}, Hit{1, 1}, Hit{2, 1}},
      Rhythm{Hit{0, 1.5}, Hit{1.5, 0.5}, Hit{2, 1}},
      Rhythm{Hit{0, 1}, Hit{1, 2}},
      Rhythm{Hit{1, 1}, Hit{2, 1}},
  };
  return bpb == 4 ? four : three;
}

/** the bar a phrase ends with: a long note */
const std::vector<Rhythm>& ENDINGS(int bpb) {
  static const std::vector<Rhythm> four = {
      Rhythm{Hit{0, 4}},
      Rhythm{Hit{0, 2}, Hit{2, 2}},
      Rhythm{Hit{0, 1}, Hit{1, 3}},
      Rhythm{Hit{0, 3}},
  };
  static const std::vector<Rhythm> three = {
      Rhythm{Hit{0, 3}},
      Rhythm{Hit{0, 1}, Hit{1, 2}},
      Rhythm{Hit{0, 2}},
  };
  return bpb == 4 ? four : three;
}

using Arpeggio = std::vector<int>;

/** broken chords: which of the chord's notes (root, third, fifth, octave, third above) on each eighth or quarter */
const std::vector<Arpeggio>& ARPEGGIOS(int bpb, bool eighths) {
  static const std::vector<Arpeggio> eighths4 = {
      Arpeggio{0, 2, 3, 2, 1, 2, 3, 2},
      Arpeggio{0, 1, 2, 3, 2, 1, 2, 1},
      Arpeggio{0, 2, 4, 2, 3, 2, 1, 2},
      Arpeggio{0, 3, 2, 3, 1, 3, 2, 3},
  };
  static const std::vector<Arpeggio> quarters4 = {
      Arpeggio{0, 2, 3, 2},
      Arpeggio{0, 1, 2, 3},
      Arpeggio{0, 2, 1, 2},
  };
  static const std::vector<Arpeggio> eighths3 = {
      Arpeggio{0, 2, 3, 2, 1, 2},
      Arpeggio{0, 1, 2, 3, 2, 1},
      Arpeggio{0, 2, 4, 2, 3, 2},
  };
  static const std::vector<Arpeggio> quarters3 = {
      Arpeggio{0, 2, 3},
      Arpeggio{0, 1, 2},
      Arpeggio{0, 2, 1},
  };
  return bpb == 4 ? (eighths ? eighths4 : quarters4) : (eighths ? eighths3 : quarters3);
}

/** [beat, beats held, root (0) or fifth (1)] */
using BassHit = std::array<double, 3>;
using BassLine = std::vector<BassHit>;

/** the bass in a bar: [beat, beats held, root (0) or fifth (1)] */
const std::vector<BassLine>& BASSES(int bpb) {
  static const std::vector<BassLine> four = {
      BassLine{BassHit{0, 4, 0}},
      BassLine{BassHit{0, 2, 0}, BassHit{2, 2, 1}},
      BassLine{BassHit{0, 3, 0}, BassHit{3, 1, 1}},
  };
  static const std::vector<BassLine> three = {
      BassLine{BassHit{0, 3, 0}},
      BassLine{BassHit{0, 2, 0}, BassHit{2, 1, 1}},
  };
  return bpb == 4 ? four : three;
}

struct Palette {
  Instrument melody;
  Instrument second;
  /** (nullopt: no broken chords) */
  std::optional<Instrument> arp;
  bool pad;
};

/** who plays: the theme (and on its return), broken chords (or none), held chords */
const std::vector<Palette>& PALETTES() {
  static const std::vector<Palette> all = {
      Palette{Instrument::Bell, Instrument::Flute, Instrument::Pluck, true},
      Palette{Instrument::Flute, Instrument::Bell, Instrument::Pluck, false},
      Palette{Instrument::Marimba, Instrument::Bell, std::nullopt, true},
      Palette{Instrument::Flute, Instrument::Marimba, Instrument::Marimba, true},
      Palette{Instrument::Bell, Instrument::Pluck, std::nullopt, true},
      Palette{Instrument::Pluck, Instrument::Flute, Instrument::Marimba, false},
  };
  return all;
}

/** the melody's range, in steps of the scale above its tonic (an octave and a half) */
const int LO = 0;
const int HI = 10;

int mod(int a, int n) { return ((a % n) + n) % n; }
/** Math.floor(a / n) of whole numbers */
int floorDiv(int a, int n) { return static_cast<int>(std::floor(static_cast<double>(a) / n)); }
int sign(int x) { return x > 0 ? 1 : x < 0 ? -1 : 0; }

struct MelNote {
  /** beat from the start of the section, beats held, step of the scale (0: the tonic), leant on */
  double beat;
  double dur;
  int idx;
  bool strong;
};

enum class Close { Home, Open, Half };

enum class PartKind { Intro, A, A2, B, A3, Outro };

struct Part {
  PartKind kind;
  int bars;
  std::vector<int> chords;
  double level;
};

}  // namespace

const char* instrumentName(Instrument i) {
  static const char* const names[] = {"bell", "pluck", "marimba", "flute", "pad", "bass"};
  return names[static_cast<size_t>(i)];
}

const char* modeName(ModeName mode) {
  static const char* const names[] = {"major", "lydian", "mixolydian", "dorian", "minor"};
  return names[static_cast<size_t>(mode)];
}

Piece composePiece(uint32_t seed) {
  Seeded rnd = seeded(seed);
  auto pick = [&rnd](const auto& a) -> const auto& { return a[static_cast<size_t>(std::floor(rnd() * static_cast<double>(a.size()))) % a.size()]; };
  auto weighted = [&rnd](const auto& items) {
    double sum = 0;
    for (const auto& item : items) sum += item.second;
    double r = rnd() * sum;
    for (const auto& item : items) {
      r -= item.second;
      if (r <= 0) return item.first;
    }
    return items[items.size() - 1].first;
  };

  const std::vector<std::pair<ModeName, double>> modes = {
      {ModeName::Major, 0.3},
      {ModeName::Lydian, 0.15},
      {ModeName::Mixolydian, 0.15},
      {ModeName::Dorian, 0.2},
      {ModeName::Minor, 0.2},
  };
  const ModeName mode = weighted(modes);
  const std::array<int, 7>& scale = MODES[mode];
  const int key = static_cast<int>(std::floor(rnd() * 12));
  const double bpm = jsRound(60 + rnd() * 24);
  const int bpb = rnd() < 0.3 ? 3 : 4;
  const double spb = 60 / bpm;
  const double barSec = bpb * spb;
  const Palette& palette = pick(PALETTES());
  const bool sevenths = rnd() < 0.45;
  const bool eighths = bpm < 70 || rnd() < 0.5;   // arpKind: 'eighths' : 'quarters'
  const Arpeggio& arpPattern = pick(ARPEGGIOS(bpb, eighths));
  const BassLine& bassPattern = pick(BASSES(bpb));

  /** the scale's notes as one ladder: step `idx` above the tonic `base` (MIDI) */
  auto midi = [&scale](int base, int idx) -> int { return base + 12 * floorDiv(idx, 7) + scale[static_cast<size_t>(mod(idx, 7))]; };
  auto isChordTone = [](int idx, int root) -> bool {
    const int d = mod(idx - root, 7);
    return d == 0 || d == 2 || d == 4;
  };
  /** the nearest note of the chord, looking first the way the melody moves – within the melody's range */
  auto snap = [&isChordTone](int idx, int root, int dir) -> int {
    const int first = dir >= 0 ? 1 : -1;
    for (int d = 0; d <= 3; d++) {
      const int tries[2] = {d * first, -d * first};
      for (int k = 0; k < (d == 0 ? 1 : 2); k++) {
        const int j = idx + tries[k];
        if (j >= LO && j <= HI && isChordTone(j, root)) return j;
      }
    }
    return std::max(LO, std::min(HI, idx));
  };
  const std::vector<std::pair<int, double>> steps = {
      {-4, 1},
      {-3, 3},
      {-2, 10},
      {-1, 28},
      {0, 5},
      {1, 28},
      {2, 10},
      {3, 3},
      {4, 1},
  };
  /**
   * The next note: mostly a step, a leap turned back by a step the other way, on the chord where it leans – and
   * near where the phrase began (`center`): a step that would take it more than four steps away turns back.
   */
  auto walk = [&](int prev, int last, bool strong, int root, int center) -> int {
    int step = std::abs(last) >= 3 ? -sign(last) : weighted(steps);
    if (std::abs(prev + step - center) > 4 && std::abs(prev + step - center) > std::abs(prev - center)) step = -step;
    if (prev + step < LO || prev + step > HI) step = -step;
    int idx = std::max(LO, std::min(HI, prev + step));
    if (strong) idx = snap(idx, root, step);
    return idx;
  };

  // ---- harmony: a progression for A (closing with a cadence), another for B (closing half)
  const std::vector<std::array<int, 4>>& progs = PROGRESSIONS(mode);
  const std::array<int, 4>* progA = &pick(progs);
  const std::array<int, 4>* progB = &pick(progs);
  for (int i = 0; i < 4 && progB == progA && progs.size() > 1; i++) progB = &pick(progs);
  // (B begins away from home: the progression turned until it starts elsewhere than on the tonic)
  int turn = 0;
  while ((*progB)[static_cast<size_t>(turn % 4)] == 0 && turn < 3) turn++;
  std::array<int, 4> rotB;
  for (int k = 0; k < 4; k++) rotB[static_cast<size_t>(k)] = (*progB)[static_cast<size_t>((k + turn) % 4)];
  // (a cadence whose first chord is not the one just before it: no chord twice in a row on the way home)
  std::vector<std::array<int, 2>> cads;
  for (const std::array<int, 2>& c : CADENCES(mode))
    if (c[0] != (*progA)[1]) cads.push_back(c);
  const std::array<int, 2> cad = pick(cads.size() > 0 ? cads : CADENCES(mode));
  const std::vector<int> chordsA = {(*progA)[0], (*progA)[1], (*progA)[2], (*progA)[3], (*progA)[0], (*progA)[1], cad[0], cad[1]};
  const int beforeHalf = rotB[2] != cad[0] ? rotB[2] : rotB[3] != cad[0] ? rotB[3] : rotB[1];
  const std::vector<int> chordsB = {rotB[0], rotB[1], rotB[2], rotB[3], rotB[0], rotB[1], beforeHalf, cad[0]};

  // ---- the themes
  const int melodyBase = 57 + mod(key - 57, 12);
  auto ending = [&]() -> const Rhythm* { return &pick(ENDINGS(bpb)); };

  /** a phrase of four bars from `bar0` of the section; `copy`: its first two bars as another phrase had them */
  auto phrase = [&](const std::vector<int>& chords, int bar0, const std::array<const Rhythm*, 4>& rhythms, Close close, int start, const std::vector<MelNote>* copy) -> std::vector<MelNote> {
    std::vector<MelNote> out;
    int prev = start;
    // (the note the phrase keeps near: where it begins)
    const int center = copy && copy->size() > 0 ? (*copy)[0].idx : start;
    int last = 0;
    auto push = [&](int bar, double beat, double dur, int idx, bool strong) {
      out.push_back(MelNote{(bar0 + bar) * bpb + beat, dur, idx, strong});
      last = idx - prev;
      prev = idx;
    };
    auto strongAt = [bpb](double beat, double dur) -> bool { return beat == 0 || (bpb == 4 && beat == 2) || dur >= 1.5; };
    for (int bar = 0; bar < 2; bar++) {
      if (copy) {
        for (const MelNote& n : *copy)
          if (static_cast<int>(std::floor(n.beat / bpb)) % 4 == bar) push(bar, std::fmod(n.beat, bpb), n.dur, n.idx, n.strong);
        continue;
      }
      for (const Hit& hit : *rhythms[static_cast<size_t>(bar)]) {
        const double beat = hit[0], dur = hit[1];
        const bool strong = strongAt(beat, dur);
        const int idx = walk(prev, last, strong, chords[static_cast<size_t>(bar)], center);
        push(bar, beat, dur, idx, strong);
      }
    }
    // the third bar: the first bar's figure again, moved onto this bar's chord (a sequence) – unless it is free
    std::vector<MelNote> first;
    for (const MelNote& n : out)
      if (n.beat < (bar0 + 1) * bpb) first.push_back(n);
    if (rhythms[2] == rhythms[0] && first.size() > 0) {
      const int target = snap(prev + (rnd() < 0.5 ? 1 : -1), chords[2], 0);
      const int shift = target - first[0].idx;
      for (const MelNote& n : first) {
        int idx = std::max(LO, std::min(HI, n.idx + shift));
        if (n.strong) idx = snap(idx, chords[2], shift);
        push(2, n.beat - bar0 * bpb, n.dur, idx, n.strong);
      }
    } else {
      for (const Hit& hit : *rhythms[2]) {
        const double beat = hit[0], dur = hit[1];
        const bool strong = strongAt(beat, dur);
        const int idx = walk(prev, last, strong, chords[2], center);
        push(2, beat, dur, idx, strong);
      }
    }
    // the last bar: towards where the phrase ends – home on the tonic, open on the chord's third or fifth
    const Rhythm& end = *rhythms[3];
    const int root = chords[3];
    int goal;
    if (close == Close::Home) {
      // (the tonic nearest between the last note and where the phrase began)
      goal = static_cast<int>(jsRound((prev + center) / 2.0 / 7)) * 7;
      if (goal > HI) goal -= 7;
      if (goal < LO) goal += 7;
    } else {
      // (open: on the chord's third or fifth – the half close on its root or fifth –, near both the last note and
      // where the phrase began, so that the next phrase does not have to leap far to begin again)
      const int want[2] = {close == Close::Open ? 2 : 0, 4};
      int best = prev;
      double bd = std::numeric_limits<double>::infinity();
      for (int j = LO; j <= HI; j++) {
        const int d = mod(j - root, 7);
        const double cost = std::abs(j - prev) + 1.5 * std::abs(j - center);
        if ((d == want[0] || d == want[1]) && cost < bd) {
          bd = cost;
          best = j;
        }
      }
      goal = best;
    }
    for (size_t k = 0; k < end.size(); k++) {
      const double beat = end[k][0], dur = end[k][1];
      if (k == end.size() - 1) push(3, beat, dur, goal, true);
      else {
        // (stepping towards the goal, on a note of the chord where it leans)
        const int dir = sign(goal - prev) != 0 ? sign(goal - prev) : 1;
        int idx = prev + dir * (std::abs(goal - prev) > 2 ? 2 : 1);
        if (idx == goal) idx = goal + dir;
        idx = std::max(LO, std::min(HI, idx));
        push(3, beat, dur, beat == 0 ? snap(idx, root, dir) : idx, beat == 0);
      }
    }
    return out;
  };

  auto section = [&](const std::vector<int>& chords, int start, Close close) -> std::vector<MelNote> {
    // (a motif: two bars of rhythm)
    const Rhythm* m0 = &pick(RHYTHMS(bpb));
    const Rhythm* m1 = &pick(RHYTHMS(bpb));
    const Rhythm* endOne = ending();
    const std::vector<MelNote> one = phrase(std::vector<int>(chords.begin(), chords.begin() + 4), 0, {m0, m1, m0, endOne}, Close::Open, start, nullptr);
    std::vector<MelNote> firstTwo;
    for (const MelNote& n : one)
      if (n.beat < 2 * bpb) firstTwo.push_back(n);
    const int lastOfTwo = firstTwo.size() > 0 ? firstTwo[firstTwo.size() - 1].idx : start;
    // (the second phrase begins as the first – its third bar goes its own way)
    const Rhythm* third = &pick(RHYTHMS(bpb));
    const Rhythm* endTwo = ending();
    const std::vector<MelNote> two = phrase(std::vector<int>(chords.begin() + 4, chords.begin() + 8), 4, {m0, m1, third, endTwo}, close, lastOfTwo, &firstTwo);
    std::vector<MelNote> all = one;
    all.insert(all.end(), two.begin(), two.end());
    return all;
  };
  const int startOfA = snap(2 + static_cast<int>(std::floor(rnd() * 3)), chordsA[0], 1);
  const std::vector<MelNote> themeA = section(chordsA, startOfA, Close::Home);
  // (the second theme begins between where the first ends and where it begins – it follows the one and leads
  // back into the other, so neither has to leap to reach it)
  const int startA = themeA.size() > 0 ? themeA[0].idx : 3;
  const int endA = themeA.size() > 0 ? themeA[themeA.size() - 1].idx : 0;
  const int startOfB = snap(static_cast<int>(jsRound((startA + endA) / 2.0)) + (rnd() < 0.5 ? 0 : 1), chordsB[0], 1);
  const std::vector<MelNote> themeB = section(chordsB, startOfB, Close::Half);

  // ---- the form: intro, sections, ending – shorter at a slow tempo
  const int intro = rnd() < 0.5 ? 1 : 2;
  const bool isLong = (4 * 8 + intro + 2) * barSec <= 170;
  std::vector<Part> parts = {Part{PartKind::Intro, intro, std::vector<int>(static_cast<size_t>(intro), 0), 0.75}};
  const std::vector<PartKind> sections = isLong ? std::vector<PartKind>{PartKind::A, PartKind::A2, PartKind::B, PartKind::A3} : std::vector<PartKind>{PartKind::A, PartKind::B, PartKind::A3};
  auto levelOf = [](PartKind k) -> double { return k == PartKind::A ? 0.9 : k == PartKind::A2 ? 1 : k == PartKind::B ? 0.95 : 0.9; };
  for (PartKind k : sections) parts.push_back(Part{k, 8, k == PartKind::B ? chordsB : chordsA, levelOf(k)});
  parts.push_back(Part{PartKind::Outro, 2, {0, 0}, 0.8});

  std::vector<MusicNote> notes;
  std::vector<int> chords;
  auto human = [&rnd](double t, double amount) -> double { return std::max(0.0, t + (rnd() - 0.5) * amount); };
  auto vary = [&rnd](double v) -> double { return std::min(1.0, v * (0.94 + 0.12 * rnd())); };
  /** the chord on `root` as pitch classes, its notes from the root up (semitones), with its seventh if the piece colours them */
  auto chordPcs = [&](int root, bool seventh) -> std::vector<int> {
    std::vector<int> out;
    for (int k : {0, 2, 4}) out.push_back(mod(key + midi(0, root + k), 12));
    if (seventh) out.push_back(mod(key + midi(0, root + 6), 12));
    return out;
  };
  auto above = [](int lo, int pc) -> int { return lo + mod(pc - lo, 12); };

  int bar = 0;
  for (const Part& part : parts) {
    for (int b = 0; b < part.bars; b++, bar++) {
      const int root = part.chords[static_cast<size_t>(b)];
      chords.push_back(root);
      const double t0 = bar * barSec;
      const double lv = part.kind == PartKind::Intro ? part.level + (0.25 * b) / std::max(1, part.bars - 1) : part.kind == PartKind::A3 ? part.level * (1 - 0.15 * (b / 8.0)) : part.level;
      // held chords: the palette's, and more the second time; merged with the next bar where the chord stays
      const bool pad = palette.pad || part.kind == PartKind::A2 || part.kind == PartKind::Outro;
      const int prevRoot = chords.size() > 1 ? chords[chords.size() - 2] : -1;
      const bool newChord = b == 0 || root != prevRoot;
      if (pad && newChord) {
        int len = 1;
        while (b + len < part.bars && part.chords[static_cast<size_t>(b + len)] == root) len++;
        for (int pc : chordPcs(root, sevenths && part.kind != PartKind::Outro)) {
          const double v = vary(0.3 * lv);
          notes.push_back(MusicNote{t0, len * barSec + 0.1, above(55, pc), v, Instrument::Pad, NoteRole::Chords});
        }
      }
      // broken chords (not in the ending's last bar: it rings out)
      if (palette.arp && !(part.kind == PartKind::Outro && b > 0)) {
        const int r0 = above(52, chordPcs(root, false)[0]);
        const int tones[5] = {r0, r0 + midi(0, root + 2) - midi(0, root), r0 + midi(0, root + 4) - midi(0, root), r0 + 12, r0 + 12 + midi(0, root + 2) - midi(0, root)};
        const double step = eighths ? 0.5 : 1;
        for (size_t j = 0; j < arpPattern.size(); j++) {
          const int k = arpPattern[j];
          const double beat = static_cast<double>(j) * step;
          // (in this order: the moment first, then the strength – each draws a number)
          const double t = human(t0 + beat * spb, 0.012);
          const double v = vary((j == 0 ? 0.5 : 0.4) * lv);
          notes.push_back(MusicNote{t, step * spb, tones[k], v, *palette.arp, NoteRole::Chords});
        }
      }
      // the bass (in from the intro's second bar; one long note in the ending)
      if (!(part.kind == PartKind::Intro && b == 0 && part.bars > 1)) {
        const int rootBass = above(40, chordPcs(root, false)[0]);
        const int fifthBass = rootBass + midi(0, root + 4) - midi(0, root);
        if (part.kind == PartKind::Outro) {
          if (b == 0) {
            const double v = vary(0.55 * lv);
            notes.push_back(MusicNote{t0, 2 * barSec, rootBass, v, Instrument::Bass, NoteRole::Bass});
          }
        } else {
          for (const BassHit& hit : bassPattern) {
            const double beat = hit[0], dur = hit[1];
            const bool which = hit[2] != 0;
            const double v = vary((beat == 0 ? 0.6 : 0.5) * lv);
            notes.push_back(MusicNote{t0 + beat * spb, dur * spb * 0.95, which ? fifthBass : rootBass, v, Instrument::Bass, NoteRole::Bass});
          }
        }
      }
    }
  }

  // the themes on their instruments
  int barAt = intro;
  for (PartKind k : sections) {
    const std::vector<MelNote>& theme = k == PartKind::B ? themeB : themeA;
    const Instrument inst = k == PartKind::A2 ? palette.second : palette.melody;
    const double level = levelOf(k);
    for (size_t j = 0; j < theme.size(); j++) {
      const MelNote& n = theme[j];
      const bool last = j == theme.size() - 1;
      // (the very last note of the piece is held into the ending)
      const double dur = last && k == PartKind::A3 ? n.dur + bpb : n.dur;
      const double t = human((barAt * bpb + n.beat) * spb, 0.016);
      const double v = vary((n.strong ? 0.82 : 0.72) * level);
      notes.push_back(MusicNote{t, dur * spb * 0.92, midi(melodyBase, n.idx), v, inst, NoteRole::Melody});
    }
    barAt += 8;
  }
  // a last bell on the tonic as the ending begins
  notes.push_back(MusicNote{barAt * barSec, barSec, melodyBase + 12, 0.5, Instrument::Bell, NoteRole::Chords});

  // (JavaScript's sort keeps notes of the same moment in the order they were made)
  std::stable_sort(notes.begin(), notes.end(), [](const MusicNote& a, const MusicNote& b) { return a.t < b.t; });
  Piece piece;
  piece.seed = seed;
  piece.bpm = bpm;
  piece.beatsPerBar = bpb;
  piece.key = key;
  piece.mode = mode;
  piece.chords = std::move(chords);
  piece.length = bar * barSec + 4;
  piece.notes = std::move(notes);
  return piece;
}

int scaleDegree(const Piece& piece, int p) {
  const std::array<int, 7>& steps = MODES[piece.mode];
  const int pc = mod(p - piece.key, 12);
  for (size_t i = 0; i < steps.size(); i++)
    if (steps[i] == pc) return static_cast<int>(i);
  return -1;
}

}  // namespace fs
