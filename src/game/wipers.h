// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/wipers.ts @ v1.7.7
/**
 * The windscreen in rain and snow, and the wipers that keep it clear.
 *
 * The stalk has four places – off, intermittent, normal, fast – and the
 * washer, which sprays while it is held and lets the wipers run on for two
 * more sweeps after it is let go (as on a real car).  Each wiper is an arm
 * turning about a pivot at the foot of the glass; its blade clears what it
 * passes over.
 *
 * What lies on the glass is kept in a coarse grid over it (in metres along
 * the glass: x from its left edge as the driver sees it, y up from its foot):
 *   - drops: how thickly drops sit there, 0 … 1 – rain lays them down, faster
 *     when driving into it; the renderer draws the drops themselves;
 *   - film: the sheet of water once the drops run together – it blurs the view;
 *   - snow: flakes that settle and build up into a white cover;
 *   - dirt: the spray of a wet road; wiped wet it goes, wiped dry it smears;
 *   - smear: the streaks of dirt wiped over a dry screen, until it is washed.
 * Warm air melts the snow into water; a dry screen dries what is on it.
 *
 * Nothing here draws or knows the engine: render/windscreen.h draws it, the
 * game feeds it the weather and the speed.  Kept apart so that it can be tested.
 */
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace fs {

/** 'off' | 'intermittent' | 'normal' | 'fast' */
using WiperMode = std::string;
extern const std::vector<WiperMode> WIPER_MODES;

/** The stalk one place on: off → intermittent → normal → fast → off. */
WiperMode nextWiperMode(const WiperMode& m);

/** seconds for one sweep, up and back down (normal about 50 a minute, fast about 80) */
struct SweepSeconds {
  double normal;
  double fast;
};
inline constexpr SweepSeconds SWEEP_SECONDS = {1.2, 0.75};
/** the pause between two sweeps on intermittent (s) */
inline constexpr double INTERVAL_PAUSE = 4;
/** sweeps after the washer is let go */
inline constexpr int WASH_AFTER = 2;

/** Where along its sweep a blade is, 0 parked … 1 at the far end, at a point of the cycle (0 … 1). */
double sweepAt(double phase);

/**
 * The stalk and the motor.  Off stops the wipers only once they are back
 * in their place at the foot of the glass, as a real motor does.
 */
class Wipers {
 public:
  WiperMode mode = "off";
  /** point of the sweep cycle, 0 (parked) … 1 */
  double phase = 0;
  /** what the blades passed over in the last step: from … to (0 parked, 1 far end) */
  struct Swept {
    double lo = 0;
    double hi = 0;
    bool any = false;
  } swept;

  /** The stalk one place on (the key, the button); returns the new place. */
  WiperMode cycle();
  void set(const WiperMode& mode);
  /** the blades' place now, 0 parked … 1 at the far end */
  double angle() const { return sweepAt(phase); }
  /** Are the blades moving (or about to)? */
  bool running() const { return phase > 0; }
  /** Runs the motor for `dt` seconds, the washer held or not; fills `swept`. */
  void step(double dt, bool washer);

 private:
  /** seconds until the next sweep on intermittent */
  double pause_ = 0;
  /** sweeps still to come after the washer */
  int after_ = 0;
};

/** One wiper: its pivot (m, in the glass), the reach of its blade, where it lies parked and how far it turns. */
struct WiperArm {
  double px = 0;
  double py = 0;
  /** the blade from rIn to rOut from the pivot */
  double rIn = 0;
  double rOut = 0;
  /** direction when parked (rad, 0 = to the right, counter-clockwise up) */
  double park = 0;
  /** how far it turns (rad, + = counter-clockwise) */
  double sweep = 0;
};

/**
 * Two wipers side by side as on most cars, both parked pointing to the
 * right and sweeping up and over to the left: the left one clears the
 * driver's side up to the pillar, the right one the middle and the right.
 */
std::vector<WiperArm> tandemArms(double width, double height);

/** The weather at the glass and how the vehicle moves through it. */
struct ScreenWeather {
  /** rain, 0 … 1 (rainIntensity) */
  double rain = 0;
  /** snowfall, 0 … 1 (snowIntensity) */
  double snow = 0;
  /** air temperature (°C) */
  double temperature = 0;
  /** how wet the road is, 0 … 1 – driving on it throws up dirty spray */
  double roadWet = 0;
  /** speed forwards (m/s; backwards counts as standing) */
  double speed = 0;
  /** the wind across the glass from left to right (m/s), the vehicle's own airflow not included */
  double crossWind = 0;
  /** the engine runs: warm air on the glass from inside */
  bool heater = false;
};

// (the original's `seeded` – mulberry32 – is audio/waves.h's seeded(): the same generator, one name in C++)

/**
 * What lies on one windscreen: the grid of drops, water, snow and dirt over
 * it, and its wipers.  `width` × `height` metres along the glass.
 */
class Windscreen {
 public:
  const int cols;
  const int rows;
  const double width;
  const double height;
  std::vector<float> drops;
  std::vector<float> film;
  std::vector<float> snow;
  std::vector<float> dirt;
  std::vector<float> smear;
  /** how often each cell has been wiped (counts round at 256): the renderer lays new drops in new places after each wipe */
  std::vector<uint8_t> gen;
  Wipers wipers;
  const std::vector<WiperArm> arms;
  /** where the running drops have got to (m): the renderer moves its running drops by it */
  struct Flow {
    double x = 0;
    double y = 0;
  } flow;
  /** share of the drops that run (driving fast), 0 … 1 */
  double running = 0;

  Windscreen(double width, double height, int cols = 48, int rows = 24, std::optional<std::vector<WiperArm>> arms = std::nullopt, uint32_t seed = 1);

  /** The cell at a point of the glass (m), or −1 off it. */
  int cellAt(double x, double y) const;
  /** Is the cell ever reached by a blade? */
  bool wiped(int cell) const;
  /** Each blade's place along the glass now: its direction (rad) for the renderer. */
  double armAngle(int k) const;
  /**
   * One step of `dt` seconds: what falls on the glass, runs off it, melts or
   * dries – then what the blades pass over, with the washer held or not.
   */
  void step(double dt, const ScreenWeather& w, bool washer = false);
  /** The blades passed from `lo` to `hi` of their sweep: clear what lies there. */
  void wipe(double lo, double hi);
  /**
   * How much of the view through the glass is lost, 0 clear … 1 none left –
   * over the part the driver looks through (the left two thirds, from a
   * quarter up to near the top).
   */
  double obscured() const;
  /** Everything off the glass (a new vehicle, a fresh start). */
  void clear();

 private:
  /** where along each arm's sweep each cell lies (0 … 1), −1 where its blade never comes */
  std::vector<std::vector<float>> reach_;
  /** how readily snow settles on each cell (patches: 0.6 … 1.4) */
  std::vector<float> patch_;
};

}  // namespace fs
