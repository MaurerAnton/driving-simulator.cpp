// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/fuelgauge.ts @ v1.7.7
/**
 * The fuel gauge of the HUD's dashboard strip: a pump, and beside it a bar
 * that shows how full the tank is – amber and the pump lit, as the lamp of
 * an instrument cluster, once the reserve is reached.  A vehicle without a
 * tank (the tram, on foot) shows none.  The exact amount is shown as a
 * percentage beside the bar ("9.5%", and to three decimals in the tooltip
 * and the accessible name: "Fuel — 9.536%").
 */
#pragma once

#include <optional>
#include <string>

#include "platform/dom.h"

namespace fs {

/** The tank's share (0 … 1) as a percentage to three decimals: "9.536". */
std::string fuelPercent(double share);

class FuelGauge {
 public:
  const dom::El el;

  FuelGauge(const std::string& title, const std::string& lowTitle);

  /** How full the tank is (0 … 1; none: no tank), and whether the reserve is reached. */
  void update(std::optional<double> share, bool low);

 private:
  dom::El bar;
  dom::El amount;
  std::string shown;
  const std::string title;
  const std::string lowTitle;
};

}  // namespace fs
