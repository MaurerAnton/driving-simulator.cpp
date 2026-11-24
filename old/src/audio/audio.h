// SPDX-License-Identifier: AGPL-3.0-only
/**
 * All sound is synthesised – engine, tyres, wind, indicator relay, horn,
 * starter – so no audio files are shipped.
 *
 * Origin: src/audio/audio.ts (MaurerAnton/driving-simulator @ 5b6ddc7, v0.1.0).
 * The Web Audio graph (oscillators, biquads, noise buffer, ramps) is
 * reproduced faithfully; the host supplies the device through `AudioBackend`.
 */
#pragma once

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

namespace fs {

struct AudioState {
  double rpm = 0;
  /** 0 … 1 engine load (throttle) */
  double load = 0;
  bool engineRunning = false;
  bool starter = false;
  /** m/s */
  double speed = 0;
  /** 0 … 1 tyre squeal */
  double skid = 0;
  bool indicatorOn = false;
  bool blinkOn = false;
  bool horn = false;
  /** surface roughness for road noise (1 asphalt … 3 gravel) */
  double rough = 1;
};

enum class ClickKind { Tick, Tock, Crash, Grind, Chime, Bump };

/**
 * The audio device behind the graph.  Under Emscripten this is the Web Audio
 * API through EM_JS; in the native build a backend can record or ignore it.
 */
class AudioBackend {
 public:
  virtual ~AudioBackend() = default;
  /** Create the context and the graph (call from a user gesture). */
  virtual bool start(double volume) = 0;
  virtual bool running() const = 0;
  virtual void suspend() = 0;
  virtual void resume() = 0;
  virtual double sampleRate() const = 0;
  virtual double currentTime() const = 0;
  /** setTargetAtTime on a named parameter (see GameAudio::update). */
  virtual void setTarget(const std::string& param, double value, double timeConstant) = 0;
  /** One-shot click: noise burst through a filter, or the chime tone pair. */
  virtual void click(ClickKind kind) = 0;
  virtual void setMasterGain(double v, double timeConstant) = 0;
};

class GameAudio {
 public:
  explicit GameAudio(AudioBackend& backend) : backend_(backend) {}

  /** Must be called from a user gesture (click / key) – browsers require it. */
  void start() {
    if (!started_) {
      backend_.start(volume_);
      started_ = true;
    } else {
      backend_.resume();
    }
  }
  void suspend() { backend_.suspend(); }
  void setVolume(double v) {
    volume_ = v;
    if (started_) backend_.setMasterGain(v, 0.05);
  }
  double volume() const { return volume_; }

  /** Short percussive sounds. */
  void click(ClickKind kind) { backend_.click(kind); }

  /**
   * Drive the graph.  The parameter names are the ones `AudioBackend`
   * understands: engF0, engF1, engF2, engGain, engFilter, roadGain,
   * roadFilter, skidGain, hornGain, starterGain.
   */
  void update(const AudioState& s) {
    if (!started_ || !backend_.running()) return;
    double rpm = s.rpm > 0 ? s.rpm : 0;
    // four-stroke four-cylinder: two ignitions per revolution
    double f0 = rpm / 30.0;
    if (f0 < 20) f0 = 20;
    backend_.setTarget("engF0", f0, 0.03);
    backend_.setTarget("engF1", f0 * 0.5, 0.03);
    backend_.setTarget("engF2", f0 * 2.01, 0.03);
    bool turning = rpm > 120;
    double level = turning ? (s.engineRunning ? 0.05 + 0.1 * s.load + (rpm / 7000.0) * 0.08 : 0.03) : 0;
    backend_.setTarget("engGain", level, 0.05);
    backend_.setTarget("engFilter", 260 + rpm * 0.33 + s.load * 900, 0.05);
    double v = s.speed < 0 ? -s.speed : s.speed;
    backend_.setTarget("roadGain", std::min(0.22, v * 0.0045 * s.rough), 0.1);
    backend_.setTarget("roadFilter", 300 + v * 28 * s.rough, 0.1);
    backend_.setTarget("skidGain", s.skid * 0.3, 0.04);
    backend_.setTarget("hornGain", s.horn ? 0.16 : 0, 0.015);
    backend_.setTarget("starterGain", s.starter && !s.engineRunning ? 0.12 : 0, 0.02);
    if (s.indicatorOn && s.blinkOn != lastBlink_) click(s.blinkOn ? ClickKind::Tick : ClickKind::Tock);
    lastBlink_ = s.blinkOn;
  }

 private:
  AudioBackend& backend_;
  bool started_ = false;
  bool lastBlink_ = false;
  double volume_ = 0.7;
};

}  // namespace fs
