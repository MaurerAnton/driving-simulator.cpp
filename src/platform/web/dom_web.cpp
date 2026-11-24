// SPDX-License-Identifier: AGPL-3.0-only
/**
 * dom.h in the browser: every call goes to the element behind the handle
 * (platform/web/library_fahrsim.js); how many Els name a handle is counted here,
 * and the page's table lets go of an element when nothing names it any more.
 */
#include <emscripten.h>

#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "platform/dom.h"
#include "platform/dom_internal.h"

extern "C" {
void fsd_init(double* numbers, char* texts);
void fsd_free(int h);
int fsd_create(const char* tag);
int fsd_by_id(const char* id);
int fsd_query(int h, const char* selector);
int fsd_query_all(int h, const char* selector, int* out, int max);
void fsd_append(int p, int c);
void fsd_append_text(int p, const char* text);
void fsd_prepend(int p, int c);
void fsd_insert_before(int p, int c, int ref);
void fsd_after(int h, int o);
void fsd_before(int h, int o);
void fsd_remove(int h);
void fsd_clear(int h);
void fsd_replace_with(int h, int o);
void fsd_set_str(int h, const char* prop, const char* v);
void fsd_set_num(int h, const char* prop, double v);
void fsd_set_bool(int h, const char* prop, int v);
char* fsd_get_str(int h, const char* prop);
double fsd_get_num(int h, const char* prop);
int fsd_get_bool(int h, const char* prop);
void fsd_set_attr(int h, const char* name, const char* v);
int fsd_has_attr(int h, const char* name);
char* fsd_get_attr(int h, const char* name);
void fsd_remove_attr(int h, const char* name);
void fsd_set_style(int h, const char* prop, const char* v);
char* fsd_get_style(int h, const char* prop);
char* fsd_computed_style(int h, const char* prop);
int fsd_class_op(int h, const char* name, int op);
void fsd_listen(int h, const char* type, int id, int flags);
void fsd_unlisten(int h, const char* type, int id, int flags);
int fsd_set_handler(int h, const char* type, int id);
void fsd_rect(int h, double* out);
int fsd_parent(int h);
int fsd_child(int h, int i);
int fsd_child_count(int h);
int fsd_sibling(int h, int dir);
int fsd_contains(int h, int o);
int fsd_closest(int h, const char* selector);
int fsd_matches(int h, const char* selector);
int fsd_is_connected(int h);
void fsd_call(int h, const char* method);
void fsd_pointer_capture(int h, int id, int on);
int fsd_has_pointer_capture(int h, int id);
int fsd_document_element(const char* which);
void fsd_exit_fullscreen();
int fsd_handle_limit();
void fsd_connected_map(uint8_t* out, int n);
void fsd_tree_map(int* out, int n);
void fsd_forget_listener(int id);
}

