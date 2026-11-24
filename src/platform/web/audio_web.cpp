// SPDX-License-Identifier: AGPL-3.0-only
/**
 * audio.h in the browser: every call goes to the Web Audio object behind the handle
 * (platform/web/library_audio.js).  How many values name a handle is counted here, and
 * the page's table lets go of the object when nothing names it any more – what still
 * sounds is kept by the browser's own audio graph until it has ended.
 */
#include <emscripten.h>

#include <cstdlib>
#include <string>
#include <vector>

#include "core/js.h"
#include "platform/audio.h"

extern "C" {
int fsa_supported();
int fsa_context_new();
double fsa_context_num(int ctx, int what);
void fsa_context_call(int ctx, int what);
int fsa_destination(int ctx);
int fsa_create(int ctx, int kind, double a);
int fsa_has_panner(int ctx);
int fsa_buffer(int ctx, int channels, int length, double sampleRate);
void fsa_buffer_write(int buffer, int channel, const float* data, int n, int start);
void fsa_buffer_read(int buffer, int channel, float* out, int n);
int fsa_wave(int ctx, const float* real, const float* imag, int n, int disableNormalization);
void fsa_connect(int node, int destination, int param);
void fsa_disconnect(int node, int destination, int param);
void fsa_param(int node, int which, int op, double value, double time, double timeConstant);
double fsa_param_get(int node, int which);
void fsa_set_type(int node, const char* type);
void fsa_set_wave(int node, int wave);
void fsa_set_buffer(int node, int buffer);
void fsa_set_loop(int node, int on);
void fsa_start(int node, double when, double offset, double duration);
void fsa_stop(int node, double when);
int fsa_on_ended(int node, int listener);
void fsa_free(int h);
int fss_supported();
void fss_init();
char* fss_voices();
void fss_speak(int id, const char* text, const char* lang, int hasVoice, const char* voiceURI, const char* voiceName, const char* voiceLang, double rate, double pitch,
               double volume);
void fss_cancel();
void fss_resume();
int fss_flags();
}

namespace fs {

namespace {

/** how many values name each handle */
std::vector<int>& refs() {
  static std::vector<int> r;
  return r;
}

std::string take(char* s) {
  if (!s) return std::string();
  std::string out(s);
  std::free(s);
  return out;
}

}  // namespace

namespace audioBackend {

void retain(int h) {
  auto& r = refs();
  if (h <= 0) return;
  if (static_cast<size_t>(h) >= r.size()) r.resize(static_cast<size_t>(h) + 64, 0);
  r[static_cast<size_t>(h)]++;
}

void release(int h) {
  auto& r = refs();
  if (h <= 0 || static_cast<size_t>(h) >= r.size()) return;
  if (--r[static_cast<size_t>(h)] <= 0) {
    r[static_cast<size_t>(h)] = 0;
    fsa_free(h);
  }
}

bool supported() { return fsa_supported() != 0; }
int createContext() { return fsa_context_new(); }
double contextNumber(int ctx, int what) { return fsa_context_num(ctx, what); }
void contextCall(int ctx, int what) { fsa_context_call(ctx, what); }
int destination(int ctx) { return fsa_destination(ctx); }
int createNode(int ctx, int kind, double a) { return fsa_create(ctx, kind, a); }
bool hasStereoPanner(int ctx) { return fsa_has_panner(ctx) != 0; }
int createBuffer(int ctx, int channels, int length, double sampleRate) { return fsa_buffer(ctx, channels, length, sampleRate); }
void bufferWrite(int buffer, int channel, const float* data, int n, int start) { fsa_buffer_write(buffer, channel, data, n, start); }
void bufferRead(int buffer, int channel, float* out, int n) { fsa_buffer_read(buffer, channel, out, n); }
int createWave(int ctx, const float* real, const float* imag, int n, bool disableNormalization) { return fsa_wave(ctx, real, imag, n, disableNormalization ? 1 : 0); }
void connect(int node, int destination, int param) { fsa_connect(node, destination, param); }
void disconnect(int node, int destination, int param) { fsa_disconnect(node, destination, param); }
void param(int node, int which, int op, double value, double time, double timeConstant) { fsa_param(node, which, op, value, time, timeConstant); }
double paramValue(int node, int which) { return fsa_param_get(node, which); }
void setType(int node, const char* type) { fsa_set_type(node, type); }
void setWave(int node, int wave) { fsa_set_wave(node, wave); }
void setBuffer(int node, int buffer) { fsa_set_buffer(node, buffer); }
void setLoop(int node, bool on) { fsa_set_loop(node, on ? 1 : 0); }
void start(int node, double when, double offset, double duration) { fsa_start(node, when, offset, duration); }
void stop(int node, double when) { fsa_stop(node, when); }
int onEnded(int node, int listener) { return fsa_on_ended(node, listener); }

bool speechSupported() { return fss_supported() != 0; }
void speechInit() { fss_init(); }

std::vector<SpeechSynthesisVoice> voices() {
  // one voice a record (ended by U+001E), its fields parted by U+001F: name, lang, voiceURI, flags (1 local, 2 default)
  std::vector<SpeechSynthesisVoice> out;
  std::string all = take(fss_voices());
  if (all.empty()) return out;
  for (const std::string& record : split(all, "\x1e")) {
    if (record.empty()) continue;
    std::vector<std::string> f = split(record, "\x1f");
    if (f.size() < 4) continue;
    SpeechSynthesisVoice v;
    v.name = f[0];
    v.lang = f[1];
    v.voiceURI = f[2];
    int flags = std::atoi(f[3].c_str());
    v.localService = (flags & 1) != 0;
    v.default_ = (flags & 2) != 0;
    out.push_back(std::move(v));
  }
  return out;
}

void speak(int id, const std::string& text, const std::string& lang, const SpeechSynthesisVoice* voice, double rate, double pitch, double volume) {
  fss_speak(id, text.c_str(), lang.c_str(), voice ? 1 : 0, voice ? voice->voiceURI.c_str() : "", voice ? voice->name.c_str() : "", voice ? voice->lang.c_str() : "", rate, pitch,
            volume);
}

void speechCancel() { fss_cancel(); }
void speechResume() { fss_resume(); }
int speechFlags() { return fss_flags(); }

}  // namespace audioBackend

}  // namespace fs

extern "C" {

/** A source has ended: its handler runs (once). */
EMSCRIPTEN_KEEPALIVE void fsa_ended(int listener) { fs::audioDetail::ended(listener); }

/** An utterance has been read to its end (kind 0) or has failed (kind 1: `error` says why). */
EMSCRIPTEN_KEEPALIVE void fss_event(int id, int kind, const char* error) { fs::audioDetail::speechEvent(id, kind, error ? std::string(error) : std::string()); }

/** The browser's list of voices has changed. */
EMSCRIPTEN_KEEPALIVE void fss_voices_changed() { fs::audioDetail::voicesChanged(); }

}  // extern "C"
