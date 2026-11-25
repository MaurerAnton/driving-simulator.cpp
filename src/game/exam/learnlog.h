// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/exam/learnlog.ts @ v1.7.7
/**
 * The lesson log – "where am I?": every practical lesson, every basic task
 * of class B (game/drills.ts), every theory scene, the theory exams, the sign
 * trainer and "spot the danger", each with its state – not tried, tried, or
 * passed (with stars and the date) – gathered from what the browser keeps:
 * the learning store (learnstore.ts), the theory scenes passed
 * (game/theorymode.ts), the log of faults (a lesson with faults was at
 * least tried: game/faultlog.ts) and the tours (game/tourlog.ts).
 *
 * A pure function of those (tests/game/learn.test.ts); ui/learn.ts shows it.
 */
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "game/exam/learnstore.h"

namespace fs {

/** 'none' | 'tried' | 'passed' */
using LogState = std::string;

struct LogRow {
  std::string id;
  LogState state;
  /** 0–3 (passed only; nullopt: a lesson that has no stars) */
  std::optional<double> stars;
  /** when it was passed – or last tried (ms; 0: never) */
  double date = 0;
  /** tries so far */
  double tries = 0;
};

struct LogSection {
  /** 'lessons' | 'drills' | 'theory' | 'danger' | 'exams' */
  std::string id;
  std::vector<LogRow> rows;
  /** passed, of how many */
  int passed = 0;
  int total = 0;
};

struct LogFault {
  std::string mode;
  double t = 0;
};

struct LogTour {
  std::string date;
  bool done = false;
};

struct LogInput {
  LearnData learn;
  /** the practical lessons and the basic tasks, in their order */
  std::vector<std::string> lessons;
  std::vector<std::string> drills;
  /** the theory scenes, and those answered all right */
  std::vector<std::string> theory;
  std::vector<std::string> theoryDone;
  /** the "spot the danger" items */
  std::vector<std::string> danger;
  /** the faults of all drives: in which mode (lesson id), when */
  std::vector<LogFault> faults;
  /** the tours: when (ISO), driven to the end */
  std::vector<LogTour> tours;
  double now = 0;
};

struct LearnLog {
  std::vector<LogSection> sections;
  struct Exams {
    int taken = 0;
    int passed = 0;
    double best = 0;
    double max = 0;
    double last = 0;
    bool operator==(const Exams&) const = default;
  } exams;
  SignProgress signs;
  struct Tours {
    int driven = 0;
    int done = 0;
    double last = 0;
    bool operator==(const Tours&) const = default;
  } tours;
};

LearnLog learnLog(const LogInput& o);

}  // namespace fs
