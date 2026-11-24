// SPDX-License-Identifier: AGPL-3.0-only
/**
 * A text field that finds addresses of the map while one types
 * (street, house number, town).
 *
 * Origin: src/ui/addresspicker.ts (MaurerAnton/driving-simulator @ 1c08919,
 * v0.5.0).
 * The original builds a DOM input with a list of buttons below it; here the
 * same widgets are HudNodes the platform renders and forwards (focus, typing
 * and Enter come back as calls to `focus`, `type` and `enter`).
 */
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "game/address.h"
#include "ui/hud.h"

namespace fs {

struct AddressPickerOptions {
  std::string placeholder;
  std::string loading;
  std::string none;
  /** fetches the address list the first time it is needed (null: not there) */
  std::function<AddressIndex*()> load;
  std::function<void(const AddressHit&)> pick;
};

/** The widget tree and the field of one address picker. */
struct AddressPicker {
  std::unique_ptr<HudNode> node;
  HudNode* input = nullptr;

  /** The index is asked for once somebody uses the field. */
  void ensure();
  /** Text was typed into (or shown in) the field. */
  void type(const std::string& text);
  /** Enter was pressed: the first hit is the one meant. */
  void enter();
  /** An arrow key: walk the mark through the list below the field. */
  void arrow(int dir);   // +1 down, -1 up
  /** Escape: close the list first; a second Escape reaches the menu. */
  bool escape();
  /** The field was focused (the original loads the list then). */
  void focus() { ensure(); }

  AddressPickerOptions o;
  AddressIndex* index = nullptr;
  bool asked = false;
  std::vector<AddressHit> hits;
  /** the entry the arrow keys have reached, -1: none yet */
  int active = -1;
  std::vector<HudNode*> buttons;

 private:
  /** The entry the arrow keys have reached is marked in the list. */
  void mark(int i);
  void say(const std::string& text);
  void show(const std::string& q);
  void choose(const AddressHit& h);
  std::string value() const;
  void setValue(const std::string& v);
  void clearList();
};

AddressPicker addressPicker(const AddressPickerOptions& o);

}  // namespace fs
