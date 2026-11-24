// SPDX-License-Identifier: AGPL-3.0-only
/**
 * dom.h on this machine: the page as a tree kept in memory.  Enough of a browser
 * for the interface code to run and for the tests to read what it built, click
 * its buttons and type into its fields – nothing is laid out or painted (every
 * rectangle is what a test set it to).
 */
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "platform/dom.h"
#include "platform/dom_internal.h"
#include "platform/native/dom_native.h"

namespace fs::dom {

namespace {

struct Listener {
  std::string type;
  int id;
  int flags;
};

struct Node {
  std::string tag;
  /** text nodes: their text */
  std::string data;
  std::map<std::string, std::string> props, attrs, styles;
  std::vector<std::string> classes;
  std::vector<int> kids;
  int parent = 0;
  std::vector<Listener> listeners;
  std::map<std::string, int> handlers;
  double rect[4] = {0, 0, 0, 0};
  bool connectedRoot = false;
  int refs = 0;
};

struct Page {
  std::vector<std::unique_ptr<Node>> nodes;   // by handle (0: none)
  int win = 0, doc = 0, body = 0, html = 0;
  int focused = 0, fullscreen = 0;
  std::map<int, int> capture;   // pointer → the element that has it
};

Page& page();

int make(const std::string& tag) {
  Page& p = page();
  p.nodes.push_back(std::make_unique<Node>());
  p.nodes.back()->tag = tag;
  return static_cast<int>(p.nodes.size()) - 1;
}

Page& page() {
  static Page* p = nullptr;
  if (!p) {
    p = new Page();
    p->nodes.push_back(nullptr);
    p->win = make("#window");
    p->doc = make("#document");
    p->html = make("html");
    p->body = make("body");
    p->nodes[p->html]->connectedRoot = true;
    p->nodes[p->html]->kids.push_back(p->body);
    p->nodes[p->body]->parent = p->html;
  }
  return *p;
}

Node* node(int h) {
  Page& p = page();
  if (h <= 0 || h >= static_cast<int>(p.nodes.size())) return nullptr;
  return p.nodes[h].get();
}

bool isText(const Node& n) { return n.tag == "#text"; }

void detach(int h) {
  Node* n = node(h);
  if (!n || !n->parent) return;
  Node* par = node(n->parent);
  for (size_t i = 0; i < par->kids.size(); i++)
    if (par->kids[i] == h) {
      par->kids.erase(par->kids.begin() + static_cast<long>(i));
      break;
    }
  n->parent = 0;
}

void insertAt(int parent, int child, size_t index) {
  Node* par = node(parent);
  Node* c = node(child);
  if (!par || !c || parent == child) return;
  // (an element that is moved inside its own parent: the index is meant without it)
  if (c->parent == parent) {
    for (size_t i = 0; i < par->kids.size(); i++)
      if (par->kids[i] == child) {
        if (i < index) index--;
        break;
      }
  }
  detach(child);
  if (index > par->kids.size()) index = par->kids.size();
  par->kids.insert(par->kids.begin() + static_cast<long>(index), child);
  c->parent = parent;
}

size_t indexIn(int parent, int child) {
  Node* par = node(parent);
  for (size_t i = 0; par && i < par->kids.size(); i++)
    if (par->kids[i] == child) return i;
  return par ? par->kids.size() : 0;
}

std::string textOf(int h) {
  Node* n = node(h);
  if (!n) return "";
  if (isText(*n)) return n->data;
  std::string out;
  for (int k : n->kids) out += textOf(k);
  return out;
}

std::vector<std::string> splitWords(const std::string& s) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : s) {
    if (c == ' ' || c == '\t' || c == '\n') {
      if (!cur.empty()) out.push_back(cur);
      cur.clear();
    } else cur += c;
  }
  if (!cur.empty()) out.push_back(cur);
  return out;
}

std::string joinWords(const std::vector<std::string>& w) {
  std::string out;
  for (size_t i = 0; i < w.size(); i++) {
    if (i) out += ' ';
    out += w[i];
  }
  return out;
}