namespace fs::dom {

namespace {

/** how many Els name each handle */
std::vector<int>& refs() {
  static std::vector<int> r;
  return r;
}

/** where the page writes an event for us (see FSD.dispatch) */
double eventNumbers[23 + 48];
char eventTexts[160];
bool ready = false;

void init() {
  if (ready) return;
  ready = true;
  fsd_init(eventNumbers, eventTexts);
}

std::string take(char* s) {
  if (!s) return std::string();
  std::string out(s);
  std::free(s);
  return out;
}

}  // namespace

namespace backend {

int create(const char* tag) {
  init();
  return fsd_create(tag);
}
void retain(int h) {
  auto& r = refs();
  if (static_cast<size_t>(h) >= r.size()) r.resize(static_cast<size_t>(h) + 64, 0);
  r[static_cast<size_t>(h)]++;
}
void release(int h) {
  auto& r = refs();
  if (static_cast<size_t>(h) >= r.size()) return;
  if (--r[static_cast<size_t>(h)] <= 0) {
    r[static_cast<size_t>(h)] = 0;
    fsd_free(h);
  }
}
int special(int which) {
  init();
  return which + 1;
}
int byId(const char* id) {
  init();
  return fsd_by_id(id);
}
int query(int h, const char* selector) { return fsd_query(h, selector); }
void queryAll(int h, const char* selector, std::vector<int>& out) {
  int buf[256];
  int n = fsd_query_all(h, selector, buf, 256);
  if (n > 256) {
    std::vector<int> big(static_cast<size_t>(n));
    n = fsd_query_all(h, selector, big.data(), n);
    out.assign(big.begin(), big.begin() + n);
  } else out.assign(buf, buf + n);
}
void append(int parent, int child) { fsd_append(parent, child); }
void appendText(int parent, const char* text) { fsd_append_text(parent, text); }
void prepend(int parent, int child) { fsd_prepend(parent, child); }
void insertBefore(int parent, int child, int ref) { fsd_insert_before(parent, child, ref); }
void after(int h, int other) { fsd_after(h, other); }
void before(int h, int other) { fsd_before(h, other); }
void remove(int h) { fsd_remove(h); }
void clear(int h) { fsd_clear(h); }
void replaceWith(int h, int other) { fsd_replace_with(h, other); }
void setStr(int h, const char* prop, const char* value) { fsd_set_str(h, prop, value); }
void setNum(int h, const char* prop, double value) { fsd_set_num(h, prop, value); }
void setBool(int h, const char* prop, bool value) { fsd_set_bool(h, prop, value ? 1 : 0); }
std::string getStr(int h, const char* prop) { return take(fsd_get_str(h, prop)); }
double getNum(int h, const char* prop) { return fsd_get_num(h, prop); }
bool getBool(int h, const char* prop) { return fsd_get_bool(h, prop) != 0; }
void setAttr(int h, const char* name, const char* value) { fsd_set_attr(h, name, value); }
bool hasAttr(int h, const char* name) { return fsd_has_attr(h, name) != 0; }
std::string getAttr(int h, const char* name) { return take(fsd_get_attr(h, name)); }
void removeAttr(int h, const char* name) { fsd_remove_attr(h, name); }
void setStyle(int h, const char* prop, const char* value) { fsd_set_style(h, prop, value); }
std::string getStyle(int h, const char* prop) { return take(fsd_get_style(h, prop)); }
std::string computedStyle(int h, const char* prop) { return take(fsd_computed_style(h, prop)); }
bool classOp(int h, const char* name, int op) { return fsd_class_op(h, name, op) != 0; }
void listen(int h, const char* type, int listener, int flags) { fsd_listen(h, type, listener, flags); }
void unlisten(int h, const char* type, int listener, int flags) { fsd_unlisten(h, type, listener, flags); }
int setHandler(int h, const char* type, int listener) { return fsd_set_handler(h, type, listener); }
void rect(int h, double out[4]) { fsd_rect(h, out); }
int parent(int h) { return fsd_parent(h); }
int child(int h, int index) { return fsd_child(h, index); }
int childCount(int h) { return fsd_child_count(h); }
int sibling(int h, int dir) { return fsd_sibling(h, dir); }
bool contains(int h, int other) { return fsd_contains(h, other) != 0; }
int closest(int h, const char* selector) { return fsd_closest(h, selector); }
bool matches(int h, const char* selector) { return fsd_matches(h, selector) != 0; }
bool isConnected(int h) { return fsd_is_connected(h) != 0; }
void call(int h, const char* method) { fsd_call(h, method); }
void pointerCapture(int h, int pointerId, bool on) { fsd_pointer_capture(h, pointerId, on ? 1 : 0); }
bool hasPointerCapture(int h, int pointerId) { return fsd_has_pointer_capture(h, pointerId) != 0; }
int documentElementOf(const char* which) {
  init();
  return fsd_document_element(which);
}
void exitFullscreen() { fsd_exit_fullscreen(); }

int handleLimit() { return fsd_handle_limit(); }
int refCount(int h) {
  auto& r = refs();
  return static_cast<size_t>(h) < r.size() ? r[static_cast<size_t>(h)] : 0;
}
void liveness(int limit, std::vector<uint8_t>& connected, std::vector<int>& tree) {
  connected.assign(static_cast<size_t>(limit), 0);
  tree.assign(static_cast<size_t>(limit), 0);
  fsd_connected_map(connected.data(), limit);
  fsd_tree_map(tree.data(), limit);
}
void forgetListener(int listener) { fsd_forget_listener(listener); }

}  // namespace backend

}  // namespace fs::dom

extern "C" {

/** An event has come for a listener: the page wrote it to eventNumbers/eventTexts. Returns 1: prevent the default, 2: stop it here. */
EMSCRIPTEN_KEEPALIVE int fsd_event(int listener) {
  using namespace fs::dom;
  Event e;
  const double* n = eventNumbers;
  e.clientX = n[0];
  e.clientY = n[1];
  e.offsetX = n[2];
  e.offsetY = n[3];
  e.movementX = n[4];
  e.movementY = n[5];
  e.pointerId = static_cast<int>(n[6]);
  e.button = static_cast<int>(n[7]);
  e.buttons = static_cast<int>(n[8]);
  e.isPrimary = n[9] != 0;
  e.deltaX = n[10];
  e.deltaY = n[11];
  e.deltaMode = static_cast<int>(n[12]);
  int mods = static_cast<int>(n[13]);
  e.ctrlKey = (mods & 1) != 0;
  e.altKey = (mods & 2) != 0;
  e.shiftKey = (mods & 4) != 0;
  e.metaKey = (mods & 8) != 0;
  e.repeat = (mods & 16) != 0;
  e.isTrusted = (mods & 32) == 0;
  e.alpha = n[14];
  e.beta = n[15];
  e.gamma = n[16];
  e.hasOrientation = n[17] != 0;
  e.gamepadIndex = static_cast<int>(n[18]);
  e.targetHandle = static_cast<int>(n[19]);
  e.currentHandle = static_cast<int>(n[20]);
  int touches = static_cast<int>(n[21]), changed = static_cast<int>(n[22]);
  const double* t = n + 23;
  for (int i = 0; i < touches; i++, t += 3) e.touches.push_back(TouchPoint{static_cast<int>(t[0]), t[1], t[2]});
  for (int i = 0; i < changed; i++, t += 3) e.changedTouches.push_back(TouchPoint{static_cast<int>(t[0]), t[1], t[2]});
  e.type = eventTexts;
  e.key = eventTexts + 48;
  e.code = eventTexts + 96;
  e.pointerType = eventTexts + 144;
  // (the two elements stay named while the handler runs, and are let go after it if nobody kept them)
  El target = El::of(e.targetHandle), current = El::of(e.currentHandle);
  detail::dispatch(listener, e);
  return (e.defaultPrevented ? 1 : 0) | (e.propagationStopped ? 2 : 0);
}

/** A listener that was added with `once` has run. */
EMSCRIPTEN_KEEPALIVE void fsd_listener_gone(int listener) { fs::dom::detail::dropListener(listener); }

}  // extern "C"
