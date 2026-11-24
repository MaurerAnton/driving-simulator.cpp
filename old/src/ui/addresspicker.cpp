// SPDX-License-Identifier: AGPL-3.0-only
/**
 * A text field that finds addresses of the map while one types (street,
 * house number, town).
 *
 * Origin: src/ui/addresspicker.ts (MaurerAnton/driving-simulator @ 1c08919,
 * v0.5.0).
 * The original is a DOM input with a list of buttons under it; here the same
 * widgets are HudNodes the platform renders.  Typing, focus and Enter come
 * back through `type()` / `focus()` / `enter()` (the platform forwards them),
 * so the search itself is the original's.
 */
#include "ui/addresspicker.h"

namespace fs {

namespace {
/** build one widget (the name avoids AddressPicker::node, the picker's root) */
std::unique_ptr<HudNode> mk(const std::string& tag, const std::string& cls = "",
                            const std::string& text = "") {
  auto n = std::make_unique<HudNode>();
  n->tag = tag;
  n->cls = cls;
  n->text = text;
  return n;
}
}  // namespace

AddressPicker addressPicker(const AddressPickerOptions& o) {
  AddressPicker p;
  p.o = o;
  auto root = mk("div", "addr");
  auto input = mk("input");
  input->id = "addrInput";
  input->text = o.placeholder;   // the platform reads it as the field's placeholder
  auto list = mk("div", "addr-list");
  list->id = "addrList";
  p.input = root->add(std::move(input));
  root->add(std::move(list));
  p.node = std::move(root);
  return p;
}

std::string AddressPicker::value() const {
  if (!input) return "";
  return input->cls.rfind("value:", 0) == 0 ? input->cls.substr(6) : std::string();
}

void AddressPicker::setValue(const std::string& v) {
  if (input) input->cls = "value:" + v;
}

void AddressPicker::clearList() {
  hits.clear();
  buttons.clear();
  active = -1;
  if (node && node->children.size() > 1) {
    HudNode* list = node->children[1].get();
    list->children.clear();
    list->text.clear();
  }
}

/** The entry the arrow keys have reached is marked (the field keeps the focus). */
void AddressPicker::mark(int i) {
  int n = static_cast<int>(hits.size());
  active = n == 0 ? -1 : ((i % n) + n) % n;
  for (size_t k = 0; k < buttons.size(); k++) {
    if (!buttons[k]) continue;
    // (the mark is a class: the platform shows it; aria-selected goes with it)
    buttons[k]->cls = static_cast<int>(k) == active ? "on" : "";
  }
}

void AddressPicker::say(const std::string& text) {
  clearList();
  if (text.empty()) return;
  if (node && node->children.size() > 1) {
    HudNode* list = node->children[1].get();
    list->add(mk("small", "", text));
  }
}

void AddressPicker::choose(const AddressHit& h) {
  setValue(h.label);
  clearList();
  if (o.pick) o.pick(h);
}

void AddressPicker::show(const std::string& qRaw) {
  std::string q = qRaw;
  while (!q.empty() && (q.front() == ' ' || q.front() == '\t' || q.front() == '\n' ||
                        q.front() == '\r'))
    q.erase(q.begin());
  while (!q.empty() && (q.back() == ' ' || q.back() == '\t' || q.back() == '\n' || q.back() == '\r'))
    q.pop_back();
  if (q.size() < 2) {
    hits.clear();
    say("");
    return;
  }
  if (!index) {
    say(o.loading);
    return;
  }
  hits = index->search(q, 8);
  if (hits.empty()) {
    say(o.none);
    return;
  }
  clearList();
  if (node && node->children.size() > 1) {
    HudNode* list = node->children[1].get();
    for (const AddressHit& h : hits) {
      auto b = mk("button", "", h.label);
      // the field keeps the focus: a click or a pointer on the entry takes it
      b->click = [this, h]() { choose(h); };
      buttons.push_back(b.get());
      list->add(std::move(b));
    }
    mark(-1);
  }
}

void AddressPicker::ensure() {
  if (asked) return;
  asked = true;
  index = o.load ? o.load() : nullptr;
  if (!index) say(o.none);
  else show(value());
}

void AddressPicker::type(const std::string& text) {
  setValue(text);
  ensure();
  show(text);
}

void AddressPicker::enter() {
  if (!hits.empty()) choose(hits[active >= 0 ? active : 0]);
}

/** An arrow key: walk the mark through the list below the field. */
void AddressPicker::arrow(int dir) {
  if (hits.empty()) {
    show(value());
    return;
  }
  mark(active < 0 ? (dir > 0 ? 0 : static_cast<int>(hits.size()) - 1) : active + dir);
}

/** Escape: close the list first; a second Escape reaches the menu. */
bool AddressPicker::escape() {
  if (hits.empty()) return false;
  clearList();
  return true;
}

}  // namespace fs