bool hasClass(const Node& n, const std::string& c) {
  for (const auto& x : n.classes)
    if (x == c) return true;
  return false;
}

// --- selectors: tag, #id, .class, [attr], [attr=value], :not(.class) joined, descendants (space), children (>), lists (,) ---

struct Simple {
  std::string tag, id;
  std::vector<std::string> classes, notClasses;
  std::vector<std::pair<std::string, std::string>> attrs;   // value "\x01": only has to be there
  char combinator = ' ';   // how it hangs on the one before: ' ' descendant, '>' child
};

std::vector<std::vector<Simple>> parseSelector(const std::string& sel) {
  std::vector<std::vector<Simple>> list;
  std::vector<Simple> chain;
  Simple cur;
  bool any = false;
  char pending = ' ';
  auto flush = [&]() {
    if (any) {
      cur.combinator = pending;
      chain.push_back(cur);
      pending = ' ';
    }
    cur = Simple();
    any = false;
  };
  size_t i = 0;
  auto word = [&]() {
    std::string w;
    while (i < sel.size() && (std::isalnum(static_cast<unsigned char>(sel[i])) || sel[i] == '-' || sel[i] == '_')) w += sel[i++];
    return w;
  };
  while (i < sel.size()) {
    char c = sel[i];
    if (c == ' ' || c == '>') {
      flush();
      if (c == '>') pending = '>';
      i++;
    } else if (c == ',') {
      flush();
      if (!chain.empty()) list.push_back(chain);
      chain.clear();
      i++;
    } else if (c == '#') {
      i++;
      cur.id = word();
      any = true;
    } else if (c == '.') {
      i++;
      cur.classes.push_back(word());
      any = true;
    } else if (c == '[') {
      i++;
      std::string name = word(), value = "\x01";
      if (i < sel.size() && sel[i] == '=') {
        i++;
        value.clear();
        char q = (i < sel.size() && (sel[i] == '"' || sel[i] == '\'')) ? sel[i++] : 0;
        while (i < sel.size() && sel[i] != (q ? q : ']')) value += sel[i++];
        if (q && i < sel.size()) i++;
      }
      if (i < sel.size() && sel[i] == ']') i++;
      cur.attrs.emplace_back(name, value);
      any = true;
    } else if (c == ':') {
      if (sel.compare(i, 6, ":not(.") == 0) {
        i += 6;
        cur.notClasses.push_back(word());
        if (i < sel.size() && sel[i] == ')') i++;
        any = true;
      } else {
        // (other pseudo-classes are not known here: the selector matches nothing)
        cur.tag = "\x02";
        any = true;
        i++;
        word();
      }
    } else if (c == '*') {
      any = true;
      i++;
    } else {
      std::string w = word();
      if (w.empty()) {
        i++;
        continue;
      }
      cur.tag = w;
      any = true;
    }
  }
  flush();
  if (!chain.empty()) list.push_back(chain);
  return list;
}

bool matchSimple(const Node& n, const Simple& s) {
  if (isText(n) || n.tag[0] == '#') return false;
  if (!s.tag.empty() && s.tag != n.tag) return false;
  if (!s.id.empty()) {
    auto it = n.props.find("id");
    if (it == n.props.end() || it->second != s.id) return false;
  }
  for (const auto& c : s.classes)
    if (!hasClass(n, c)) return false;
  for (const auto& c : s.notClasses)
    if (hasClass(n, c)) return false;
  for (const auto& a : s.attrs) {
    auto it = n.attrs.find(a.first);
    std::string have;
    bool there = it != n.attrs.end();
    if (there) have = it->second;
    else {
      auto p = n.props.find(a.first);
      if (p != n.props.end()) {
        there = true;
        have = p->second;
      }
    }
    if (!there) return false;
    if (a.second != "\x01" && have != a.second) return false;
  }
  return true;
}

