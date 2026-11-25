// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/theory.ts @ v1.7.7
/**
 * Theory lessons: difficult situations of German road traffic shown as short
 * scenes in 3D, the way a theory book draws them – a crossroads without
 * signs, a priority road that bends, turning left with oncoming traffic, a
 * zebra crossing, a tram, an ambulance in a jam, a school bus at its stop,
 * a cyclist to overtake, the blind spot, a lane that ends, joining the
 * motorway, a roundabout, the green arrow.
 *
 * Each lesson is a script: a place (game/theorystage.ts) and the vehicles,
 * cyclists and people on it, each moving along a path of the place by
 * timed key positions.  At the key moments the scene stops and asks a
 * question – who goes first, what do you do – which is answered by tapping
 * a vehicle or an answer; then it tells why (with the paragraph of the
 * StVO) and plays on the right way.  "You" are always the white
 * driving-school car coming from the south.
 *
 * All of this is plain data and arithmetic: render/theory.ts draws it on
 * the practice yard, game/theorymode.ts runs it, ui/theory.ts shows the
 * texts – and tests/game/theory.test.ts checks the scripts without a browser.
 */
#pragma once

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "game/theorystage.h"

namespace fs::theory {

// ------------------------------------------------------------------- actors
/** the learner's car; another car; an ambulance or police van; a bus; a lorry; a tram; a cyclist; a person; a child; a deer; a wild boar
 *  ('me' | 'car' | 'van' | 'bus' | 'truck' | 'tram' | 'bike' | 'person' | 'child' | 'deer' | 'boar') */
using ActorKind = std::string;

/**
 * How big each kind is: length and width, how far its nose lies ahead of
 * the middle between its axles (the point the models are placed by), its
 * wheelbase.  The scripts move the NOSE along a path: a car waits "at the
 * line" with its nose there.
 */
struct Footprint {
  double length = 0;
  double width = 0;
  double front = 0;
  double wheelbase = 0;
};

/** FOOT[kind] */
const Footprint& FOOT(const ActorKind& kind);

/** A key position: at time `t` (s) the nose is `at` along the path – metres, or a mark of the path ± metres ('line-12') – going `v` m/s. */
struct Key {
  double t = 0;
  std::variant<double, std::string> at;
  double v = 0;
  Key(double t_, double at_, double v_) : t(t_), at(at_), v(v_) {}
  Key(double t_, int at_, double v_) : t(t_), at(static_cast<double>(at_)), v(v_) {}
  Key(double t_, const char* at_, double v_) : t(t_), at(std::string(at_)), v(v_) {}
  Key(double t_, std::string at_, double v_) : t(t_), at(std::move(at_)), v(v_) {}
};

/** [t, off, dur]: moving sideways from its path – from `t` on, to `off` metres (+ to the left), taking `dur` seconds (also: [t, yaw, dur] of `face`) */
using TimedMove = std::array<double, 3>;

/** a police officer directing the traffic, a flagman at roadworks, a worker there, somebody old walking with a rollator ('police' | 'flagman' | 'worker' | 'senior') */
using PersonRole = std::string;
/** a police officer's arms: down (the basic position), both out to the sides, one raised; a flagman's sign: red, green ('down' | 'arms' | 'raise' | 'stop' | 'go') */
using Gesture = std::string;

struct ActorDef {
  std::string id;
  ActorKind kind;
  /** the name of its path in the stage */
  std::string path;
  std::vector<Key> keys;
  /** its paint (cars), the shirt of a cyclist */
  std::optional<int> colour{};
  /** a person's look (render/people.ts) */
  std::optional<int> look{};
  /** moving sideways from its path: from `t` on, to `off` metres (+ to the left), taking `dur` seconds */
  std::vector<TimedMove> side{};
  /** the indicators from `t` on: −1 left, 0 off, 1 right, 2 hazard lights */
  std::vector<std::pair<double, int>> blink{};
  /** blue lights (an ambulance, the police) */
  bool blue = false;
  /** what it is called in the answers (a text key; "": by its kind) */
  std::string name{};
  /** the destination display of a bus */
  std::string display{};
  /** the vehicle it gets out of (it starts inside it: the two do not count as touching) */
  std::string leaves{};
  /** a person at work or of an age (game/theoryscenes.ts): a police officer, a flagman with his stop/go sign, a worker, somebody old with a rollator ("": none) */
  PersonRole role{};
  /** which way a person faces from `t` on (a yaw of the stage), turning over `dur` seconds – instead of the way its path goes */
  std::vector<TimedMove> face{};
  /** a person's signs from `t` on: a police officer's arms ('down', 'arms', 'raise'), a flagman's sign ('stop', 'go') */
  std::vector<std::pair<double, Gesture>> gesture{};
  /** its headlights from `t` on, drawn on the road in the dark: 0 off, 1 dipped, 2 full beam */
  std::vector<std::pair<double, int>> beam{};
};

/** A traffic light's lamps: R red, Y red and amber, G green, A amber, O off. */
using LightState = char;

struct StepDef {
  /** when the scene stops for the question (s) */
  double at = 0;
  /** the answers: ids of actors (tapped in the picture, or in the list) or of answers of their own (their text: theory.<lesson>.<step>.<id>; '!yes' and '!no': theory.a.yes / no) */
  std::vector<std::string> answers;
  std::string right;
  /** the rule (the same in every language) */
  std::string ref;
  /** the question, when it is a common one (else theory.<lesson>.<step>.ask; "": not given) */
  std::string ask{};
  /** the blind spot of this actor is shown while the question is open ("": none) */
  std::string blind{};
};

/**
 * The camera at the start: what it looks at (a point of the stage, or an
 * actor it follows – then `ahead` metres in front of it), from which side
 * (yaw: 0 from the south, + from the east), how steeply and from how far.
 */
struct LessonView {
  std::variant<std::array<double, 2>, std::string> at;
  std::optional<double> ahead{};
  double yaw = 0;
  double pitch = 0;
  double dist = 0;
};

/** the side gap between two actors shown while one passes the other */
struct LessonGap {
  std::string a;
  std::string b;
  double t0 = 0;
  double t1 = 0;
};

/** its own sky (game/theorymode.ts): the sun's elevation (degrees – below 0 dusk and night) and fog (how far one sees, m) */
struct LessonSky {
  std::optional<double> sun{};
  std::optional<double> fog{};
};

struct LessonDef {
  std::string id;
  std::function<Stage()> stage;
  std::vector<ActorDef> actors;
  std::vector<StepDef> steps;
  /** when it is over (s) */
  double end = 0;
  std::map<std::string, std::vector<std::pair<double, LightState>>> lights{};
  LessonView view{};
  std::optional<LessonGap> gap{};
  std::optional<LessonSky> sky{};
};

/** Every lesson, in the order of the list (and the scenes of game/theoryscenes.ts after them). */
const std::vector<LessonDef>& THEORY();

/** The lesson of this id (null: none). */
const LessonDef* theoryLesson(const std::string& id);

/** The mode of the main menu that opens the theory lessons. */
inline const std::string THEORY_MODE = "theory";

// ------------------------------------------------------------------- texts
/** The text keys of a lesson and its steps (n: the step's number, from 1). */
namespace theoryKeys {
std::string title(const std::string& id);
std::string intro(const std::string& id);
std::string text(const std::string& id, int n);
std::string ask(const std::string& id, int n, const StepDef& step);
std::string why(const std::string& id, int n);
}  // namespace theoryKeys

/** The text key of an answer: an actor's name, a common answer ('!yes'), or one of the step's own. */
std::string answerKey(const LessonDef& lesson, int n, const std::string& answer);

/** What an actor is called (a text key). */
std::string actorName(const ActorDef& a);

/** Every text key the lessons use (for the tests). */
std::vector<std::string> allTheoryKeys();

// ------------------------------------------------------------------ motion
struct ResolvedKey {
  double t = 0;
  double s = 0;
  double v = 0;
};

/** An actor of a lesson with its path and its keys as numbers. */
struct ActorRun {
  const ActorDef* def = nullptr;
  const Path* path = nullptr;
  Footprint foot;
  std::vector<ResolvedKey> keys;
};

/** A lesson made ready to play: its place and its actors (not copied: the actors point into its stage). */
struct Scene {
  const LessonDef* lesson = nullptr;
  Stage stage;
  std::vector<ActorRun> actors;

