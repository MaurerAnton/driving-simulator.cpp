// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/audio/waves.ts @ v1.7.7
#include "audio/waves.h"

#include <algorithm>
#include <cmath>

namespace fs {

namespace {

const double PI = 3.141592653589793;

/** In-place radix-2 FFT (forward, e^(−j…)); the length a power of two. */
void fft(std::vector<double>& re, std::vector<double>& im) {
  const int n = static_cast<int>(re.size());
  for (int i = 1, j = 0; i < n; i++) {
    int bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) {
      double t = re[i];
      re[i] = re[j];
      re[j] = t;
      t = im[i];
      im[i] = im[j];
      im[j] = t;
    }
  }
  for (int len = 2; len <= n; len <<= 1) {
    const int half = len >> 1;
    const double ang = (-2 * PI) / len;
    const double wr = std::cos(ang);
    const double wi = std::sin(ang);
    for (int i = 0; i < n; i += len) {
      double cr = 1;
      double ci = 0;
      for (int j = 0; j < half; j++) {
        const int a = i + j;
        const int b = a + half;
        const double tr = re[b] * cr - im[b] * ci;
        const double ti = re[b] * ci + im[b] * cr;
        re[b] = re[a] - tr;
        im[b] = im[a] - ti;
        re[a] += tr;
        im[a] += ti;
        const double t = cr * wr - ci * wi;
        ci = cr * wi + ci * wr;
        cr = t;
      }
    }
  }
}

}  // namespace

Partials wavePartials(const WaveSpec& spec, double periodDeg, int count, uint32_t seed) {
  Partials out;
  out.real.assign(static_cast<size_t>(count) + 1, 0.0f);
  out.imag.assign(static_cast<size_t>(count) + 1, 0.0f);
  std::vector<float>& real = out.real;
  std::vector<float>& imag = out.imag;
  Seeded rnd = seeded(seed);
  if (spec.kind == WaveKind::Pulses) {
    const size_t n = spec.firing.size();
    std::vector<double> phase(n);
    std::vector<double> amp(n);
    for (size_t i = 0; i < n; i++) {
      phase[i] = (spec.firing[i] + spec.skew * (rnd() * 2 - 1)) / periodDeg;
      amp[i] = (i < spec.amps.size() ? spec.amps[i] : 1) * (1 + spec.spread * (rnd() * 2 - 1));
    }
    const double ta = spec.rise / periodDeg;
    const double td = spec.decay / periodDeg;
    for (int k = 1; k <= count; k++) {
      const double w = 2 * PI * k;
      // the pulse e^(−t/decay) − e^(−t/rise): its spectrum at this partial (the periodic sum of its tail is exact)
      const double dd = 1 + w * w * td * td;
      const double da = 1 + w * w * ta * ta;
      const double hr = td / dd - ta / da;
      const double hi = (-w * td * td) / dd + (w * ta * ta) / da;
      // … once at every firing: Σ a·e^(−jwφ)
      double sr = 0;
      double si = 0;
      for (size_t i = 0; i < n; i++) {
        sr += amp[i] * std::cos(w * phase[i]);
        si -= amp[i] * std::sin(w * phase[i]);
      }
      const double cr = hr * sr - hi * si;
      const double ci = hr * si + hi * sr;
      real[static_cast<size_t>(k)] = static_cast<float>(2 * cr);
      imag[static_cast<size_t>(k)] = static_cast<float>(-2 * ci);
    }
  } else {
    for (const std::array<double, 2>& o : spec.orders) {
      const double order = o[0];
      const double a = o[1];
      if (order < 1 || order > count) continue;
      const size_t at = static_cast<size_t>(order);
      // (a phase of its own for each order – the same in both waves of a voice, so that blending them never cancels a partial)
      const double ph = spec.scatter ? seeded(seed ^ (static_cast<uint32_t>(order) * 2654435761u))() * 2 * PI : 0;
      real[at] = static_cast<float>(real[at] + a * std::cos(ph));
      imag[at] = static_cast<float>(imag[at] + a * std::sin(ph));
    }
  }
  const double peak = peakOf(out);
  if (peak > 0) {
    for (size_t k = 1; k <= static_cast<size_t>(count); k++) {
      real[k] = static_cast<float>(real[k] / peak);
      imag[k] = static_cast<float>(imag[k] / peak);
    }
  }
  return out;
}

