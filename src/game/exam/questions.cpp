// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/exam/questions.ts @ v1.7.7
#include "game/exam/questions.h"

namespace fs {

namespace {

ExamPicture signPic(const char* sign) {
  ExamPicture p;
  p.sign = sign;
  return p;
}

ExamPicture scenePic(const char* scene, int step) {
  ExamPicture p;
  p.scene = scene;
  p.step = step;
  return p;
}

/** a question with answers to tick */
ExamQuestion pick(const char* id, const char* topic, int points, int answers, std::vector<int> correct, const char* ref, std::optional<ExamPicture> picture = std::nullopt) {
  return ExamQuestion{.id = id, .topic = topic, .points = points, .answers = answers, .correct = std::move(correct), .number = std::nullopt, .ref = ref, .picture = std::move(picture)};
}

/** a question answered with a number */
ExamQuestion num(const char* id, const char* topic, int points, double value, const char* unit, const char* ref, std::optional<ExamPicture> picture = std::nullopt) {
  return ExamQuestion{.id = id, .topic = topic, .points = points, .answers = 0, .correct = {}, .number = ExamNumber{value, unit}, .ref = ref, .picture = std::move(picture)};
}

}  // namespace

namespace detail {

const std::vector<ExamQuestion>& examQuestions() {
  // (outlives everything: read by static tables of other files at any time)
  static const auto* list = new std::vector<ExamQuestion>{
    pick("dan01", "danger", 5, 3, {0, 2}, "§ 3 Abs. 2a StVO"),
    pick("dan02", "danger", 4, 3, {0, 2}, "§ 3 Abs. 2a StVO"),
    pick("dan03", "danger", 4, 3, {0, 1}, "§ 3 Abs. 1 StVO"),
    pick("dan04", "danger", 4, 3, {0}, "§ 3 Abs. 1 StVO"),
    pick("dan05", "danger", 4, 3, {0, 2}, "§ 3 Abs. 1 StVO"),
    pick("dan06", "danger", 4, 3, {0, 2}, "§ 1 Abs. 2 StVO"),
    pick("dan07", "danger", 5, 3, {0, 2}, "§ 3 Abs. 2a StVO"),
    pick("dan08", "danger", 4, 3, {0}, "§ 3 Abs. 1 StVO"),
    pick("dan09", "danger", 4, 3, {0, 1}, "Anlage 1 StVO, Zeichen 142"),
    pick("dan10", "danger", 4, 3, {0, 1}, "§ 1 Abs. 2 StVO"),
    pick("dan11", "danger", 5, 3, {0, 2}, "§ 14 Abs. 1 StVO"),
    pick("dan12", "danger", 4, 3, {0, 2}, "§ 3 Abs. 1 StVO"),
    pick("dan13", "danger", 4, 3, {0, 1}, "§ 9 Abs. 3 StVO"),
    pick("dan14", "danger", 5, 3, {0, 1}, "§ 3 Abs. 2a StVO"),
    pick("dan15", "danger", 4, 3, {0, 1}, "§ 3 Abs. 1, § 17 Abs. 3 StVO"),
    pick("dan16", "danger", 5, 3, {0}, "§ 8 Abs. 1 StVO", scenePic("rbl", 0)),
    pick("beh01", "behaviour", 4, 3, {0, 2}, "§ 38 Abs. 1 StVO"),
    pick("beh02", "behaviour", 5, 3, {0}, "§ 11 Abs. 2 StVO"),
    pick("beh03", "behaviour", 4, 3, {0}, "§ 7 Abs. 4 StVO"),
    pick("beh04", "behaviour", 4, 3, {0}, "§ 2 Abs. 2 StVO"),
    pick("beh05", "behaviour", 5, 3, {0}, "§ 9 Abs. 3 StVO"),
    pick("beh06", "behaviour", 4, 3, {0, 1}, "§ 9 Abs. 5 StVO"),
    pick("beh07", "behaviour", 4, 3, {0}, "§ 23 Abs. 1a StVO"),
    pick("beh08", "behaviour", 5, 3, {0, 1}, "§ 323c StGB"),
    pick("beh09", "behaviour", 4, 3, {0, 1}, "§ 34 Abs. 1 Nr. 6 StVO, § 142 StGB"),
    pick("beh10", "behaviour", 3, 3, {0, 1}, "§ 16 Abs. 1 StVO"),
    pick("beh11", "behaviour", 4, 3, {0, 1, 2}, "§ 7 Abs. 5 StVO"),
    pick("beh12", "behaviour", 5, 3, {0}, "§ 18 Abs. 7 StVO"),
    pick("beh13", "behaviour", 4, 3, {0}, "§ 18 Abs. 3 StVO"),
    pick("beh14", "behaviour", 4, 3, {0}, "§ 21 Abs. 1a StVO"),
    pick("beh15", "behaviour", 3, 3, {0}, "§ 8 Abs. 1a, § 9 Abs. 1 StVO"),
    pick("beh16", "behaviour", 4, 3, {0, 1}, "§ 20 Abs. 4 StVO"),
    pick("pri01", "priority", 5, 3, {0}, "§ 8 Abs. 1 StVO"),
    pick("pri02", "priority", 4, 3, {0}, "Anlage 2 StVO, Zeichen 205", signPic("205")),
    pick("pri03", "priority", 5, 3, {0, 1}, "Anlage 2 StVO, Zeichen 206", signPic("206")),
    pick("pri04", "priority", 4, 3, {0}, "Anlage 3 StVO, Zeichen 301", signPic("301")),
    pick("pri05", "priority", 4, 3, {0, 1}, "Anlage 3 StVO, Zeichen 306", signPic("306")),
    pick("pri06", "priority", 5, 3, {0}, "§ 36 Abs. 1, Abs. 2 StVO"),
    pick("pri07", "priority", 5, 3, {0}, "§ 9 Abs. 3 StVO"),
    pick("pri08", "priority", 4, 3, {0}, "§ 9 Abs. 4 StVO"),
    pick("pri09", "priority", 5, 3, {0}, "§ 10 StVO"),
    pick("pri10", "priority", 4, 2, {0}, "§ 9 Abs. 3 StVO"),
    pick("pri11", "priority", 4, 2, {0}, "§ 8 Abs. 1a StVO, Anlage 2 StVO, Zeichen 215", signPic("215")),
    pick("pri12", "priority", 5, 3, {0}, "§ 37 Abs. 2 Nr. 1 StVO", signPic("720")),
    pick("pri13", "priority", 4, 3, {0, 1}, "Anlage 3 StVO, Zeichen 306 mit Zusatzzeichen", signPic("1002-10")),
    pick("pri14", "priority", 4, 2, {0}, "Anlage 2 StVO, Zeichen 208", signPic("208")),
    pick("sig01", "signs", 3, 3, {0}, "Anlage 2 StVO, Zeichen 274", signPic("274-50")),
    pick("sig02", "signs", 4, 3, {0, 2}, "Anlage 2 StVO, Zeichen 276", signPic("276")),
    pick("sig03", "signs", 3, 2, {0}, "Anlage 2 StVO, Zeichen 283", signPic("283")),
    pick("sig04", "signs", 3, 3, {0, 1}, "Anlage 2 StVO, Zeichen 286", signPic("286")),
    pick("sig05", "signs", 3, 2, {0}, "Anlage 2 StVO, Zeichen 267", signPic("267")),
    pick("sig06", "signs", 4, 3, {0, 1, 2}, "Anlage 3 StVO, Zeichen 325.1", signPic("325.1")),
    pick("sig07", "signs", 3, 2, {0}, "§ 45 Abs. 1c StVO, Anlage 2 StVO, Zeichen 274.1", signPic("274.1")),
    pick("sig08", "signs", 3, 2, {0}, "Anlage 1 StVO, Zeichen 151", signPic("151")),
    pick("sig09", "signs", 3, 2, {0}, "Anlage 1 StVO, Zeichen 136", signPic("136-10")),
    pick("sig10", "signs", 3, 3, {0}, "Anlage 2 StVO, Zeichen 239", signPic("239")),
    pick("sig11", "signs", 3, 2, {0}, "Anlage 2 StVO, Zeichen 237", signPic("237")),
    pick("sig12", "signs", 4, 3, {0}, "Anlage 2 StVO, Zeichen 224 (Ge- oder Verbot)", signPic("224")),
    pick("sig13", "signs", 3, 3, {0}, "Anlage 3 StVO, Zeichen 330.1, § 18 Abs. 1 StVO", signPic("330.1")),
    pick("sig14", "signs", 3, 2, {0}, "Anlage 2 StVO, Zeichen 282", signPic("282")),
    pick("sig15", "signs", 3, 2, {0}, "Anlage 2 StVO, Zeichen 274 mit Zusatzzeichen 1052-36", signPic("1052-36")),
    pick("sig16", "signs", 4, 3, {0}, "Anlage 2 StVO, Zeichen 250, Zusatzzeichen 1020-30", signPic("1020-30")),
    pick("sig17", "signs", 3, 2, {0}, "Anlage 3 StVO, Zeichen 357", signPic("357")),
    pick("spd01", "speed", 4, 3, {0}, "§ 3 Abs. 3 StVO"),
    pick("spd02", "speed", 4, 3, {0}, "§ 3 Abs. 3 StVO"),
    pick("spd03", "speed", 3, 3, {0}, "Autobahn-Richtgeschwindigkeits-Verordnung"),
    pick("spd04", "speed", 5, 3, {0}, "§ 3 Abs. 3 Nr. 2 StVO"),
    num("spd05", "speed", 4, 50, "km/h", "§ 3 Abs. 4 StVO"),
    num("spd06", "speed", 3, 15, "m", "§ 3 Abs. 1 StVO (Faustformel)"),
    num("spd07", "speed", 3, 25, "m", "§ 3 Abs. 1 StVO (Faustformel)"),
    num("spd08", "speed", 4, 130, "m", "§ 3 Abs. 1 StVO (Faustformel)"),
    pick("spd09", "speed", 4, 3, {0}, "§ 3 Abs. 1 StVO"),
    num("spd10", "speed", 4, 40, "m", "§ 4 Abs. 1 StVO"),
    num("spd11", "speed", 4, 50, "km/h", "§ 3 Abs. 1 Satz 3 StVO"),
    pick("spd12", "speed", 4, 3, {0, 1}, "§ 3 Abs. 1 StVO"),
    pick("spd13", "speed", 3, 2, {0}, "§ 3 Abs. 2 StVO"),
    pick("spd14", "speed", 5, 3, {0, 1}, "§ 26 Abs. 1 StVO", signPic("350")),
    pick("ovt01", "overtaking", 4, 3, {0}, "§ 5 Abs. 1 StVO"),
    pick("ovt02", "overtaking", 5, 3, {0, 2}, "§ 5 Abs. 2, Abs. 3 StVO"),
    num("ovt03", "overtaking", 4, 1.5, "m", "§ 5 Abs. 4 StVO"),
    num("ovt04", "overtaking", 4, 2, "m", "§ 5 Abs. 4 StVO"),
    pick("ovt05", "overtaking", 5, 2, {0}, "§ 5 Abs. 6 StVO"),
    pick("ovt06", "overtaking", 4, 3, {0, 1}, "§ 5 Abs. 7 Satz 1, § 7 Abs. 3 StVO"),
    pick("ovt07", "overtaking", 4, 2, {0}, "§ 19 Abs. 1 Satz 3 StVO"),
    pick("ovt08", "overtaking", 4, 3, {0, 1}, "§ 5 Abs. 4, Abs. 4a StVO"),
    pick("ovt09", "overtaking", 4, 2, {0}, "§ 5 Abs. 7 Satz 2 bis 4 StVO"),
    pick("ovt10", "overtaking", 5, 2, {0}, "§ 26 Abs. 3 StVO"),
    pick("prk01", "parking", 3, 3, {0, 1}, "§ 12 Abs. 2 StVO"),
    num("prk02", "parking", 4, 5, "m", "§ 12 Abs. 3 Nr. 1 StVO"),
    pick("prk03", "parking", 4, 3, {0, 1}, "§ 12 Abs. 1 Nr. 1, Anlage 2 StVO, Zeichen 293"),
    pick("prk04", "parking", 3, 2, {0}, "§ 12 Abs. 4 StVO"),
    pick("prk05", "parking", 3, 2, {0}, "§ 12 Abs. 3 Nr. 3 StVO"),
    pick("prk06", "parking", 4, 2, {0, 1}, "§ 37 Abs. 1 StVO, Anlage 2 StVO, Zeichen 205, 206"),
    pick("prk07", "parking", 3, 3, {0}, "§ 13 Abs. 2 Nr. 2 StVO"),
    pick("prk08", "parking", 4, 3, {0, 1}, "§ 14 Abs. 2 StVO"),
    pick("prk09", "parking", 3, 2, {0}, "§ 12 Abs. 4, Abs. 4a StVO, Anlage 3 StVO, Zeichen 315", signPic("315")),
    pick("prk10", "parking", 4, 2, {0}, "Anlage 3 StVO, Zeichen 314", signPic("314")),
    pick("lgt01", "lights", 3, 3, {0}, "Anlage 3 StVO, Zeichen 327", signPic("327")),
    pick("lgt02", "lights", 4, 3, {0}, "§ 17 Abs. 3 Satz 5 StVO"),
    pick("lgt03", "lights", 3, 2, {0}, "§ 17 Abs. 2 StVO"),
    pick("lgt04", "lights", 3, 2, {0}, "§ 17 Abs. 3 StVO"),
    pick("lgt05", "lights", 4, 3, {0, 1}, "§ 15, § 15a Abs. 3, § 16 Abs. 2 StVO"),
    pick("lgt06", "lights", 3, 2, {0}, "§ 17 Abs. 2 StVO"),
    pick("lgt07", "lights", 3, 3, {0, 1}, "§ 16 Abs. 1 Nr. 1 StVO"),
    pick("lgt08", "lights", 4, 2, {0}, "§ 17 Abs. 4 StVO"),
    pick("lgt09", "lights", 3, 2, {0}, "§ 17 Abs. 1 StVO, Anlage 3 StVO, Zeichen 327"),
    pick("lgt10", "lights", 3, 2, {0}, "§ 17 Abs. 3 StVO"),
    pick("alc01", "alcohol", 5, 3, {0}, "§ 24c Abs. 1 StVG"),
    num("alc02", "alcohol", 5, 0.5, "‰", "§ 24a Abs. 1 StVG"),
    pick("alc03", "alcohol", 5, 2, {0}, "§ 316 StGB"),
    pick("alc04", "alcohol", 4, 3, {0}, "§ 316 StGB"),
    pick("alc05", "alcohol", 4, 2, {0}, "§ 316 StGB, § 24a Abs. 2 StVG, § 2 Abs. 1 FeV"),
    pick("alc06", "alcohol", 4, 3, {0}, "§ 2 Abs. 1 FeV, § 315c StGB"),
    num("alc07", "alcohol", 5, 3.5, "ng/ml", "§ 24a Abs. 1a, Abs. 2a, § 24c StVG"),
    pick("alc08", "alcohol", 4, 3, {0}, "§ 2a Abs. 1, Abs. 2a StVG"),
    num("alc09", "alcohol", 4, 8, "", "§ 4 Abs. 5 Satz 1 Nr. 3 StVG"),
    pick("alc10", "alcohol", 5, 3, {0, 1, 2}, "§ 316 StGB, § 24a StVG"),
    pick("env01", "environment", 3, 2, {0}, "§ 30 Abs. 1 StVO"),
    pick("env02", "environment", 2, 3, {0, 1}, "§ 30 Abs. 1 StVO"),
    pick("env03", "environment", 2, 3, {0, 1}, "§ 30 Abs. 1 StVO"),
    pick("env04", "environment", 3, 2, {0}, "§ 30 Abs. 1 Satz 3 StVO"),
    pick("env05", "environment", 2, 2, {0}, "§ 30 Abs. 1 StVO"),
    pick("env06", "environment", 3, 2, {0}, "Anlage 2 StVO, Zeichen 270.1", signPic("270.1")),
    pick("env07", "environment", 2, 2, {0}, "§ 8 AltölV"),
    pick("env08", "environment", 2, 2, {0}, "§ 30 Abs. 1 StVO"),
    pick("env09", "environment", 2, 2, {0}, "§ 30 Abs. 1 StVO"),
    pick("env10", "environment", 3, 2, {0}, "§ 30 Abs. 1 StVO"),
    num("tec01", "technique", 3, 1.6, "mm", "§ 36 Abs. 3 StVZO"),
    pick("tec02", "technique", 3, 2, {0}, "§ 2 Abs. 3a StVO, § 36 Abs. 4 StVZO"),
    pick("tec03", "technique", 3, 2, {0}, "§ 41b StVZO"),
    pick("tec04", "technique", 3, 3, {0, 1, 2}, "§ 53a Abs. 2, § 35h StVZO"),
    pick("tec05", "technique", 3, 2, {0}, "§ 23 Abs. 2 StVO"),
    pick("tec06", "technique", 2, 2, {0}, "§ 36 StVZO"),
    pick("tec07", "technique", 3, 2, {0}, "§ 29 StVZO, Anlage VIII"),
    pick("tec08", "technique", 3, 2, {0}, "§ 3 Abs. 1 StVO"),
    pick("tec09", "technique", 2, 2, {0}, "§ 41 StVZO"),
    pick("tec10", "technique", 3, 2, {0, 1}, "§ 21a Abs. 1 StVO"),
    pick("tec11", "technique", 2, 2, {0}, "§ 23 Abs. 1 StVO"),
    pick("tec12", "technique", 3, 2, {0}, "§ 3 Abs. 1 StVO"),
    num("clb01", "classB", 4, 3500, "kg", "§ 6 Abs. 1 FeV"),
    pick("clb02", "classB", 4, 3, {0, 1}, "§ 6 Abs. 1 FeV (Klasse B)"),
    pick("clb03", "classB", 3, 3, {0}, "§ 6 Abs. 1 FeV"),
    pick("clb04", "classB", 4, 3, {0, 1}, "§ 22 Abs. 4, Abs. 5 StVO"),
    pick("clb05", "classB", 5, 2, {0}, "§ 22 Abs. 1 StVO"),
    pick("clb06", "classB", 4, 3, {0, 1, 2}, "§ 15a Abs. 1, Abs. 3 StVO, § 43 Abs. 3 StVZO"),
    pick("clb07", "classB", 4, 3, {0, 1}, "§ 3 Abs. 1 StVO"),
    num("clb08", "classB", 3, 17, "", "§ 48a FeV"),
    pick("clb09", "classB", 3, 3, {0, 1}, "§ 6 Abs. 3 FeV"),
    pick("clb10", "classB", 4, 3, {0, 1}, "§ 22 Abs. 1, § 23 Abs. 1 StVO"),
    pick("clb11", "classB", 5, 3, {0}, "§ 23 Abs. 1 StVO"),
    num("clb12", "classB", 4, 4, "m", "§ 22 Abs. 2 StVO, § 32 Abs. 2 StVZO"),
  };
  return *list;
}

}  // namespace detail

}  // namespace fs