bool matchChain(int h, const std::vector<Simple>& chain, int upTo) {
  // the last part must match the element itself, the ones before it its ancestors
  size_t k = chain.size();
  Node* n = node(h);
  if (!n || !matchSimple(*n, chain[k - 1])) return false;
  char comb = chain[k - 1].combinator;
  int cur = n->parent;
  k--;
  while (k > 0) {
    bool found = false;
    while (cur && cur != upTo) {
      Node* a = node(cur);
      if (a && matchSimple(*a, chain[k - 1])) {
        found = true;
        break;
      }
      if (comb == '>') break;
      cur = a ? a->parent : 0;
    }
    if (!found) return false;
    comb = chain[k - 1].combinator;
    cur = node(cur)->parent;
    k--;
  }
  return true;
}

bool matchesSel(int h, const std::string& selector, int scope = 0) {
  for (const auto& chain : parseSelector(selector))
    if (!chain.empty() && matchChain(h, chain, scope ? node(scope)->parent : 0)) return true;
  return false;
}

void collect(int root, const std::string& selector, std::vector<int>& out, bool first) {
  Node* n = node(root);
  if (!n) return;
  for (int k : n->kids) {
    if (first && !out.empty()) return;
    Node* c = node(k);
    if (!c || isText(*c)) continue;
    if (matchesSel(k, selector)) {
      out.push_back(k);
      if (first) return;
    }
    collect(k, selector, out, first);
  }
}

int rootOf(int h) {
  int cur = h;
  for (;;) {
    Node* n = node(cur);
    if (!n || !n->parent) return cur;
    cur = n->parent;
  }
}

bool connected(int h) {
  Node* r = node(rootOf(h));
  return r && r->connectedRoot;
}

void run(int listener, Event& e) { detail::dispatch(listener, e); }

/** Send an event to an element: its own handlers, then (for events that bubble) its ancestors', the document's, the window's. */
void fire(int target, Event& e, bool bubbles) {
  Page& p = page();
  std::vector<int> path;
  for (int cur = target; cur; cur = node(cur) ? node(cur)->parent : 0) path.push_back(cur);
  if (target != p.win && target != p.doc && connected(target)) {
    path.push_back(p.doc);
    path.push_back(p.win);
  } else if (target == p.doc) path.push_back(p.win);
  e.targetHandle = target;
  // the capturing listeners first, from the outside in
  for (size_t i = path.size(); i-- > 0;) {
    Node* n = node(path[i]);
    if (!n) continue;
    e.currentHandle = path[i];
    std::vector<Listener> ls = n->listeners;
    for (const Listener& l : ls) {
      if (!(l.flags & 1) || l.type != e.type) continue;
      run(l.id, e);
      if (l.flags & 4) backend::unlisten(path[i], l.type.c_str(), l.id, l.flags);
    }
    if (e.propagationStopped) return;
  }
  for (size_t i = 0; i < path.size(); i++) {
    Node* n = node(path[i]);
    if (!n) continue;
    e.currentHandle = path[i];
    auto hit = n->handlers.find(e.type);
    if (hit != n->handlers.end() && hit->second) run(hit->second, e);
    std::vector<Listener> ls = n->listeners;
    for (const Listener& l : ls) {
      if ((l.flags & 1) || l.type != e.type) continue;
      run(l.id, e);
      if (l.flags & 4) backend::unlisten(path[i], l.type.c_str(), l.id, l.flags);
    }
    if (e.propagationStopped || !bubbles) return;
  }
}

bool isBoolProp(const std::string& p) {
  return p == "hidden" || p == "disabled" || p == "checked" || p == "open" || p == "selected" || p == "multiple" || p == "draggable" || p == "spellcheck" ||
         p == "readOnly" || p == "required" || p == "muted" || p == "loop" || p == "indeterminate";
}

std::string escapeHtml(const std::string& s) {
  std::string out;
  for (char c : s) {
    if (c == '<') out += "&lt;";
    else if (c == '>') out += "&gt;";
    else if (c == '&') out += "&amp;";
    else if (c == '"') out += "&quot;";
    else out += c;
  }
  return out;
}

