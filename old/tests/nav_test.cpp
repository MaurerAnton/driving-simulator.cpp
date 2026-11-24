// SPDX-License-Identifier: AGPL-3.0-only
/** Navigation: route hints, roundabout exit counting, target picking. */
#include "../src/game/nav.h"
#include "../src/i18n/index.h"
#include <cassert>
#include <cstdio>

using namespace fs;

namespace {
WorldJson makeNet() {
  WorldJson j;
  j.version = 1;
  j.meta.tileSize = 500;
  auto node = [](double x, double y, double z, int t) {
    NodeJson n;
    n.x = x; n.y = y; n.z = z; n.t = t;
    return n;
  };
  // junction at the origin with three arms (E, S, N) plus the west arm
  j.nodes.push_back(node(0, 0, 0, CTRL_RBL));
  j.nodes.push_back(node(100, 0, 0, CTRL_NONE));
  j.nodes.push_back(node(0, 0, 100, CTRL_NONE));
  j.nodes.push_back(node(0, 0, -100, CTRL_NONE));
  j.nodes.push_back(node(-100, 0, 0, CTRL_NONE));
  auto edge = [](int a, int b, std::vector<double> p, int cls, double w) {
    EdgeJson e;
    e.a = a; e.b = b; e.c = cls; e.p = std::move(p);
    e.w = w; e.lf = 1; e.lb = 1; e.lw = 3.5; e.mf = 50; e.mb = 50;
    return e;
  };
  j.edges.push_back(edge(0, 1, {-100, 0, 0, 100, 0, 0}, RESIDENTIAL, 7.0));
  j.edges.push_back(edge(3, 2, {0, 0, -100, 0, 0, 100}, RESIDENTIAL, 7.0));
  j.edges.push_back(edge(4, 0, {-200, 0, -100, 0, 0}, RESIDENTIAL, 7.0));
  auto arm = [](int e, int r) {
    ArmJson a;
    a.e = e; a.r = r;
    return a;
  };
  j.nodes[0].arms = {arm(0, ROLE_PRIORITY), arm(1, ROLE_PRIORITY), arm(2, ROLE_PRIORITY)};
  j.nodes[1].arms = {arm(0, ROLE_NONE)};
  j.nodes[2].arms = {arm(1, ROLE_NONE)};
  j.nodes[3].arms = {arm(1, ROLE_NONE)};
  j.nodes[4].arms = {arm(2, ROLE_NONE)};
  return j;
}
}  // namespace

int main() {
  World w(makeNet());
  LaneGraph g(w);
  Navigator nav(g);

  // lessons ship with their ids and routes; the texts are the lesson.* ones
  // in every language (0.6.0)
  const auto& ls = lessons();
  // 0.7.0: eight lessons for the car and four for the class-CE truck
  assert(ls.size() == 12);
  assert(ls[0].id == "basics" && ls[0].kind == "basics" && ls[0].vehicle == "car");
  assert(ls[1].id == "rbl" && ls[1].targets.size() == 3);
  assert(ls[7].id == "exam" && ls[7].kind == "exam" && ls[7].vehicle == "car");
  // the truck: three tasks on the practice yard, then the test with all of them
  std::vector<std::string> truck;
  for (const auto& l : ls)
    if (l.vehicle == "truck") truck.push_back(l.id);
  assert((truck == std::vector<std::string>{"ceCouple", "ceCorner", "ceRamp", "ceExam"}));
  assert(ls[8].kind == "yard" && ls[8].tasks == std::vector<std::string>{"couple"});
  assert(ls[9].tasks == std::vector<std::string>{"corner"});
  assert(ls[10].tasks == std::vector<std::string>{"ramp"});
  assert(ls[11].kind == "exam" &&
         (ls[11].tasks == std::vector<std::string>{"couple", "corner", "ramp"}));
  assert(ls[11].targets.size() >= 4);   // a drive in traffic follows the yard
  // the car keeps its own lessons; the routes are for both
  bool carExam = false, sharedRoute = false;
  for (const auto& l : ls) {
    if (l.vehicle == "car" && l.kind == "exam") carExam = true;
    if (l.vehicle.empty() && l.kind == "route") sharedRoute = true;
  }
  assert(carExam && sharedRoute);
  assert(lessonTitle("exam", "de") == "Prüfungsfahrt");
  assert(lessonIntro("exam", "en").find("examiner") != std::string::npos);
  for (const auto& l : ls) {
    assert(!lessonTitle(l.id, "de").empty() && !lessonTitle(l.id, "en").empty());
    assert(!lessonIntro(l.id, "de").empty() && !lessonIntro(l.id, "en").empty());
  }

  // setting a goal plans a route; driving it produces a hint
  Lane& start = *g.edgeLanes[0].fwd[0];   // east-bound through the junction
  Edge& goal = w.edges()[2];              // the western arm
  assert(nav.setGoal(&goal, &start));
  assert(!nav.route.empty());
  assert(nav.route.back()->to->edge == &goal);

  // a hint for the first manoeuvre (the junction is a real one: 3 arms)
  NavHint& h = nav.update(1.0 / 60, &start, 5, -50, 0);
  assert(!h.what.empty());
  assert(h.dist >= 0);   // we are on the way, not arrived

  // arriving: the goal edge itself reports done (as does being within 7 m of it)
  Lane& goalLane = *g.edgeLanes[2].fwd[0];
  nav.update(1.0 / 60, &goalLane, 1, -150, 0);
  assert(nav.hint.done && nav.hint.what == "routeDone");

  // clearing drops the route
  nav.clear();
  assert(nav.route.empty() && nav.goal == nullptr && nav.routeLanes.empty());

  // findTarget: 'home' returns the home edge, others pick a reachable edge
  std::set<Edge*> avoid;
  assert(findTarget(w, g, TargetType::Home, 0, 0, start, &goal, avoid) == &goal);
  Edge* t = findTarget(w, g, TargetType::Rbl, -50, 0, start, nullptr, avoid);
  // this little network has no 3-arm rechts-vor-links junction with tier-2 arms
  assert(t == nullptr || LaneGraph::drivable(*t, true));

  // an avoided edge is never chosen
  avoid.insert(&w.edges()[1]);
  Edge* t2 = findTarget(w, g, TargetType::Priority, -50, 0, start, nullptr, avoid);
  if (t2) assert(t2 != &w.edges()[1]);

  std::printf("nav: all checks passed (%zu lessons, route of %zu connectors)\n",
              ls.size(), nav.route.size());
  return 0;
}
