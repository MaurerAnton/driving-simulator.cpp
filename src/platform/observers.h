// SPDX-License-Identifier: AGPL-3.0-only
/**
 * What the page tells about its elements beside events (dom.h): `ResizeObserver`,
 * `MutationObserver` (attributes only) and `HTMLElement.offsetParent`.
 *
 *     const ro = new ResizeObserver(place);          plat::ResizeObserver ro(place);
 *     ro.observe(panel);                             ro.observe(panel);
 *     const mo = new MutationObserver(place);        plat::MutationObserver mo(place);
 *     mo.observe(panel, { attributes: true,          mo.observe(panel, {"hidden", "class", "style"});
 *       attributeFilter: ['hidden', 'class', 'style'] });
 *     ro.disconnect();                               ro.disconnect();
 *
 * An observer is a reference to the browser's one (copying it copies the reference); it
 * observes until disconnect() – also after the last copy is gone, as in JavaScript while
 * the observed element lives.  The callback takes no arguments (the game looks at the
 * element itself).
 *
 * Two hosts implement the backend: the browser (platform/web: the real observers) and this
 * machine (platform/native/maps_native.cpp: nothing is laid out, so nothing fires by itself –
 * a test calls plat::native::notifyResize(el) / notifyMutation(el, attribute), maps_native.h).
 */
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "platform/dom.h"

namespace fs::plat {

namespace observerBackend {
/** `new ResizeObserver(fn)` (kind 0) / `new MutationObserver(fn)` (kind 1): a handle (never 0) */
int create(int kind, std::function<void()> fn);
/** observer.observe(element) – for a MutationObserver with { attributes: true, attributeFilter } */
void observe(int observer, int element, const std::vector<std::string>& attributeFilter);
/** observer.disconnect(): it observes nothing any more, and its callback is let go of */
void disconnect(int observer);
/** element.offsetParent (0: none) */
int offsetParent(int element);
}  // namespace observerBackend

class ResizeObserver {
 public:
  ResizeObserver() = default;
  explicit ResizeObserver(std::function<void()> fn) : h_(observerBackend::create(0, std::move(fn))) {}
  void observe(const dom::El& target) const { observerBackend::observe(h_, target.handle(), {}); }
  void disconnect() const { observerBackend::disconnect(h_); }

 private:
  int h_ = 0;
};

class MutationObserver {
 public:
  MutationObserver() = default;
  explicit MutationObserver(std::function<void()> fn) : h_(observerBackend::create(1, std::move(fn))) {}
  /** observe(target, { attributes: true, attributeFilter }) – an empty filter: every attribute */
  void observe(const dom::El& target, const std::vector<std::string>& attributeFilter = {}) const { observerBackend::observe(h_, target.handle(), attributeFilter); }
  void disconnect() const { observerBackend::disconnect(h_); }

 private:
  int h_ = 0;
};

/** `el.offsetParent`: the element its offsetLeft/offsetTop count from (empty: none – not in the page, or fixed) */
inline dom::El offsetParent(const dom::El& e) { return dom::El::of(observerBackend::offsetParent(e.handle())); }

}  // namespace fs::plat