void dump(int h, std::string& out, int depth, bool pretty) {
  Node* n = node(h);
  if (!n) return;
  std::string pad = pretty ? std::string(static_cast<size_t>(depth) * 2, ' ') : "";
  if (isText(*n)) {
    out += pad + escapeHtml(n->data) + (pretty ? "\n" : "");
    return;
  }
  out += pad + "<" + n->tag;
  if (!n->classes.empty()) out += " class=\"" + escapeHtml(joinWords(n->classes)) + "\"";
  for (const auto& kv : n->props) {
    if (kv.first == "innerHTML") continue;
    if (isBoolProp(kv.first)) {
      if (kv.second == "1") out += " " + kv.first;
    } else out += " " + kv.first + "=\"" + escapeHtml(kv.second) + "\"";
  }
  for (const auto& kv : n->attrs) out += " " + kv.first + "=\"" + escapeHtml(kv.second) + "\"";
  if (!n->styles.empty()) {
    out += " style=\"";
    for (const auto& kv : n->styles) out += kv.first + ": " + escapeHtml(kv.second) + "; ";
    out += "\"";
  }
  out += ">";
  auto raw = n->props.find("innerHTML");
  if (raw != n->props.end()) out += raw->second;
  if (n->kids.empty()) {
    out += "</" + n->tag + ">" + (pretty ? "\n" : "");
    return;
  }
  if (pretty) out += "\n";
  for (int k : n->kids) dump(k, out, depth + 1, pretty);
  out += pad + "</" + n->tag + ">" + (pretty ? "\n" : "");
}

/** fontSize → font-size (custom properties and names that are already CSS stay) */
std::string cssName(const std::string& name) {
  if (name.compare(0, 2, "--") == 0) return name;
  std::string out;
  for (char c : name) {
    if (c >= 'A' && c <= 'Z') {
      out += '-';
      out += static_cast<char>(c - 'A' + 'a');
    } else out += c;
  }
  return out;
}

}  // namespace

// --- the backend -----------------------------------------------------------------

