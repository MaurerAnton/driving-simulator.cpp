// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/addresspicker.ts @ v1.7.7
/** A text field that finds addresses of the map while one types (street, house number, town). */
#pragma once

#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/js.h"
#include "core/task.h"
#include "game/address.h"
#include "i18n/unicode.h"
#include "platform/dom.h"

namespace fs {

/**
 * `{ search(query, limit?, near?): T[] }`: the list that is searched – an AddressIndex, or
 * whatever stands in for one (the big map's search of places).  An empty function is `null`.
 */
template <class T = AddressHit>
using AddressSearchOf = std::function<std::vector<T>(const std::string& query, size_t limit, const std::optional<AddressIndex::Near>& near)>;

template <class T = AddressHit>
struct AddressPickerOptions {
  std::string placeholder;
  std::string loading;
  std::string none;
  /** fetches the address list the first time it is needed */
  std::function<Task<AddressSearchOf<T>>()> load;
  std::function<void(const T& hit)> pick;
  /** where the one who asks is (a place of the map): what lies near comes first (may be left out) */
  std::function<std::optional<AddressIndex::Near>()> near = nullptr;
};

/** `{ node, input }` */
struct AddressPicker {
  dom::El node;
  dom::El input;
};

/**
 * Is a query a line number of public transport, enough on its own to search
 * for ("5", "52", "N3"): digits, or a letter before digits.  Such a token is
 * whole at one character, where a single letter of a name is not.
 */
bool isLineToken(const std::string& q);

namespace detail {
/** `++pickers` */
int nextAddressPicker();

template <class T>
struct AddressPickerState {
  AddressSearchOf<T> index = nullptr;
  bool asked = false;
  std::vector<T> hits;
  /** the entry the arrow keys have reached, -1: none yet */
  int active = -1;
};

template <class T>
Task<void> addressPickerLoad(std::shared_ptr<AddressPickerOptions<T>> o, std::shared_ptr<AddressPickerState<T>> st, std::function<void(const std::string&)> say, std::function<void()> show) {
  AddressSearchOf<T> ix = co_await o->load();
  st->index = ix;
  if (!ix) say(o->none);
  else show();
}
}  // namespace detail

template <class T = AddressHit>
AddressPicker addressPicker(AddressPickerOptions<T> options) {
  using dom::El;
  auto o = std::make_shared<AddressPickerOptions<T>>(std::move(options));
  El node = dom::el("div");
  node.className("addr");
  El input = dom::el("input");
  input.type("search");
  input.placeholder(o->placeholder);
  input.set("autocomplete", "off");
  input.set("spellcheck", false);
  input.set("enterKeyHint", "search");
  El list = dom::el("div");
  list.className("addr-list");
  node.append(input, list);

  // a combo box: the field keeps the focus, the arrow keys move a mark through the list below it
  const std::string listId = "addr-list-" + std::to_string(detail::nextAddressPicker());
  list.id(listId);
  list.setAttribute("role", "listbox");
  input.setAttribute("role", "combobox");
  input.setAttribute("aria-autocomplete", "list");
  input.setAttribute("aria-controls", listId);
  input.setAttribute("aria-expanded", "false");

  // (what the closures share holds no elements: the buttons are the list's children)
  auto st = std::make_shared<detail::AddressPickerState<T>>();
  std::function<void(int)> mark = [st, input, list](int i) {
    const int n = static_cast<int>(st->hits.size());
    st->active = n == 0 ? -1 : ((i % n) + n) % n;
    const std::vector<El> buttons = n == 0 ? std::vector<El>() : list.children();
    for (int k = 0; k < static_cast<int>(buttons.size()); k++) {
      buttons[static_cast<size_t>(k)].toggleClass("on", k == st->active);
      buttons[static_cast<size_t>(k)].setAttribute("aria-selected", k == st->active ? "true" : "false");
    }
    if (st->active >= 0 && st->active < static_cast<int>(buttons.size())) {
      input.setAttribute("aria-activedescendant", buttons[static_cast<size_t>(st->active)].get("id"));
      // (the original asks for { block: 'nearest' }: dom.h's scrollIntoView takes no options)
      buttons[static_cast<size_t>(st->active)].scrollIntoView();
    } else input.removeAttribute("aria-activedescendant");
  };
  std::function<void()> clear = [st, input, list] {
    list.replaceChildren();
    st->hits.clear();
    st->active = -1;
    input.setAttribute("aria-expanded", "false");
    input.removeAttribute("aria-activedescendant");
  };
  std::function<void(const std::string&)> say = [clear, list](const std::string& text) {
    clear();
    if (text.empty()) return;
    El p = dom::el("small");
    p.textContent(text);
    list.append(p);
  };
  std::function<void(T)> choose = [o, clear, input](T h) {
    input.value(h.label);
    clear();
    o->pick(h);
  };
  std::function<void()> show = [o, st, say, clear, mark, choose, input, list, listId] {
    const std::string q = trim(input.value());
    // (one character is enough for a line number of public transport – "5", "N3" – where it is a
    //  whole token; for text a single letter would match half the map, so it waits for the second)
    if (i18n::utf16Length(q) < (isLineToken(q) ? 1u : 2u)) {
      say("");
      return;
    }
    if (!st->index) {
      say(o->loading);
      return;
    }
    std::vector<T> found = st->index(q, 8, o->near ? o->near() : std::nullopt);
    if (found.empty()) {
      say(o->none);
      return;
    }
    clear();
    st->hits = std::move(found);
    input.setAttribute("aria-expanded", "true");
    for (int k = 0; k < static_cast<int>(st->hits.size()); k++) {
      El b = dom::el("button");
      b.type("button");
      b.id(listId + "-" + std::to_string(k));
      b.setAttribute("role", "option");
      // the field keeps the focus (Tab goes on to the next control, the arrow keys walk the list)
      b.tabIndex(-1);
      b.textContent(st->hits[static_cast<size_t>(k)].label);
      b.onclick([st, choose, k] {
        if (k < static_cast<int>(st->hits.size())) choose(st->hits[static_cast<size_t>(k)]);
      });
      b.handler("pointerenter", [mark, k](dom::Event&) { mark(k); });
      list.append(b);
    }
    mark(-1);
  };
  std::function<void()> ensure = [o, st, say, show] {
    if (st->asked) return;
    st->asked = true;
    detail::addressPickerLoad<T>(o, st, say, show).detach();
  };
  input.on("focus", ensure);
  input.on("input", [ensure, show] {
    ensure();
    show();
  });
  input.on("keydown", [st, show, mark, choose, clear](dom::Event& e) {
    // by key *and* by code: some browsers and input methods report one of them only
    // (the original also leaves a key alone while a text is being composed – `e.isComposing`,
    //  which dom::Event does not carry)
    const std::string k = !e.key.empty() ? e.key : e.code;
    const bool down = k == "ArrowDown" || k == "Down" || e.code == "ArrowDown";
    const bool up = k == "ArrowUp" || k == "Up" || e.code == "ArrowUp";
    const int n = static_cast<int>(st->hits.size());
    if (down || up) {
      // (also keeps the browser from moving the caret or opening its own list of earlier entries)
      e.preventDefault();
      if (n == 0) show();
      else mark(st->active < 0 ? (down ? 0 : n - 1) : st->active + (down ? 1 : -1));
    } else if ((k == "Enter" || e.code == "NumpadEnter") && n > 0) {
      e.preventDefault();
      choose(st->hits[static_cast<size_t>(std::max(0, st->active))]);
    } else if ((k == "Escape" || k == "Esc") && n > 0) {
      // close the list first; a second Escape reaches the menu
      e.preventDefault();
      clear();
    } else if (k == "Tab" && st->active >= 0) choose(st->hits[static_cast<size_t>(st->active)]);
    // typing must not drive the car or close the menu
    e.stopPropagation();
  });
  input.on("keyup", [](dom::Event& e) { e.stopPropagation(); });
  return {node, input};
}

}  // namespace fs
