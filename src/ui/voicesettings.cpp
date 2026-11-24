// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/voicesettings.ts @ v1.7.7
#include "ui/voicesettings.h"

#include <array>
#include <memory>
#include <utility>

#include "audio/speech.h"
#include "core/js.h"
#include "platform/frame.h"

namespace fs {

using dom::El;
using dom::el;

El voiceSettings(VoiceSettingsHost s, const std::string& lang, const TrFn& t, std::function<void()> changed) {
  El box = el("div", "voice-settings");
  if (!speech.supported()) {
    box.append(el("p", "note", t("voice.unsupported", {})));
    return box;
  }
  auto pref = [s, lang]() -> SpeechPref { return prefOf(s.speech ? &*s.speech : nullptr, lang); };
  /** `set({ rate: v })`: the language's choice with this changed */
  auto set = [s, lang, pref, changed](const std::function<void(SpeechPref& p)>& change) {
    SpeechPref p = pref();
    change(p);
    if (!s.speech) s.speech.emplace();
    (*s.speech)[lang] = p;
    changed();
  };

  // the voice
  El row = el("div", "row");
  El sel = el("select");
  row.append(el("label", "", t("voice.choice", {})), sel);

  // rate and pitch: their own ranges, 1 in the middle of the track
  auto slider = [](const std::string& label, const std::array<double, 2>& range, double value, std::function<void(double v)> put) {
    El r = el("div", "row");
    El input = el("input");
    input.type("range");
    input.set("min", jsStr(range[0]));
    input.set("max", jsStr(range[1]));
    input.set("step", "0.05");
    input.value(jsStr(value));
    input.onchange([input, put = std::move(put)] { put(jsRound(jsNumber(input.value()) * 100) / 100); });
    r.append(el("label", "", label), input);
    return r;
  };
  El rate = slider(t("voice.rate", {}), RATE_RANGE, pref().rate, [set](double v) { set([v](SpeechPref& p) { p.rate = v; }); });
  El pitch = slider(t("voice.pitch", {}), PITCH_RANGE, pref().pitch, [set](double v) { set([v](SpeechPref& p) { p.pitch = v; }); });

  El buttons = el("div", "buttons");
  El test = el("button", "btn ghost chip", t("voice.test", {}));
  // (a sentence the game really says – with a distance, so the units are heard too)
  test.onclick([s, lang] { speech.test(tr("navIn", lang, {{"d", 200}, {"what", tr("navRight", lang)}}), lang, s); });
  El reset = el("button", "btn ghost chip", t("voice.reset", {}));
  buttons.append(test, reset);

  El note = el("p", "note");
  El online = el("p", "note", t("voice.onlineNote", {}));
  box.append(row, rate, pitch, buttons, note, online);

  auto status = [t, lang, pref, note, row, rate, pitch, buttons] {
    const VoiceStatus st = speech.status(lang);
    const std::optional<SpeechSynthesisVoice> v = speech.voiceFor(lang, pref());
    note.className(st == VoiceStatus::None ? "note warn" : "note");
    const std::string text = st == VoiceStatus::None      ? t("voice.none", {})
                             : st == VoiceStatus::Related ? t("voice.related", {{"name", v ? v->name + " (" + v->lang + ")" : std::string()}})
                             : st == VoiceStatus::Unknown ? t("voice.unknown", {})
                                                          : std::string();
    note.textContent(text);
    note.hidden(text.empty());
    const bool has = st == VoiceStatus::Good || st == VoiceStatus::Related || st == VoiceStatus::Unknown;
    row.hidden(st == VoiceStatus::None || st == VoiceStatus::Unknown);
    buttons.hidden(!has);
    pitch.hidden(!has);
    rate.hidden(!has);
  };
  auto fill = [t, lang, pref, sel, online, status] {
    const std::vector<SpeechSynthesisVoice> list = speech.voicesFor(lang);
    sel.textContent("");
    El autoOp = el("option", "", !list.empty() ? t("voice.auto", {{"name", list[0].name}}) : t("voice.autoNone", {}));
    autoOp.value("");
    sel.append(autoOp);
    bool anyOnline = false;
    for (const SpeechSynthesisVoice& v : list) {
      El o = el("option", "", v.name + " (" + v.lang + ")" + (v.localService ? "" : " · " + t("voice.online", {})));
      o.value(v.voiceURI);
      sel.append(o);
      if (!v.localService) anyOnline = true;
    }
    const std::string chosen = pref().voice;
    bool there = false;
    for (const SpeechSynthesisVoice& v : list)
      if (v.voiceURI == chosen) there = true;
    sel.value(there ? chosen : "");
    online.hidden(!anyOnline);
    status();
  };
  sel.onchange([set, sel, status] {
    const std::string voice = sel.value();
    set([voice](SpeechPref& p) { p.voice = voice; });
    status();
  });
  reset.onclick([s, lang, changed, rate, pitch, fill] {
    if (s.speech) s.speech->erase(lang);
    else s.speech.emplace();
    changed();
    rate.querySelector("input").value("1");
    pitch.querySelector("input").value("1");
    fill();
  });
  fill();
  // (the voices arrive late; once the panel is gone, it stops listening)
  auto off = std::make_shared<std::function<void()>>();
  *off = speech.onVoices([box, fill, off] {
    if (!box.isConnected() && !box.dataset("shown").empty()) (*off)();
    else fill();
  });
  plat::requestAnimationFrame([box](double) { box.dataset("shown", "1"); });
  return box;
}

}  // namespace fs