namespace backend {

int create(const char* tag) { return make(tag); }
void retain(int h) {
  if (Node* n = node(h)) n->refs++;
}
void release(int h) {
  if (Node* n = node(h)) n->refs--;
}
int special(int which) {
  Page& p = page();
  return which == 0 ? p.win : which == 1 ? p.doc : which == 2 ? p.body : p.html;
}
int byId(const char* id) {
  std::vector<int> out;
  collect(page().html, std::string("#") + id, out, true);
  return out.empty() ? 0 : out[0];
}
int query(int h, const char* selector) {
  std::vector<int> out;
  collect(h == page().doc ? page().html : h, selector, out, true);
  if (out.empty() && h == page().doc && matchesSel(page().html, selector)) return page().html;
  return out.empty() ? 0 : out[0];
}
void queryAll(int h, const char* selector, std::vector<int>& out) { collect(h == page().doc ? page().html : h, selector, out, false); }

void append(int parent, int child) {
  Node* par = node(parent);
  if (par) insertAt(parent, child, par->kids.size());
}
void appendText(int parent, const char* text) {
  int t = make("#text");
  node(t)->data = text;
  append(parent, t);
}
void prepend(int parent, int child) { insertAt(parent, child, 0); }
void insertBefore(int parent, int child, int ref) {
  Node* par = node(parent);
  if (!par) return;
  insertAt(parent, child, ref ? indexIn(parent, ref) : par->kids.size());
}
void after(int h, int other) {
  Node* n = node(h);
  if (n && n->parent) insertAt(n->parent, other, indexIn(n->parent, h) + 1);
}
void before(int h, int other) {
  Node* n = node(h);
  if (n && n->parent) insertAt(n->parent, other, indexIn(n->parent, h));
}
void remove(int h) { detach(h); }
void clear(int h) {
  Node* n = node(h);
  if (!n) return;
  std::vector<int> kids = n->kids;
  for (int k : kids) detach(k);
  n->props.erase("innerHTML");
}
void replaceWith(int h, int other) {
  Node* n = node(h);
  if (!n || !n->parent) return;
  int par = n->parent;
  size_t at = indexIn(par, h);
  detach(h);
  insertAt(par, other, at);
}

void setStr(int h, const char* prop, const char* value) {
  Node* n = node(h);
  if (!n) return;
  std::string p = prop;
  if (p == "className") n->classes = splitWords(value);
  else if (p == "textContent" || p == "innerText") {
    clear(h);
    if (*value) appendText(h, value);
  } else if (p == "innerHTML") {
    clear(h);
    if (*value) n->props[p] = value;
  } else n->props[p] = value;
}
void setNum(int h, const char* prop, double value) {
  char buf[40];
  if (value == static_cast<long long>(value)) std::snprintf(buf, sizeof buf, "%lld", static_cast<long long>(value));
  else std::snprintf(buf, sizeof buf, "%.17g", value);
  setStr(h, prop, buf);
}
void setBool(int h, const char* prop, bool value) {
  Node* n = node(h);
  if (!n) return;
  if (value) n->props[prop] = "1";
  else n->props.erase(prop);
}
std::string getStr(int h, const char* prop) {
  Node* n = node(h);
  if (!n) return "";
  std::string p = prop;
  if (p == "className") return joinWords(n->classes);
  if (p == "textContent" || p == "innerText") return textOf(h);
  if (p == "tagName") {
    std::string t = n->tag;
    for (char& c : t)
      if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return t;
  }
  auto it = n->props.find(p);
  return it == n->props.end() ? "" : it->second;
}
double getNum(int h, const char* prop) {
  Node* n = node(h);
  if (!n) return 0;
  std::string p = prop;
  if (p == "clientWidth" || p == "offsetWidth") return n->rect[2];
  if (p == "clientHeight" || p == "offsetHeight") return n->rect[3];
  if (p == "childElementCount") return childCount(h);
  auto it = n->props.find(p);
  if (it == n->props.end()) return 0;
  return std::strtod(it->second.c_str(), nullptr);
}
bool getBool(int h, const char* prop) {
  Node* n = node(h);
  if (!n) return false;
  auto it = n->props.find(prop);
  return it != n->props.end() && !it->second.empty() && it->second != "0";
}

void setAttr(int h, const char* name, const char* value) {
  if (Node* n = node(h)) n->attrs[name] = value;
}
bool hasAttr(int h, const char* name) {
  Node* n = node(h);
  return n && n->attrs.count(name) > 0;
}
std::string getAttr(int h, const char* name) {
  Node* n = node(h);
  if (!n) return "";
  auto it = n->attrs.find(name);
  return it == n->attrs.end() ? "" : it->second;
}
void removeAttr(int h, const char* name) {
  if (Node* n = node(h)) n->attrs.erase(name);
}
void setStyle(int h, const char* prop, const char* value) {
  Node* n = node(h);
  if (!n) return;
  std::string k = cssName(prop);
  if (*value) n->styles[k] = value;
  else n->styles.erase(k);
}
std::string getStyle(int h, const char* prop) {
  Node* n = node(h);
  if (!n) return "";
  auto it = n->styles.find(cssName(prop));
  return it == n->styles.end() ? "" : it->second;
}
std::string computedStyle(int h, const char* prop) {
  // (nothing is styled here: what was set on the element or on one above it)
  for (int cur = h; cur; cur = node(cur) ? node(cur)->parent : 0) {
    std::string v = getStyle(cur, prop);
    if (!v.empty()) return v;
  }
  return "";
}
bool classOp(int h, const char* name, int op) {
  Node* n = node(h);
  if (!n) return false;
  bool has = hasClass(*n, name);
  if (op == 3) return has;
  bool want = op == 1 ? true : op == 0 ? false : !has;
  if (want && !has) n->classes.push_back(name);
  if (!want && has) {
    for (size_t i = 0; i < n->classes.size(); i++)
      if (n->classes[i] == name) {
        n->classes.erase(n->classes.begin() + static_cast<long>(i));
        break;
      }
  }
  return want;
}

void listen(int h, const char* type, int listener, int flags) {
  if (Node* n = node(h)) n->listeners.push_back(Listener{type, listener, flags});
}
void unlisten(int h, const char* type, int listener, int flags) {
  Node* n = node(h);
  if (!n) return;
  for (size_t i = 0; i < n->listeners.size(); i++) {
    const Listener& l = n->listeners[i];
    if (l.id == listener && l.type == type && ((l.flags & 1) == (flags & 1))) {
      n->listeners.erase(n->listeners.begin() + static_cast<long>(i));
      return;
    }
  }
}
int setHandler(int h, const char* type, int listener) {
  Node* n = node(h);
  if (!n) return 0;
  int old = 0;
  auto it = n->handlers.find(type);
  if (it != n->handlers.end()) old = it->second;
  if (listener) n->handlers[type] = listener;
  else n->handlers.erase(type);
  return old;
}

void rect(int h, double out[4]) {
  Node* n = node(h);
  for (int i = 0; i < 4; i++) out[i] = n ? n->rect[i] : 0;
}
int parent(int h) {
  Node* n = node(h);
  return n ? n->parent : 0;
}
int child(int h, int index) {
  Node* n = node(h);
  if (!n) return 0;
  int i = 0;
  for (int k : n->kids) {
    if (isText(*node(k))) continue;
    if (i++ == index) return k;
  }
  return 0;
}
int childCount(int h) {
  Node* n = node(h);
  int c = 0;
  for (int k : n ? n->kids : std::vector<int>())
    if (!isText(*node(k))) c++;
  return c;
}
int sibling(int h, int dir) {
  Node* n = node(h);
  if (!n || !n->parent) return 0;
  Node* par = node(n->parent);
  long i = static_cast<long>(indexIn(n->parent, h)) + dir;
  for (; i >= 0 && i < static_cast<long>(par->kids.size()); i += dir)
    if (!isText(*node(par->kids[static_cast<size_t>(i)]))) return par->kids[static_cast<size_t>(i)];
  return 0;
}
bool contains(int h, int other) {
  for (int cur = other; cur; cur = node(cur) ? node(cur)->parent : 0)
    if (cur == h) return true;
  return false;
}
int closest(int h, const char* selector) {
  for (int cur = h; cur; cur = node(cur) ? node(cur)->parent : 0)
    if (matchesSel(cur, selector)) return cur;
  return 0;
}
bool matches(int h, const char* selector) { return matchesSel(h, selector); }
bool isConnected(int h) { return connected(h); }

void call(int h, const char* method) {
  std::string m = method;
  Page& p = page();
  if (m == "click") {
    Node* n = node(h);
    if (!n || getBool(h, "disabled")) return;
    // (a checkbox or a radio button changes before its handlers run)
    std::string type = getStr(h, "type");
    bool check = n->tag == "input" && (type == "checkbox" || type == "radio");
    if (check) setBool(h, "checked", type == "radio" ? true : !getBool(h, "checked"));
    Event e;
    e.type = "click";
    fire(h, e, true);
    if (check) {
      Event c;
      c.type = "change";
      fire(h, c, true);
    }
  } else if (m == "focus") {
    if (p.focused == h) return;
    int was = p.focused;
    p.focused = h;
    if (was) {
      Event b;
      b.type = "blur";
      fire(was, b, false);
    }
    Event e;
    e.type = "focus";
    fire(h, e, false);
  } else if (m == "blur") {
    if (p.focused != h) return;
    p.focused = 0;
    Event e;
    e.type = "blur";
    fire(h, e, false);
  } else if (m == "requestFullscreen") {
    p.fullscreen = h;
    Event e;
    e.type = "fullscreenchange";
    fire(p.doc, e, false);
  }
}
void pointerCapture(int h, int pointerId, bool on) {
  if (on) page().capture[pointerId] = h;
  else if (page().capture.count(pointerId) && page().capture[pointerId] == h) page().capture.erase(pointerId);
}
bool hasPointerCapture(int h, int pointerId) {
  auto it = page().capture.find(pointerId);
  return it != page().capture.end() && it->second == h;
}
int documentElementOf(const char* which) {
  std::string w = which;
  if (w == "fullscreenElement") return page().fullscreen;
  if (w == "activeElement") return page().focused ? page().focused : page().body;
  return 0;
}
void exitFullscreen() {
  Page& p = page();
  if (!p.fullscreen) return;
  p.fullscreen = 0;
  Event e;
  e.type = "fullscreenchange";
  fire(p.doc, e, false);
}

int handleLimit() { return static_cast<int>(page().nodes.size()); }
int refCount(int h) {
  Node* n = node(h);
  return n ? n->refs : 0;
}
void liveness(int limit, std::vector<uint8_t>& connectedOut, std::vector<int>& tree) {
  connectedOut.assign(static_cast<size_t>(limit), 0);
  tree.assign(static_cast<size_t>(limit), 0);
  Page& p = page();
  for (int h = 1; h < limit; h++) {
    if (!node(h)) continue;
    if (h == p.win || h == p.doc || connected(h)) connectedOut[static_cast<size_t>(h)] = 1;
    else tree[static_cast<size_t>(h)] = rootOf(h);
  }
}
void forgetListener(int listener) {
  // (the handler is gone; the element still lists it, which costs nothing: it is never asked for again)
  (void)listener;
}

}  // namespace backend

