// SPDX-License-Identifier: AGPL-3.0-only
#include "core/js.h"

#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace fs {

std::string jsStr(double x) {
  if (x != x) return "NaN";
  if (std::isinf(x)) return x > 0 ? "Infinity" : "-Infinity";
  if (x == 0) return "0";
  char buf[64];
  double a = std::fabs(x);
  if (a >= 1e21 || a < 1e-6) {
    // JavaScript writes these with an exponent: 1e+21, 1.5e-7
    auto r = std::to_chars(buf, buf + sizeof buf, x, std::chars_format::scientific);
    std::string s(buf, r.ptr);
    // to_chars: "1e+21", "1.5e-07" → JavaScript: "1e+21", "1.5e-7"
    size_t e = s.find('e');
    if (e != std::string::npos) {
      std::string mant = s.substr(0, e);
      char sign = s[e + 1];
      size_t d = e + 2;
      while (d + 1 < s.size() && s[d] == '0') d++;
      s = mant + "e" + sign + s.substr(d);
    }
    return s;
  }
  auto r = std::to_chars(buf, buf + sizeof buf, x, std::chars_format::fixed);
  return std::string(buf, r.ptr);
}

std::string toFixed(double x, int digits) {
  if (x != x) return "NaN";
  if (std::isinf(x)) return x > 0 ? "Infinity" : "-Infinity";
  char buf[400];
  // (JavaScript rounds the exact decimal value half up; printf rounds it to even only at an
  // exact tie, which a binary fraction reaches only where both agree but for x.5 with digits 0)
  if (digits == 0) {
    double r = std::fabs(x);
    double f = std::floor(r);
    double out = (r - f >= 0.5) ? f + 1 : f;
    if (x < 0 && out != 0) out = -out;
    std::snprintf(buf, sizeof buf, "%.0f", out);
    if (x < 0 && out == 0 && std::fabs(x) >= 0.5) return std::string("-") + buf;
    if (x < 0 && out == 0) return "-0";
    return buf;
  }
  std::snprintf(buf, sizeof buf, "%.*f", digits, x);
  return buf;
}

double jsNumber(const std::string& text, bool strict) {
  const char* s = text.c_str();
  while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
  if (!*s) return strict ? 0.0 : JS_NAN;
  if (!std::strncmp(s, "Infinity", 8) || !std::strncmp(s, "+Infinity", 9)) return JS_INF;
  if (!std::strncmp(s, "-Infinity", 9)) return -JS_INF;
  // (no hexadecimal floats, "nan" or "inf" words: those are C's, not JavaScript's)
  const char* p = s;
  if (*p == '+' || *p == '-') p++;
  if (!((*p >= '0' && *p <= '9') || (*p == '.' && p[1] >= '0' && p[1] <= '9'))) return JS_NAN;
  if (strict && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
    char* end = nullptr;
    unsigned long long v = std::strtoull(p + 2, &end, 16);
    if (end == p + 2) return JS_NAN;
    while (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r') end++;
    if (*end) return JS_NAN;
    return (*s == '-') ? -static_cast<double>(v) : static_cast<double>(v);
  }
  if (!strict && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) return 0;
  char* end = nullptr;
  double v = std::strtod(s, &end);
  if (end == s) return JS_NAN;
  if (strict) {
    while (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r') end++;
    if (*end) return JS_NAN;
  }
  return v;
}

double jsParseInt(const std::string& text) {
  const char* s = text.c_str();
  while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
  const char* p = s;
  if (*p == '+' || *p == '-') p++;
  if (!(*p >= '0' && *p <= '9')) return JS_NAN;
  double v = 0;
  while (*p >= '0' && *p <= '9') v = v * 10 + (*p++ - '0');
  return *s == '-' ? -v : v;
}

std::string toRadix(int64_t x, int radix) {
  if (x == 0) return "0";
  bool neg = x < 0;
  uint64_t u = neg ? static_cast<uint64_t>(-(x + 1)) + 1 : static_cast<uint64_t>(x);
  std::string s;
  while (u) {
    int d = static_cast<int>(u % static_cast<uint64_t>(radix));
    s.insert(s.begin(), static_cast<char>(d < 10 ? '0' + d : 'a' + d - 10));
    u /= static_cast<uint64_t>(radix);
  }
  return neg ? "-" + s : s;
}

std::string padStart(const std::string& s, size_t n, char c) {
  return s.size() >= n ? s : std::string(n - s.size(), c) + s;
}
std::string padEnd(const std::string& s, size_t n, char c) {
  return s.size() >= n ? s : s + std::string(n - s.size(), c);
}

std::string trim(const std::string& s) {
  size_t a = 0, b = s.size();
  auto ws = [](unsigned char ch) { return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' || ch == '\v'; };
  while (a < b && ws(static_cast<unsigned char>(s[a]))) a++;
  while (b > a && ws(static_cast<unsigned char>(s[b - 1]))) b--;
  return s.substr(a, b - a);
}

std::vector<std::string> split(const std::string& s, const std::string& sep) {
  std::vector<std::string> out;
  if (sep.empty()) {
    out.push_back(s);
    return out;
  }
  size_t at = 0;
  for (;;) {
    size_t i = s.find(sep, at);
    if (i == std::string::npos) {
      out.push_back(s.substr(at));
      return out;
    }
    out.push_back(s.substr(at, i - at));
    at = i + sep.size();
  }
}

std::string join(const std::vector<std::string>& parts, const std::string& sep) {
  std::string out;
  for (size_t i = 0; i < parts.size(); i++) {
    if (i) out += sep;
    out += parts[i];
  }
  return out;
}

std::string replaceAll(std::string s, const std::string& from, const std::string& to) {
  if (from.empty()) return s;
  size_t at = 0;
  while ((at = s.find(from, at)) != std::string::npos) {
    s.replace(at, from.size(), to);
    at += to.size();
  }
  return s;
}

bool startsWith(const std::string& s, const std::string& prefix) {
  return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}
bool endsWith(const std::string& s, const std::string& suffix) {
  return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string toLowerAscii(std::string s) {
  for (char& c : s)
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  return s;
}
std::string toUpperAscii(std::string s) {
  for (char& c : s)
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
  return s;
}

std::string encodeURIComponent(const std::string& s) {
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  for (unsigned char c : s) {
    bool keep = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '!' ||
                c == '~' || c == '*' || c == '\'' || c == '(' || c == ')';
    if (keep) out += static_cast<char>(c);
    else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 15];
    }
  }
  return out;
}

std::string decodeURIComponent(const std::string& s) {
  auto hv = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  std::string out;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '%' && i + 2 < s.size() && hv(s[i + 1]) >= 0 && hv(s[i + 2]) >= 0) {
      out += static_cast<char>(hv(s[i + 1]) * 16 + hv(s[i + 2]));
      i += 2;
    } else out += s[i];
  }
  return out;
}

}  // namespace fs
