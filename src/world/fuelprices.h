// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/world/fuelprices.ts @ v1.7.7
/**
 * Today's fuel prices at the real stations – where the player has a key of
 * Tankerkönig (https://creativecommons.tankerkoenig.de – the data of the
 * German Markttransparenzstelle für Kraftstoffe, CC BY 4.0; a key is free,
 * one asks for it there).  Without a key the game shows typical prices
 * (world/services.h FUELS).  The browser asks the service directly; it
 * learns the IP address, the key and the area (25 km round the player).
 */
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/task.h"
#include "platform/json.h"
#include "platform/plat.h"

namespace fs {

struct FuelPriceSource {
  const char* name;
  const char* url;
  const char* licence;
};
inline constexpr FuelPriceSource FUEL_PRICE_SOURCE = {"Tankerkönig (MTS-K)", "https://creativecommons.tankerkoenig.de", "CC BY 4.0"};

/** The prices of a station of the service by the fuels of the game (Super = e5, Super E10 = e10, Diesel and the lorries' = diesel). */
std::map<std::string, double> pricesOf(const Json& s);

class FuelPrices {
 public:
  using FetchFn = std::function<Task<plat::Response>(plat::Request)>;

  /** the player's key (settings); empty: no requests, typical prices */
  std::string key;
  /** when the last answer came (ms; 0: none yet) */
  double updated = 0;

  explicit FuelPrices(FetchFn fetchFn = nullptr);

  /** Ask for the prices round (x, z) – at most every 10 minutes, or when the player is 10 km away from where they were asked for. */
  void refresh(double x, double z, std::optional<double> now = std::nullopt);

  /** Today's price of a fuel at the station at (x, z) – null where the service knows none (no key, too far, not that fuel). */
  std::optional<double> priceAt(double x, double z, const std::string& fuel) const;

  struct LiveIn {
    double x;
    double z;
    std::map<std::string, double> prices;
  };
  /** For the tests: put the answer in directly. */
  void setLive(const std::vector<LiveIn>& list, std::optional<double> now = std::nullopt);

 private:
  struct Live {
    double x;
    double z;
    bool open;
    /** € per litre: e5, e10, diesel */
    std::map<std::string, double> prices;
  };
  std::vector<Live> live_;
  struct Asked {
    double x, z, at;
  } asked_;
  bool pending_ = false;
  FetchFn fetchFn_;
  /** (the answer of a request that comes after this list is gone is dropped) */
  std::shared_ptr<bool> life_ = std::make_shared<bool>(true);

  Task<void> ask(std::string url, double now);
};

/** The game's one price list (the services and the map ask it). */
FuelPrices& fuelPrices();

}  // namespace fs
