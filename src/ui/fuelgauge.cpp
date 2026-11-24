// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/fuelgauge.ts @ v1.7.7
#include "ui/fuelgauge.h"

#include <algorithm>
#include <cmath>

#include "core/js.h"

namespace fs {

std::string fuelPercent(double share) {
  if (!std::isfinite(share)) return "0.000";
  return toFixed(std::max(0.0, std::min(1.0, share)) * 100, 3);
}

FuelGauge::FuelGauge(const std::string& title, const std::string& lowTitle) : el(dom::el("span")), title(title), lowTitle(lowTitle) {
  el.className("fuel");
  el.title(title);
  el.dataset("low", lowTitle);
  // (the pump is markup as in the original; the bar and the amount, which update() writes to,
  //  are made as elements – the same page, and one the page kept in memory can be asked about)
  el.innerHTML(
      "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><path fill=\"currentColor\" d=\"M4 3h9a1 1 0 0 1 1 1v7h1.2a2.3 2.3 0 0 1 2.3 2.3v4.2a.9.9 0 0 0 1.8 0V9.8l-2.6-2.6 1.3-1.3 3 3a2 2 0 0 1 .6 1.4v7.2a2.9 2.9 0 0 1-5.8 0v-4.2a.3.3 0 0 0-.3-.3H14V20h1v2H2v-2h1V4a1 1 0 0 1 1-1zm1.5 2v5h6V5h-6z\"/></svg>");
  dom::El barBox = dom::el("span", "bar");
  bar = dom::el("i");
  barBox.append(bar);
  amount = dom::el("span", "amount");
  el.append(barBox, amount);
  el.hidden(true);
}

void FuelGauge::update(std::optional<double> share, bool low) {
  // (the key carries the exact percentage: the tooltip changes when it does)
  const std::string pct = !share ? "" : fuelPercent(*share);
  const std::string key = !share ? "" : pct + "|" + (low ? "1" : "0");
  if (key == shown) return;
  shown = key;
  el.hidden(!share);
  if (!share) return;
  const double n = std::max(0.0, std::min(100.0, *share * 100));
  bar.style("width", toFixed(n, 0) + "%");
  el.toggleClass("low", low);
  // (the amount, visible: "9.5 %" beside the bar – the tooltip keeps three decimals)
  amount.textContent(toFixed(n, n >= 10 ? 0 : 1) + "%");
  // the exact amount: "Fuel — 9.536%" ("Fuel low …" when the reserve is reached)
  const std::string exact = pct + "%";
  const std::string label = (low ? lowTitle : title) + " — " + exact;
  el.title(label);
  el.setAttribute("aria-label", label);
}

}  // namespace fs