// --- what the tests use ------------------------------------------------------------

namespace native {

void reset() {
  Page& p = page();
  Node* b = node(p.body);
  std::vector<int> kids = b->kids;
  for (int k : kids) detach(k);
  b->classes.clear();
  b->props.clear();
  b->attrs.clear();
  b->styles.clear();
  for (int h : {p.win, p.doc, p.html, p.body}) {
    node(h)->listeners.clear();
    node(h)->handlers.clear();
  }
  node(p.html)->classes.clear();
  node(p.html)->attrs.clear();
  node(p.html)->props.clear();
  p.focused = p.fullscreen = 0;
  p.capture.clear();
}

std::string html(const El& e, bool pretty) {
  std::string out;
  dump(e.handle(), out, 0, pretty);
  return out;
}

std::string text(const El& e) { return textOf(e.handle()); }

void setRect(const El& e, Rect r) {
  Node* n = node(e.handle());
  if (!n) return;
  n->rect[0] = r.left;
  n->rect[1] = r.top;
  n->rect[2] = r.width;
  n->rect[3] = r.height;
}

bool dispatch(const El& target, Event& e, bool bubbles) {
  // (a pointer that is captured goes to the element that has it)
  int h = target.handle();
  if (e.type.compare(0, 7, "pointer") == 0 && e.type != "pointerdown") {
    auto it = page().capture.find(e.pointerId);
    if (it != page().capture.end()) h = it->second;
  }
  fire(h, e, bubbles);
  if ((e.type == "pointerup" || e.type == "pointercancel")) page().capture.erase(e.pointerId);
  return !e.defaultPrevented;
}

bool dispatch(const El& target, const std::string& type) {
  Event e;
  e.type = type;
  return dispatch(target, e);
}

void type(const El& field, const std::string& value) {
  field.value(value);
  dispatch(field, "input");
}

void choose(const El& select, const std::string& value) {
  select.value(value);
  dispatch(select, "change");
}

bool key(const std::string& code, const std::string& keyText, bool down) {
  Event e;
  e.type = down ? "keydown" : "keyup";
  e.code = code;
  e.key = keyText;
  int target = page().focused ? page().focused : page().body;
  fire(target, e, true);
  return !e.defaultPrevented;
}

El find(const El& within, const std::string& textPart, const std::string& selector) {
  std::vector<int> all;
  collect(within.handle(), selector, all, false);
  for (int h : all)
    if (textOf(h).find(textPart) != std::string::npos) {
      // (the innermost one: an element whose child also has the text is not it)
      bool inner = true;
      std::vector<int> below;
      collect(h, selector, below, false);
      for (int b : below)
        if (textOf(b).find(textPart) != std::string::npos) inner = false;
      if (inner) return El::of(h);
    }
  return El();
}

}  // namespace native

}  // namespace fs::dom