  Scene() = default;
  Scene(const Scene&) = delete;
  Scene& operator=(const Scene&) = delete;
};

/** A place on a path by its mark, ± metres ('line-12', 'pass:S+4'), or metres. */
double markAt(const Path& path, const std::variant<double, std::string>& at);

/** Build a lesson's place and resolve its actors' keys. */
std::shared_ptr<Scene> prepare(const LessonDef& lesson);

/** Where along its path an actor's nose is at time t, how fast it goes and how it speeds up. */
struct Progress {
  double s = 0;
  double v = 0;
  double a = 0;
  /** before its first key: not there yet */
  bool started = false;
};

/**
 * The nose along the path at time t: between two keys a smooth curve
 * (cubic Hermite) through both positions with both speeds; before the first
 * key it waits there, after the last it goes on at the last speed.
 */
Progress& progressAt(const std::vector<ResolvedKey>& keys, double t, Progress& out);

struct SideAt {
  double off = 0;
  double rate = 0;
};

/** How far an actor is moved sideways from its path at time t (+ to the left), and how fast that changes. */
SideAt sideAt(const ActorDef& def, double t);

/** An actor's indicators at time t: −1 left, 0 off, 1 right, 2 hazard lights. */
int blinkAt(const ActorDef& def, double t);

/** Where an actor is and how it stands at a moment. */
struct Pose {
  bool visible = false;
  /** the point the model is placed by (the middle between the axles; a person's feet) */
  double x = 0;
  double z = 0;
  double yaw = 0;
  /** the nose along the path */
  double s = 0;
  double v = 0;
  double a = 0;
  /** the front wheels' angle (+ to the right, like the cars' models) */
  double steer = 0;
  int blink = 0;
};

inline Pose newPose() { return Pose{}; }

/**
 * When the nose was at `s` (a moment no later than `t`; the scripts never
 * go backwards) – found by halving the interval.  Before the first key: then.
 */
double timeAt(const std::vector<ResolvedKey>& keys, double s, double t);

/** An actor at time t. */
Pose& poseAt(const ActorRun& r, double t, Pose& out);

/**
 * The four corners of an actor's outline at a pose (x, z, …), made `grow`
 * metres larger all round: a vehicle from its nose to its tail, a tram back
 * from its nose, a person round its feet.
 */
std::vector<double> corners(const ActorRun& r, const Pose& p, double grow = 0);

/** Do two convex outlines (x, z, …) overlap? (separating axes) */
bool outlinesTouch(const std::vector<double>& a, const std::vector<double>& b);

/** A traffic light's state at time t. */
LightState lightAt(const Scene& scene, const std::string& id, double t);

/** The height of an actor's ground above the map's: on the road, or on a pavement or island it walks over. */
double actorLift(const Scene& scene, const ActorRun& r, double x, double z);

// -------------------------------------------------------------------- a run
/** playing; stopped at a question; told whether the answer was right; over ('play' | 'ask' | 'told' | 'end') */
using Phase = std::string;

/**
 * One go at a lesson: its clock, the step it is at, the answers given.  The
 * clock stands still while a question is open and while the answer is
 * explained; `goOn` lets the scene play on – the right way – to the next
 * question or the end.
 */
class TheoryRun {
 public:
  const std::shared_ptr<Scene> scene;
  double t = 0;
  /** the step that is asked next (or now) */
  int step = 0;
  Phase phase = "play";
  /** the answer given to the open step (nullopt: none) */
  std::optional<std::string> picked;
  /** how many were answered right the first time */
  int right = 0;

  explicit TheoryRun(std::shared_ptr<Scene> s) : scene(std::move(s)) {}

  const StepDef* current() const;

  /** On by dt seconds while playing: true when it stopped (a question, or the end). */
  bool advance(double dt);

  /** The answer to the open question: right or not (nullopt: no question is open). */
  std::optional<bool> answer(const std::string& id);

  /** Play on after the explanation. */
  void goOn();

  void restart();

  /** On to time t at once (for pictures of a moment): the questions before it count as answered right; one asked exactly then is open. */
  void skipTo(double t);
};

}  // namespace fs::theory