std::vector<double> samples(const Partials& p, int n) {
  std::vector<double> re(static_cast<size_t>(n), 0.0);
  std::vector<double> im(static_cast<size_t>(n), 0.0);
  const int k = std::min(static_cast<int>(p.real.size()), n >> 1);
  for (int i = 1; i < k; i++) {
    re[static_cast<size_t>(i)] = p.real[static_cast<size_t>(i)];
    im[static_cast<size_t>(i)] = p.imag[static_cast<size_t>(i)];
  }
  // Re Σ (a + jb)·e^(−jθ) = Σ a·cos θ + b·sin θ: the wave itself
  fft(re, im);
  return re;
}

double peakOf(const Partials& p) {
  int n = 64;
  while (n < 4 * static_cast<int>(p.real.size()) && n < 16384) n <<= 1;
  const std::vector<double> x = samples(p, n);
  double peak = 0;
  for (int i = 0; i < n; i++) peak = std::max(peak, std::abs(x[static_cast<size_t>(i)]));
  return peak;
}

double rmsOf(const Partials& p) {
  double s = 0;
  for (size_t k = 1; k < p.real.size(); k++) {
    // (as doubles, as the original reads its Float32Array)
    const double re = p.real[k];
    const double im = p.imag[k];
    s += re * re + im * im;
  }
  return std::sqrt(s / 2);
}

double aWeight(double f) {
  const double f2 = f * f;
  const double ra = (std::pow(12194, 2) * f2 * f2) / ((f2 + std::pow(20.6, 2)) * std::sqrt((f2 + std::pow(107.7, 2)) * (f2 + std::pow(737.9, 2))) * (f2 + std::pow(12194, 2)));
  // (+2.00 dB: 1 at 1 kHz)
  return ra * 1.2589;
}

double lowpassGain(double f, double fc, double qDb) {
  const double q = std::pow(10, qDb / 20);
  const double x = f / fc;
  return 1 / std::sqrt(std::pow(1 - x * x, 2) + std::pow(x / q, 2));
}

double highpassGain(double f, double fc, double qDb) {
  const double q = std::pow(10, qDb / 20);
  const double x = f / fc;
  return (x * x) / std::sqrt(std::pow(1 - x * x, 2) + std::pow(x / q, 2));
}

double bandpassGain(double f, double f0, double q) {
  const double x = f / f0;
  const double b = x / q;
  return b / std::sqrt(std::pow(1 - x * x, 2) + b * b);
}

double peakingGain(double f, double f0, double q, double gainDb) {
  const double a = std::pow(10, gainDb / 40);
  const double x = f / f0;
  const double d = std::pow(1 - x * x, 2);
  return std::sqrt((d + std::pow((x * a) / q, 2)) / (d + std::pow(x / (a * q), 2)));
}

double waveLoudness(const Partials& a, double ga, const Partials& b, double gb, double f0, const Response& response) {
  double sum = 0;
  const size_t n = std::max(a.real.size(), b.real.size());
  for (size_t k = 1; k < n; k++) {
    const double f = static_cast<double>(k) * f0;
    if (f > 16000) break;
    const double re = (k < a.real.size() ? ga * a.real[k] : 0) + (k < b.real.size() ? gb * b.real[k] : 0);
    const double im = (k < a.imag.size() ? ga * a.imag[k] : 0) + (k < b.imag.size() ? gb * b.imag[k] : 0);
    const double g = response(f) * aWeight(f);
    sum += (re * re + im * im) * g * g;
  }
  return std::sqrt(sum / 2);
}

double noiseLoudness(const Response& response, double sampleRate) {
  // (its variance of 1/3 is spread evenly up to half the sample rate; integrated over ln f)
  const double density = 1.0 / 3 / (sampleRate / 2);
  const int steps = 240;
  const double lo = std::log(20);
  const double hi = std::log(std::min(20000.0, sampleRate / 2));
  const double d = (hi - lo) / steps;
  double sum = 0;
  for (int i = 0; i <= steps; i++) {
    const double f = std::exp(lo + d * i);
    const double g = response(f) * aWeight(f);
    sum += g * g * f * d * (i == 0 || i == steps ? 0.5 : 1);
  }
  return std::sqrt(density * sum);
}

double oscLoudness(std::string_view type, double f0, const Response& response) {
  double sum = 0;
  for (int k = 1; k * f0 < 16000 && k < 400; k++) {
    double a = 0;
    if (type == "sine") a = k == 1 ? 1 : 0;
    else if (type == "sawtooth") a = 2 / (PI * k);
    else if (type == "square") a = k % 2 ? 4 / (PI * k) : 0;
    else a = k % 2 ? 8 / (PI * PI * k * k) : 0;
    if (a == 0) continue;
    const double g = a * response(k * f0) * aWeight(k * f0);
    sum += g * g;
  }
  return std::sqrt(sum / 2);
}

}  // namespace fs
