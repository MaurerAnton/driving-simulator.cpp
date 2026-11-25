// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/fuel.ts @ v1.7.7
/**
 * Fuel: the tank of the player's vehicle, what its engine burns, and the
 * pump that fills it.
 *
 * What an engine burns follows from the work its cylinders do.  The vehicle
 * model (sim/vehicle) gives the engine's speed and how far the throttle is
 * open – the share of the torque curve the cylinders give at that speed –
 * and its friction: the cylinders give `load × (full-load torque + friction)`
 * of which the friction eats its part, the rest drives the car.  That work,
 * divided by how much of the fuel's energy the cylinders turn into work
 * (about a third for a petrol engine, more for a diesel), is the fuel
 * burnt.  So the consumption comes out of the same physics as the driving:
 * idling costs a little (the idle control holds the throttle a crack open),
 * rolling in gear with the foot off costs nothing (the fuel is cut off), a
 * heavy load, a hill or a high speed cost more – realistic figures follow
 * (tests/fuel_test.cpp: a compact car 5–7 l/100 km at 100 km/h, a 12-t
 * truck 20–30 l at 80 km/h …).
 */
#pragma once

#include <string>

#include "sim/vehicle/types.h"

namespace fs {

/** 'petrol' | 'diesel' */
using FuelKind = std::string;

/** What a kind of vehicle carries and how its engine uses the fuel. */
struct TankSpec {
  /** litres */
  double capacity = 0;
  FuelKind fuel;
  /** the share of the fuel's energy the cylinders turn into work (indicated efficiency) */
  double efficiency = 0;
  /** below this share of the tank the fuel lamp lights */
  double reserve = 0;
  /** how fast the pump fills it (l/s; faster than a real pump's 0.6 – nobody wants to wait a minute) */
  double flow = 0;
};

/** The energy in a litre of fuel (J): petrol about 32 MJ, diesel about 36 MJ. */
double ENERGY_PER_LITRE(const FuelKind& fuel);

/** The tank of a kind of vehicle (game/vehicles.h VehicleKind) – null for those without one (the tram runs on the wire, the aeroplane has its own, on foot). */
const TankSpec* tankFor(const std::string& kind);

/** The full-load torque of an engine at an engine speed (N·m), from its torque curve. */
double fullTorque(const VehicleSpec& spec, double rpm);

/**
 * What a running engine burns (litres a second) at an engine speed (rpm)
 * and throttle (`load` 0 … 1, as the vehicle model applies it: idle control
 * and limiter included): the work of its cylinders over the energy of the fuel.
 */
double fuelFlow(const VehicleSpec& spec, const TankSpec& tank, double rpm, double load, bool running);

/** The fuel in the tank of the vehicle being driven. */
class FuelTank {
 public:
  const TankSpec spec;
  double litres = 0;
  /** burnt since the drive began (l), and over what distance (m) – for the consumption shown */
  double used = 0;
  double distance = 0;

  FuelTank(const TankSpec& spec, double share);

  /** How full it is (0 … 1). */
  double share() const { return litres / spec.capacity; }
  /** The fuel lamp: in the reserve. */
  bool low() const { return share() < spec.reserve; }
  bool empty() const { return litres <= 1e-6; }
  /** Burn for `dt` seconds at `flow` l/s while `metres` are driven. */
  void burn(double flow, double dt, double metres = 0);
  /** Pour in up to `litres`: what fitted. */
  double fill(double litres);
  /** Litres per 100 km since the drive began (NaN before the first kilometre). */
  double average() const;
};

/**
 * A fill-up at a pump: the litres and the price count up while the fuel
 * runs – slowly for the first second, at the pump's flow then, slowly again
 * over the last litres, until the nozzle shuts off by itself at a full tank
 * (or the driver stops it).
 */
class FillUp {
 public:
  const double price;
  double litres = 0;
  double t = 0;
  bool done = false;

  /** `price`: per litre; `flow`: the pump's (l/s) */
  FillUp(double price, double flow) : price(price), flow_(flow) {}

  double cost() const;
  /** Run the pump for `dt`: fills the tank. */
  void step(double dt, FuelTank& tank);

 private:
  double flow_;
};

/**
 * How full the tank is when a drive begins: mostly between a third and
 * nearly full, but now and then nearly empty, so that the fuel lamp and the
 * way to a station come into play.  `rand`: 0 … 1.
 */
double startShare(double rand, double rand2);

}  // namespace fs
