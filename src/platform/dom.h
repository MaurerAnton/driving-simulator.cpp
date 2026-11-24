// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The page: elements, their properties, events – the browser's DOM by nearly the
 * same names, so that the original's interface code translates line by line:
 *
 *     const b = el('button', 'btn ghost', t('drawUndo'));      El b = el("button", "btn ghost", t("drawUndo"));
 *     b.type = 'button';                                        b.type("button");
 *     b.disabled = strokes.length === 0;                        b.disabled(strokes.empty());
 *     b.onclick = () => { strokes.pop(); rematch(); };          b.onclick([this] { strokes.pop_back(); rematch(); });
 *     box.append(text, undo, done);                             box.append(text, undo, done);
 *
 * An El is a reference to an element (copying it copies the reference, as in
 * JavaScript; two Els are equal when they are the same element).  In the browser
 * the elements are real ones (platform/web); natively they are a tree kept in
 * memory (platform/native/dom_native.cpp), which the tests read and click on.
 *
 * `window` and `document` are Els too, for their events: dom::window().on("resize", …).
 *
 * Handlers: `on(type, fn)` is addEventListener, `onclick(fn)` & co. are the element's
 * on… properties (setting one replaces the one before).  A handler lives as long as
 * its element.  Capture what it needs BY VALUE (Els, numbers) or `this` of an object
 * that outlives the element – not references to locals.
 */
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace fs::dom {

class El;

struct Rect {
  double left = 0, top = 0, width = 0, height = 0;
  double right() const { return left + width; }
  double bottom() const { return top + height; }
  double x() const { return left; }
  double y() const { return top; }
};

/** One finger of a TouchEvent (touches / changedTouches). */
struct TouchPoint {
  int identifier = 0;
  double clientX = 0, clientY = 0;
};

/** What a handler is told: the fields of the browser's events that the game reads. */
struct Event {
  std::string type;
  // keys
  std::string key, code;
  bool repeat = false;
  bool ctrlKey = false, altKey = false, shiftKey = false, metaKey = false;
  // pointer, mouse, wheel
  double clientX = 0, clientY = 0, offsetX = 0, offsetY = 0, movementX = 0, movementY = 0;
  int pointerId = 0;
  /** 'mouse' | 'touch' | 'pen' ('' for other events) */
  std::string pointerType;
  int button = 0, buttons = 0;
  bool isPrimary = true;
  /** event.isTrusted: the user did it (false: a script dispatched it) */
  bool isTrusted = true;
  double deltaX = 0, deltaY = 0;
  int deltaMode = 0;
  /** touch events: the fingers down now, and those this event is about */
  std::vector<TouchPoint> touches, changedTouches;
  // deviceorientation
  double alpha = 0, beta = 0, gamma = 0;
  bool hasOrientation = false;
  /** gamepadconnected / gamepaddisconnected: the pad's slot */
  int gamepadIndex = -1;

  void preventDefault() { defaultPrevented = true; }
  void stopPropagation() { propagationStopped = true; }
  bool defaultPrevented = false;
  bool propagationStopped = false;

  /** the element the event began at (event.target), and the one the handler is on (currentTarget) */
  El target() const;
  El currentTarget() const;
  int targetHandle = 0, currentHandle = 0;
};

using Handler = std::function<void(Event&)>;

/** addEventListener's options */
struct ListenOptions {
  bool capture = false, passive = false, once = false;
};

/** What the two hosts implement (handles are small numbers; 0 is "no element"). */
namespace backend {
int create(const char* tag);
void retain(int h);
void release(int h);
int special(int which);   // 0 window, 1 document, 2 body, 3 documentElement
int byId(const char* id);
int query(int h, const char* selector);
void queryAll(int h, const char* selector, std::vector<int>& out);
void append(int parent, int child);
void appendText(int parent, const char* text);
void prepend(int parent, int child);
void insertBefore(int parent, int child, int ref);
void after(int h, int other);
void before(int h, int other);
void remove(int h);
void clear(int h);
void replaceWith(int h, int other);
void setStr(int h, const char* prop, const char* value);
void setNum(int h, const char* prop, double value);
void setBool(int h, const char* prop, bool value);
std::string getStr(int h, const char* prop);
double getNum(int h, const char* prop);
bool getBool(int h, const char* prop);
void setAttr(int h, const char* name, const char* value);
bool hasAttr(int h, const char* name);
std::string getAttr(int h, const char* name);
void removeAttr(int h, const char* name);
void setStyle(int h, const char* prop, const char* value);
std::string getStyle(int h, const char* prop);
std::string computedStyle(int h, const char* prop);
/** op: 0 remove, 1 add, 2 toggle, 3 only ask; returns whether the class is there afterwards */
bool classOp(int h, const char* name, int op);
/** flags: 1 capture, 2 passive, 4 once */
void listen(int h, const char* type, int listener, int flags);
void unlisten(int h, const char* type, int listener, int flags);
/** el['on' + type] = listener (0: null); returns the listener that was there (0: none) */
int setHandler(int h, const char* type, int listener);
void rect(int h, double out[4]);
int parent(int h);
int child(int h, int index);
int childCount(int h);
int sibling(int h, int dir);
bool contains(int h, int other);
int closest(int h, const char* selector);
bool matches(int h, const char* selector);
bool isConnected(int h);
/** focus, blur, click, select, scrollIntoView, requestFullscreen, showPicker, play, pause, requestPointerLock … */
void call(int h, const char* method);
void pointerCapture(int h, int pointerId, bool on);
bool hasPointerCapture(int h, int pointerId);
/** document.fullscreenElement, document.activeElement, document.pointerLockElement (0: none) */
int documentElementOf(const char* which);
void exitFullscreen();
// (for dom::collect: see there)
/** one more than the largest handle in use */
int handleLimit();
/** how many Els name the handle now */
int refCount(int h);
/** for every handle below `limit`: is its element in the page (1/0), and which detached tree it hangs in (0: in the page, or no element) */
void liveness(int limit, std::vector<uint8_t>& connected, std::vector<int>& tree);
/** the page forgets what it kept for a listener that was dropped */
void forgetListener(int listener);
}  // namespace backend

namespace detail {
/** the handles of the Els copied while a census is taken (what a handler holds on to) */
extern std::vector<int>* census;
int addListener(Handler fn, int owner);
void dropListener(int id);
Handler wrap(std::function<void()> fn);
}  // namespace detail

class El {
 public:
  El() = default;
  El(const El& o) : h_(o.h_) {
    if (h_) {
      backend::retain(h_);
      if (detail::census) detail::census->push_back(h_);
    }
  }
  El(El&& o) noexcept : h_(o.h_) { o.h_ = 0; }
  El& operator=(const El& o) {
    if (o.h_ != h_) {
      if (o.h_) backend::retain(o.h_);
      if (h_) backend::release(h_);
      h_ = o.h_;
    }
    return *this;
  }
  El& operator=(El&& o) noexcept {
    if (this != &o) {
      if (h_) backend::release(h_);
      h_ = o.h_;
      o.h_ = 0;
    }
    return *this;
  }
  ~El() {
    if (h_) backend::release(h_);
  }
  /** an El for a handle the backend named (0 gives the empty El) */
  static El of(int h) {
    El e;
    e.h_ = h;
    if (h) backend::retain(h);
    return e;
  }
  int handle() const { return h_; }
  /** is there an element (`if (el)`, `el !== null`)? */
  explicit operator bool() const { return h_ != 0; }
  bool operator==(const El& o) const { return h_ == o.h_; }
  bool operator!=(const El& o) const { return h_ != o.h_; }

  // --- the tree ---------------------------------------------------------------
  const El& append(const El& child) const {
    if (child.h_) backend::append(h_, child.h_);
    return *this;
  }
  const El& append(const std::string& text) const {
    backend::appendText(h_, text.c_str());
    return *this;
  }
  const El& append(const char* text) const {
    backend::appendText(h_, text);
    return *this;
  }
  /** parent.append(a, b, 'text', c) */
  template <class A, class B, class... Rest>
  const El& append(const A& a, const B& b, const Rest&... rest) const {
    append(a);
    append(b);
    (append(rest), ...);
    return *this;
  }
  const El& append(const std::vector<El>& children) const {
    for (const El& c : children) append(c);
    return *this;
  }
  const El& prepend(const El& child) const {
    if (child.h_) backend::prepend(h_, child.h_);
    return *this;
  }
  /** parent.insertBefore(child, ref) (an empty ref: at the end) */
  const El& insertBefore(const El& child, const El& ref) const {
    backend::insertBefore(h_, child.h_, ref.h_);
    return *this;
  }
  void after(const El& other) const { backend::after(h_, other.h_); }
  void before(const El& other) const { backend::before(h_, other.h_); }
  void remove() const {
    if (h_) backend::remove(h_);
  }
  /** replaceChildren(): empty it;  replaceChildren(a, b): … and put these in */
  const El& replaceChildren() const {
    backend::clear(h_);
    return *this;
  }
  template <class... Kids>
  const El& replaceChildren(const Kids&... kids) const {
    backend::clear(h_);
    (append(kids), ...);
    return *this;
  }
  void replaceWith(const El& other) const { backend::replaceWith(h_, other.h_); }
  El parentElement() const { return of(backend::parent(h_)); }
  El firstElementChild() const { return of(backend::child(h_, 0)); }
  El lastElementChild() const {
    int n = backend::childCount(h_);
    return of(n > 0 ? backend::child(h_, n - 1) : 0);
  }
  El nextElementSibling() const { return of(backend::sibling(h_, 1)); }
  El previousElementSibling() const { return of(backend::sibling(h_, -1)); }
  int childElementCount() const { return backend::childCount(h_); }
  /** children[i] */
  El child(int i) const { return of(backend::child(h_, i)); }
  std::vector<El> children() const {
    std::vector<El> out;
    int n = backend::childCount(h_);
    for (int i = 0; i < n; i++) out.push_back(child(i));
    return out;
  }
  bool contains(const El& other) const { return other.h_ && backend::contains(h_, other.h_); }
  El closest(const std::string& selector) const { return of(backend::closest(h_, selector.c_str())); }
  bool matches(const std::string& selector) const { return backend::matches(h_, selector.c_str()); }
  El querySelector(const std::string& selector) const { return of(backend::query(h_, selector.c_str())); }
  std::vector<El> querySelectorAll(const std::string& selector) const {
    std::vector<int> hs;
    backend::queryAll(h_, selector.c_str(), hs);
    std::vector<El> out;
    for (int h : hs) out.push_back(of(h));
    return out;
  }
  bool isConnected() const { return h_ && backend::isConnected(h_); }

  // --- properties: el[prop] ----------------------------------------------------
  const El& set(const char* prop, const std::string& v) const {
    backend::setStr(h_, prop, v.c_str());
    return *this;
  }
  const El& set(const char* prop, const char* v) const {
    backend::setStr(h_, prop, v);
    return *this;
  }
  const El& set(const char* prop, double v) const {
    backend::setNum(h_, prop, v);
    return *this;
  }
  const El& set(const char* prop, int v) const {
    backend::setNum(h_, prop, v);
    return *this;
  }
  const El& set(const char* prop, bool v) const {
    backend::setBool(h_, prop, v);
    return *this;
  }
  std::string get(const char* prop) const { return backend::getStr(h_, prop); }
  double num(const char* prop) const { return backend::getNum(h_, prop); }
  bool flag(const char* prop) const { return backend::getBool(h_, prop); }

  // (the ones the game uses most, by name)
  const El& textContent(const std::string& v) const { return set("textContent", v); }
  std::string textContent() const { return get("textContent"); }
  const El& innerHTML(const std::string& v) const { return set("innerHTML", v); }
  const El& className(const std::string& v) const { return set("className", v); }
  std::string className() const { return get("className"); }
  const El& id(const std::string& v) const { return set("id", v); }
  const El& title(const std::string& v) const { return set("title", v); }
  const El& hidden(bool v) const { return set("hidden", v); }
  bool hidden() const { return flag("hidden"); }
  const El& disabled(bool v) const { return set("disabled", v); }
  bool disabled() const { return flag("disabled"); }
  const El& checked(bool v) const { return set("checked", v); }
  bool checked() const { return flag("checked"); }
  const El& value(const std::string& v) const { return set("value", v); }
  const El& value(double v) const { return set("value", v); }
  std::string value() const { return get("value"); }
  double valueAsNumber() const { return num("valueAsNumber"); }
  const El& type(const std::string& v) const { return set("type", v); }
  const El& name(const std::string& v) const { return set("name", v); }
  const El& href(const std::string& v) const { return set("href", v); }
  const El& src(const std::string& v) const { return set("src", v); }
  const El& placeholder(const std::string& v) const { return set("placeholder", v); }
  const El& lang(const std::string& v) const { return set("lang", v); }
  const El& dir(const std::string& v) const { return set("dir", v); }
  const El& tabIndex(int v) const { return set("tabIndex", v); }
  const El& width(double v) const { return set("width", v); }
  const El& height(double v) const { return set("height", v); }
  double width() const { return num("width"); }
  double height() const { return num("height"); }
  double clientWidth() const { return num("clientWidth"); }
  double clientHeight() const { return num("clientHeight"); }
  double offsetWidth() const { return num("offsetWidth"); }
  double offsetHeight() const { return num("offsetHeight"); }
  double scrollTop() const { return num("scrollTop"); }
  const El& scrollTop(double v) const { return set("scrollTop", v); }
  double scrollHeight() const { return num("scrollHeight"); }

  // --- attributes, style, classes -----------------------------------------------
  const El& setAttribute(const std::string& name, const std::string& value) const {
    backend::setAttr(h_, name.c_str(), value.c_str());
    return *this;
  }
  /** getAttribute: '' when it is not there (ask hasAttribute where that differs from an empty one) */
  std::string getAttribute(const std::string& name) const { return backend::getAttr(h_, name.c_str()); }
  bool hasAttribute(const std::string& name) const { return backend::hasAttr(h_, name.c_str()); }
  const El& removeAttribute(const std::string& name) const {
    backend::removeAttr(h_, name.c_str());
    return *this;
  }
  /** el.dataset.name = value (name in camelCase, as in the original) */
  const El& dataset(const std::string& name, const std::string& value) const;
  std::string dataset(const std::string& name) const;
  /**
   * el.style.name = value – `name` as CSS writes it ('font-size', '--x') or as the
   * style object does ('fontSize'); an empty value takes the property away
   */
  const El& style(const std::string& name, const std::string& value) const {
    backend::setStyle(h_, name.c_str(), value.c_str());
    return *this;
  }
  std::string style(const std::string& name) const { return backend::getStyle(h_, name.c_str()); }
  /** getComputedStyle(el).getPropertyValue(name) */
  std::string computedStyle(const std::string& name) const { return backend::computedStyle(h_, name.c_str()); }
  /** classList.add / remove / toggle(name) / toggle(name, on) / contains */
  const El& addClass(const std::string& name) const {
    backend::classOp(h_, name.c_str(), 1);
    return *this;
  }
  const El& removeClass(const std::string& name) const {
    backend::classOp(h_, name.c_str(), 0);
    return *this;
  }
  bool toggleClass(const std::string& name) const { return backend::classOp(h_, name.c_str(), 2); }
  bool toggleClass(const std::string& name, bool on) const { return backend::classOp(h_, name.c_str(), on ? 1 : 0); }
  bool hasClass(const std::string& name) const { return backend::classOp(h_, name.c_str(), 3); }

  // --- events -------------------------------------------------------------------
  using Options = ListenOptions;
  /** addEventListener: returns what off() takes */
  int on(const std::string& type, Handler fn, Options o = Options()) const {
    int id = detail::addListener(std::move(fn), h_);
    backend::listen(h_, type.c_str(), id, (o.capture ? 1 : 0) | (o.passive ? 2 : 0) | (o.once ? 4 : 0));
    return id;
  }
  int on(const std::string& type, std::function<void()> fn, Options o = Options()) const { return on(type, detail::wrap(std::move(fn)), o); }
  /** removeEventListener (with the capture flag the listener was added with) */
  void off(const std::string& type, int listener, bool capture = false) const {
    backend::unlisten(h_, type.c_str(), listener, capture ? 1 : 0);
    detail::dropListener(listener);
  }
  /** el.on<type> = fn (replaces what was there); an empty fn is `= null` */
  const El& handler(const std::string& type, Handler fn) const {
    int id = fn ? detail::addListener(std::move(fn), h_) : 0;
    int old = backend::setHandler(h_, type.c_str(), id);
    if (old) detail::dropListener(old);
    return *this;
  }
  const El& onclick(std::function<void()> fn) const { return handler("click", fn ? detail::wrap(std::move(fn)) : Handler()); }
  const El& onclick(Handler fn) const { return handler("click", std::move(fn)); }
  const El& onchange(std::function<void()> fn) const { return handler("change", detail::wrap(std::move(fn))); }
  const El& oninput(std::function<void()> fn) const { return handler("input", detail::wrap(std::move(fn))); }
  const El& onpointerdown(Handler fn) const { return handler("pointerdown", std::move(fn)); }
  const El& onpointermove(Handler fn) const { return handler("pointermove", std::move(fn)); }
  const El& onpointerup(Handler fn) const { return handler("pointerup", std::move(fn)); }
  const El& onpointercancel(Handler fn) const { return handler("pointercancel", std::move(fn)); }
  const El& onkeydown(Handler fn) const { return handler("keydown", std::move(fn)); }

  // --- the rest -------------------------------------------------------------------
  Rect getBoundingClientRect() const {
    double r[4] = {0, 0, 0, 0};
    backend::rect(h_, r);
    return Rect{r[0], r[1], r[2], r[3]};
  }
  void focus() const { backend::call(h_, "focus"); }
  void blur() const { backend::call(h_, "blur"); }
  void click() const { backend::call(h_, "click"); }
  void select() const { backend::call(h_, "select"); }
  void scrollIntoView() const { backend::call(h_, "scrollIntoView"); }
  /** any other method without arguments */
  void call(const char* method) const { backend::call(h_, method); }
  void setPointerCapture(int pointerId) const { backend::pointerCapture(h_, pointerId, true); }
  void releasePointerCapture(int pointerId) const { backend::pointerCapture(h_, pointerId, false); }
  bool hasPointerCapture(int pointerId) const { return backend::hasPointerCapture(h_, pointerId); }

 private:
  int h_ = 0;
};

inline El Event::target() const { return El::of(targetHandle); }
inline El Event::currentTarget() const { return El::of(currentHandle); }

/** document.createElement(tag) with a class and a text: the original's `el(tag, cls, text)` */
El el(const char* tag, const std::string& cls = std::string(), const std::string& text = std::string());
El window();
El document();
El body();
/** document.documentElement (<html>) */
El root();
El getElementById(const std::string& id);
/** document.querySelector */
El querySelector(const std::string& selector);
/** document.fullscreenElement / activeElement / pointerLockElement (empty: none) */
El fullscreenElement();
El activeElement();
El pointerLockElement();
void exitFullscreen();

/**
 * Let go of the handlers of elements nothing can reach any more.
 *
 * A handler usually names elements (`b.onclick([b, panel] { … })`), and so an
 * element and its own handler hold each other: without help, every menu that was
 * built and thrown away would stay in memory for ever.  collect() finds the
 * elements that are neither in the page nor named by anything but handlers of
 * elements that are themselves gone (what a handler names is noted when it is
 * added), and drops their handlers – after which the elements go by themselves.
 * The game calls it now and then, between two frames.  Returns how many handlers went.
 */
int collect();

}  // namespace fs::dom
