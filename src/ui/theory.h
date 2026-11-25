// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/theory.ts @ v1.7.7
/**
 * The theory lessons on the screen (game/theorymode.ts runs them): the list
 * of the situations to choose from, and during a lesson a bar at the foot of
 * the screen – what is going on, the question with its answers, whether the
 * answer was right and why (with the paragraph of the StVO, linked to its
 * text), and the buttons to go on, to see it again, to the next one.  The
 * middle of the screen stays free: the vehicles there are tapped to answer.
 * Plain DOM, like the rest of the interface.
 *
 * (game/theorymode.ts, which drives it, is not in this version yet: it is the theory
 * scenes' – this panel is ready for it.)
 */
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "i18n/index.h"
#include "platform/dom.h"

namespace fs {

/** One situation of the list. */
struct TheoryItem {
  std::string id;
  std::string title;
  std::string intro;
  /** answered all right once */
  bool done = false;
};

struct TheoryAnswer {
  std::string id;
  std::string label;
};

/** What the bar shows. */
struct TheoryBar {
  /** the lesson's number and how many there are */
  int n = 0;
  int count = 0;
  std::string title;
  /** being laid out; playing (the introduction); a question; the answer explained; the end ('loading' | 'play' | 'ask' | 'told' | 'end') */
  std::string phase;
  std::string text;
  std::string ask;
  std::vector<TheoryAnswer> answers;
  /** (nullopt: none picked) */
  std::optional<std::string> picked;
  std::string right;
  std::string why;
  std::string ref;
  /** the end: how many were right */
  std::string score;
  bool hasNext = false;
};

struct TheoryBarActions {
  std::function<void(const std::string& id)> answer;
  std::function<void()> goOn;
  std::function<void()> again;
  std::function<void()> next;
  std::function<void()> list;
};

class TheoryPanel {
 public:
  using T = std::function<std::string(const std::string& key, const TrParams& vars)>;

  TheoryPanel(const dom::El& root, T t);

  /** The situations to choose from (over the whole screen). */
  void showList(const std::vector<TheoryItem>& items, std::function<void(const std::string& id)> pick, std::function<void()> close);

  /** The bar of a lesson, as it stands now (drawn anew: the controller calls this only when something changed). */
  void show(const TheoryBar& v, const TheoryBarActions& act);

  void hide();

 private:
  dom::El root;
  T t;
  dom::El bar;
  dom::El list;

  /** The rule, as German law writes it (left to right in every language), each paragraph a link to its official text. */
  dom::El rule(const std::string& ref) const;
};

}  // namespace fs
