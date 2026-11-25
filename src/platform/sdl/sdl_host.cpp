// SPDX-License-Identifier: AGPL-3.0-only
/** The game in an SDL3 window (sdl_host.h). */
#include "platform/sdl/sdl_host.h"

#include <SDL3/SDL.h>
#include <GLES3/gl3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <vector>

#include "platform/frame.h"
#include "platform/glhost.h"
#include "platform/native/audio_native.h"
#include "platform/native/dom_native.h"
#include "platform/native/gl_native.h"
#include "platform/native/maps_native.h"
#include "platform/native/native.h"
#include "platform/native/sensors_native.h"
#include "platform/overlay.h"
#include "platform/plat.h"

namespace fs::sdl {

namespace {

struct Pad {
  SDL_JoystickID id = 0;
  SDL_Gamepad* pad = nullptr;
  std::string name;
};

struct Host {
  SDL_Window* window = nullptr;
  SDL_GLContext gl = nullptr;
  SDL_AudioStream* audio = nullptr;
  double audioRate = 48000;
  dom::El canvas;
  bool quit = false;
  long callbacks = 0, pictures = 0;
  std::vector<Pad> pads;
  /** fingers down: SDL's finger → the pointer id the page sees, and the first of them (the primary) */
  std::map<SDL_FingerID, int> fingers;
  int nextFinger = 2;
  SDL_FingerID primaryFinger = 0;
  bool primaryDown = false;
  // the picture over the frame
  GLuint program = 0, vao = 0, texture = 0;
  uint64_t overlayVersion = 0;
  int overlayW = 0, overlayH = 0;
};

Host& host() {
  static Host* h = new Host();
  return *h;
}

/** the browser's KeyboardEvent.code of a key (where it is on the keyboard, whatever is printed on it) */
std::string codeOf(SDL_Scancode s) {
  if (s >= SDL_SCANCODE_A && s <= SDL_SCANCODE_Z) return std::string("Key") + static_cast<char>('A' + (s - SDL_SCANCODE_A));
  if (s >= SDL_SCANCODE_1 && s <= SDL_SCANCODE_9) return std::string("Digit") + static_cast<char>('1' + (s - SDL_SCANCODE_1));
  if (s == SDL_SCANCODE_0) return "Digit0";
  if (s >= SDL_SCANCODE_F1 && s <= SDL_SCANCODE_F12) return "F" + std::to_string(1 + (s - SDL_SCANCODE_F1));
  if (s >= SDL_SCANCODE_KP_1 && s <= SDL_SCANCODE_KP_9) return std::string("Numpad") + static_cast<char>('1' + (s - SDL_SCANCODE_KP_1));
  switch (s) {
    case SDL_SCANCODE_RETURN: return "Enter";
    case SDL_SCANCODE_ESCAPE: return "Escape";
    case SDL_SCANCODE_BACKSPACE: return "Backspace";
    case SDL_SCANCODE_TAB: return "Tab";
    case SDL_SCANCODE_SPACE: return "Space";
    case SDL_SCANCODE_MINUS: return "Minus";
    case SDL_SCANCODE_EQUALS: return "Equal";
    case SDL_SCANCODE_LEFTBRACKET: return "BracketLeft";
    case SDL_SCANCODE_RIGHTBRACKET: return "BracketRight";
    case SDL_SCANCODE_BACKSLASH: return "Backslash";
    case SDL_SCANCODE_NONUSBACKSLASH: return "IntlBackslash";
    case SDL_SCANCODE_SEMICOLON: return "Semicolon";
    case SDL_SCANCODE_APOSTROPHE: return "Quote";
    case SDL_SCANCODE_GRAVE: return "Backquote";
    case SDL_SCANCODE_COMMA: return "Comma";
    case SDL_SCANCODE_PERIOD: return "Period";
    case SDL_SCANCODE_SLASH: return "Slash";
    case SDL_SCANCODE_CAPSLOCK: return "CapsLock";
    case SDL_SCANCODE_PRINTSCREEN: return "PrintScreen";
    case SDL_SCANCODE_SCROLLLOCK: return "ScrollLock";
    case SDL_SCANCODE_PAUSE: return "Pause";
    case SDL_SCANCODE_INSERT: return "Insert";
    case SDL_SCANCODE_HOME: return "Home";
    case SDL_SCANCODE_PAGEUP: return "PageUp";
    case SDL_SCANCODE_DELETE: return "Delete";
    case SDL_SCANCODE_END: return "End";
    case SDL_SCANCODE_PAGEDOWN: return "PageDown";
    case SDL_SCANCODE_RIGHT: return "ArrowRight";
    case SDL_SCANCODE_LEFT: return "ArrowLeft";
    case SDL_SCANCODE_DOWN: return "ArrowDown";
    case SDL_SCANCODE_UP: return "ArrowUp";
    case SDL_SCANCODE_NUMLOCKCLEAR: return "NumLock";
    case SDL_SCANCODE_KP_DIVIDE: return "NumpadDivide";
    case SDL_SCANCODE_KP_MULTIPLY: return "NumpadMultiply";
    case SDL_SCANCODE_KP_MINUS: return "NumpadSubtract";
    case SDL_SCANCODE_KP_PLUS: return "NumpadAdd";
    case SDL_SCANCODE_KP_ENTER: return "NumpadEnter";
    case SDL_SCANCODE_KP_0: return "Numpad0";
    case SDL_SCANCODE_KP_PERIOD: return "NumpadDecimal";
    case SDL_SCANCODE_LCTRL: return "ControlLeft";
    case SDL_SCANCODE_LSHIFT: return "ShiftLeft";
    case SDL_SCANCODE_LALT: return "AltLeft";
    case SDL_SCANCODE_LGUI: return "MetaLeft";
    case SDL_SCANCODE_RCTRL: return "ControlRight";
    case SDL_SCANCODE_RSHIFT: return "ShiftRight";
    case SDL_SCANCODE_RALT: return "AltRight";
    case SDL_SCANCODE_RGUI: return "MetaRight";
    case SDL_SCANCODE_APPLICATION: return "ContextMenu";
    case SDL_SCANCODE_AC_BACK: return "BrowserBack";
    default: return "";
  }
}

/** the browser's KeyboardEvent.key: the character typed, or the key's name */
std::string keyOf(SDL_Scancode s, SDL_Keycode k, SDL_Keymod mod) {
  switch (s) {
    case SDL_SCANCODE_LSHIFT:
    case SDL_SCANCODE_RSHIFT: return "Shift";
    case SDL_SCANCODE_LCTRL:
    case SDL_SCANCODE_RCTRL: return "Control";
    case SDL_SCANCODE_LALT: return "Alt";
    case SDL_SCANCODE_RALT: return "AltGraph";
    case SDL_SCANCODE_LGUI:
    case SDL_SCANCODE_RGUI: return "Meta";
    case SDL_SCANCODE_SPACE: return " ";
    case SDL_SCANCODE_AC_BACK: return "GoBack";
    default: break;
  }
  const std::string code = codeOf(s);
  if (code.rfind("Arrow", 0) == 0 || code == "Enter" || code == "Escape" || code == "Backspace" || code == "Tab" || code == "Home" || code == "End" ||
      code == "PageUp" || code == "PageDown" || code == "Insert" || code == "Delete" || code == "CapsLock" || code == "NumLock" || code == "ContextMenu" ||
      (code.size() >= 2 && code[0] == 'F' && code[1] >= '0' && code[1] <= '9'))
    return code == "ContextMenu" ? "ContextMenu" : code;
  // (the character, with Shift as it is held – SDL's key for the layout in use)
  const SDL_Keycode typed = SDL_GetKeyFromScancode(s, mod, false);
  const SDL_Keycode use = typed ? typed : k;
  if (use >= 32 && use < 0x110000 && !(use & SDLK_SCANCODE_MASK)) {
    // UTF-8 of the code point
    std::string out;
    const uint32_t c = static_cast<uint32_t>(use);
    if (c < 0x80) out += static_cast<char>(c);
    else if (c < 0x800) out += static_cast<char>(0xC0 | (c >> 6)), out += static_cast<char>(0x80 | (c & 0x3F));
    else if (c < 0x10000) out += static_cast<char>(0xE0 | (c >> 12)), out += static_cast<char>(0x80 | ((c >> 6) & 0x3F)), out += static_cast<char>(0x80 | (c & 0x3F));
    else
      out += static_cast<char>(0xF0 | (c >> 18)), out += static_cast<char>(0x80 | ((c >> 12) & 0x3F)), out += static_cast<char>(0x80 | ((c >> 6) & 0x3F)),
          out += static_cast<char>(0x80 | (c & 0x3F));
    return out;
  }
  return "Unidentified";
}

void modifiers(dom::Event& e, SDL_Keymod mod) {
  e.shiftKey = (mod & SDL_KMOD_SHIFT) != 0;
  e.ctrlKey = (mod & SDL_KMOD_CTRL) != 0;
  e.altKey = (mod & SDL_KMOD_ALT) != 0;
  e.metaKey = (mod & SDL_KMOD_GUI) != 0;
}

/** the browser's MouseEvent.buttons of SDL's (left 1, right 2, middle 4) */
int buttonsOf(SDL_MouseButtonFlags b) {
  return ((b & SDL_BUTTON_LMASK) ? 1 : 0) | ((b & SDL_BUTTON_RMASK) ? 2 : 0) | ((b & SDL_BUTTON_MMASK) ? 4 : 0) | ((b & SDL_BUTTON_X1MASK) ? 8 : 0) |
         ((b & SDL_BUTTON_X2MASK) ? 16 : 0);
}
/** MouseEvent.button (left 0, middle 1, right 2) */
int buttonOf(uint8_t b) {
  switch (b) {
    case SDL_BUTTON_LEFT: return 0;
    case SDL_BUTTON_MIDDLE: return 1;
    case SDL_BUTTON_RIGHT: return 2;
    case SDL_BUTTON_X1: return 3;
    case SDL_BUTTON_X2: return 4;
    default: return 0;
  }
}

/** the window's size → innerWidth/innerHeight/devicePixelRatio, the canvas's box, 'resize' */
void sized(bool tell) {
  Host& h = host();
  int w = 0, ht = 0;
  SDL_GetWindowSize(h.window, &w, &ht);
  float density = SDL_GetWindowPixelDensity(h.window);
  if (!(density > 0)) density = 1;
  plat::native::setViewport(std::max(1, w), std::max(1, ht), density);
  if (h.canvas) {
    dom::native::setRect(h.canvas, dom::Rect{0, 0, static_cast<double>(w), static_cast<double>(ht)});
    if (tell) plat::native::notifyResize(h.canvas);
  }
  if (tell) dom::native::dispatch(dom::window(), "resize");
}

void pointer(const std::string& type, double x, double y, int id, const std::string& kind, bool primary, int button, int buttons, double mx = 0, double my = 0,
             SDL_Keymod mod = SDL_KMOD_NONE) {
  Host& h = host();
  if (!h.canvas) return;
  dom::Event e;
  e.type = type;
  e.clientX = e.offsetX = x;
  e.clientY = e.offsetY = y;
  e.movementX = mx;
  e.movementY = my;
  e.pointerId = id;
  e.pointerType = kind;
  e.isPrimary = primary;
  e.button = button;
  e.buttons = buttons;
  modifiers(e, mod);
  dom::native::dispatch(h.canvas, e);
}

std::vector<dom::TouchPoint> touchesNow() {
  Host& h = host();
  std::vector<dom::TouchPoint> out;
  for (const auto& [finger, id] : h.fingers) {
    (void)finger;
    dom::TouchPoint t;
    t.identifier = id;
    out.push_back(t);
  }
  return out;
}

void finger(const SDL_TouchFingerEvent& f, Uint32 type) {
  Host& h = host();
  const double x = f.x * plat::innerWidth(), y = f.y * plat::innerHeight();
  int id;
  if (type == SDL_EVENT_FINGER_DOWN) {
    id = h.nextFinger++;
    h.fingers[f.fingerID] = id;
    if (!h.primaryDown) h.primaryDown = true, h.primaryFinger = f.fingerID;
  } else {
    auto it = h.fingers.find(f.fingerID);
    if (it == h.fingers.end()) return;
    id = it->second;
  }
  const bool primary = h.primaryDown && h.primaryFinger == f.fingerID;
  const char* ptype = type == SDL_EVENT_FINGER_DOWN ? "pointerdown" : type == SDL_EVENT_FINGER_MOTION ? "pointermove" : type == SDL_EVENT_FINGER_UP ? "pointerup" : "pointercancel";
  pointer(ptype, x, y, id, "touch", primary, 0, type == SDL_EVENT_FINGER_UP || type == SDL_EVENT_FINGER_CANCELED ? 0 : 1);
  // the touch events beside them (where they are: each finger's last place)
  static std::map<int, dom::TouchPoint> where;
  where[id] = dom::TouchPoint{id, x, y};
  if (type == SDL_EVENT_FINGER_UP || type == SDL_EVENT_FINGER_CANCELED) {
    h.fingers.erase(f.fingerID);
    if (primary) h.primaryDown = false;
  }
  dom::Event t;
  t.type = type == SDL_EVENT_FINGER_DOWN ? "touchstart" : type == SDL_EVENT_FINGER_MOTION ? "touchmove" : type == SDL_EVENT_FINGER_UP ? "touchend" : "touchcancel";
  for (const dom::TouchPoint& p : touchesNow()) t.touches.push_back(where[p.identifier]);
  t.changedTouches.push_back(where[id]);
  if (type == SDL_EVENT_FINGER_UP || type == SDL_EVENT_FINGER_CANCELED) where.erase(id);
  if (h.canvas) dom::native::dispatch(h.canvas, t);
}

/** the pads → navigator.getGamepads() in the 'standard' layout */
void readPads() {
  Host& h = host();
  std::vector<plat::GamepadState> out;
  int slot = 0;
  for (const Pad& p : h.pads) {
    plat::GamepadState g = plat::native::gamepad(p.name, "standard", 4, 17);
    g.index = slot++;
    auto axis = [&](SDL_GamepadAxis a) { return std::clamp(SDL_GetGamepadAxis(p.pad, a) / 32767.0, -1.0, 1.0); };
    g.axes[0] = axis(SDL_GAMEPAD_AXIS_LEFTX);
    g.axes[1] = axis(SDL_GAMEPAD_AXIS_LEFTY);
    g.axes[2] = axis(SDL_GAMEPAD_AXIS_RIGHTX);
    g.axes[3] = axis(SDL_GAMEPAD_AXIS_RIGHTY);
    const SDL_GamepadButton order[17] = {SDL_GAMEPAD_BUTTON_SOUTH,      SDL_GAMEPAD_BUTTON_EAST,           SDL_GAMEPAD_BUTTON_WEST,       SDL_GAMEPAD_BUTTON_NORTH,
                                         SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, SDL_GAMEPAD_BUTTON_INVALID,   SDL_GAMEPAD_BUTTON_INVALID,
                                         SDL_GAMEPAD_BUTTON_BACK,       SDL_GAMEPAD_BUTTON_START,          SDL_GAMEPAD_BUTTON_LEFT_STICK, SDL_GAMEPAD_BUTTON_RIGHT_STICK,
                                         SDL_GAMEPAD_BUTTON_DPAD_UP,    SDL_GAMEPAD_BUTTON_DPAD_DOWN,      SDL_GAMEPAD_BUTTON_DPAD_LEFT,  SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
                                         SDL_GAMEPAD_BUTTON_GUIDE};
    for (int b = 0; b < 17; b++) {
      plat::GamepadButton& gb = g.buttons[static_cast<size_t>(b)];
      if (b == 6 || b == 7) {
        // (the triggers: how far they are pulled)
        gb.value = std::clamp(SDL_GetGamepadAxis(p.pad, b == 6 ? SDL_GAMEPAD_AXIS_LEFT_TRIGGER : SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) / 32767.0, 0.0, 1.0);
        gb.pressed = gb.value > 0.1;
      } else {
        gb.pressed = SDL_GetGamepadButton(p.pad, order[b]);
        gb.value = gb.pressed ? 1 : 0;
      }
    }
    out.push_back(std::move(g));
  }
  plat::native::setGamepads(std::move(out));
}

void padAdded(SDL_JoystickID id) {
  Host& h = host();
  for (const Pad& p : h.pads)
    if (p.id == id) return;
  SDL_Gamepad* g = SDL_OpenGamepad(id);
  if (!g) return;
  const char* name = SDL_GetGamepadName(g);
  h.pads.push_back(Pad{id, g, name ? name : "Gamepad"});
  dom::Event e;
  e.type = "gamepadconnected";
  e.gamepadIndex = static_cast<int>(h.pads.size()) - 1;
  dom::native::dispatch(dom::window(), e, false);
}

void padRemoved(SDL_JoystickID id) {
  Host& h = host();
  for (size_t i = 0; i < h.pads.size(); i++)
    if (h.pads[i].id == id) {
      SDL_CloseGamepad(h.pads[i].pad);
      h.pads.erase(h.pads.begin() + static_cast<long>(i));
      dom::Event e;
      e.type = "gamepaddisconnected";
      e.gamepadIndex = static_cast<int>(i);
      dom::native::dispatch(dom::window(), e, false);
      return;
    }
}

void event(const SDL_Event& ev) {
  Host& h = host();
  switch (ev.type) {
    case SDL_EVENT_QUIT:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED: h.quit = true; break;
    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED: sized(true); break;
    case SDL_EVENT_WINDOW_FOCUS_LOST: dom::native::dispatch(dom::window(), "blur"); break;
    case SDL_EVENT_WINDOW_FOCUS_GAINED: dom::native::dispatch(dom::window(), "focus"); break;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
      dom::Event e;
      e.type = ev.type == SDL_EVENT_KEY_DOWN ? "keydown" : "keyup";
      e.code = codeOf(ev.key.scancode);
      e.key = keyOf(ev.key.scancode, ev.key.key, ev.key.mod);
      e.repeat = ev.key.repeat;
      modifiers(e, ev.key.mod);
      dom::native::dispatch(dom::body(), e);
      break;
    }
    case SDL_EVENT_MOUSE_MOTION:
      if (ev.motion.which == SDL_TOUCH_MOUSEID) break;   // (a finger: its own events)
      pointer("pointermove", ev.motion.x, ev.motion.y, 1, "mouse", true, -1, buttonsOf(ev.motion.state), ev.motion.xrel, ev.motion.yrel, SDL_GetModState());
      break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
      if (ev.button.which == SDL_TOUCH_MOUSEID) break;
      const bool down = ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
      const int button = buttonOf(ev.button.button);
      pointer(down ? "pointerdown" : "pointerup", ev.button.x, ev.button.y, 1, "mouse", true, button, buttonsOf(SDL_GetMouseState(nullptr, nullptr)), 0, 0,
              SDL_GetModState());
      if (!down && button == 2) pointer("contextmenu", ev.button.x, ev.button.y, 1, "mouse", true, button, 0);
      if (!down && button == 0) pointer("click", ev.button.x, ev.button.y, 1, "mouse", true, button, 0);
      if (!down && button == 0 && ev.button.clicks == 2) pointer("dblclick", ev.button.x, ev.button.y, 1, "mouse", true, button, 0);
      break;
    }
    case SDL_EVENT_MOUSE_WHEEL: {
      if (ev.wheel.which == SDL_TOUCH_MOUSEID || !h.canvas) break;
      dom::Event e;
      e.type = "wheel";
      e.clientX = e.offsetX = ev.wheel.mouse_x;
      e.clientY = e.offsetY = ev.wheel.mouse_y;
      // (a notch is 100 pixels in a browser; SDL counts up as away from the user, the browser's deltaY as down the page)
      const double flip = ev.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1 : 1;
      e.deltaX = ev.wheel.x * 100 * flip;
      e.deltaY = -ev.wheel.y * 100 * flip;
      e.pointerType = "mouse";
      modifiers(e, SDL_GetModState());
      dom::native::dispatch(h.canvas, e);
      break;
    }
    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_MOTION:
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_CANCELED: finger(ev.tfinger, ev.type); break;
    case SDL_EVENT_GAMEPAD_ADDED: padAdded(ev.gdevice.which); break;
    case SDL_EVENT_GAMEPAD_REMOVED: padRemoved(ev.gdevice.which); break;
    default: break;
  }
}

/** the sound: what the device needs to hold about a tenth of a second, mixed from the graphs */
void feedAudio() {
  Host& h = host();
  if (!h.audio) return;
  const int queued = SDL_GetAudioStreamQueued(h.audio) / static_cast<int>(2 * sizeof(float));
  const int want = static_cast<int>(h.audioRate * 0.1) - queued;
  if (want <= 0) return;
  const int frames = (want + 127) / 128 * 128;
  static std::vector<float> buf;
  buf.resize(static_cast<size_t>(frames) * 2);
  audioNative::mix(buf.data(), frames, h.audioRate);
  SDL_PutAudioStreamData(h.audio, buf.data(), static_cast<int>(buf.size() * sizeof(float)));
}

// --- the picture: the game's framebuffer stretched over the window, the overlay over it ---

/** GL state the renderer keeps track of (engine/renderer: its own record of what is bound): put back as it was */
struct Saved {
  GLint drawFb = 0, readFb = 0, viewport[4] = {0, 0, 0, 0}, program = 0, vao = 0, arrayBuffer = 0, activeTexture = 0, texture = 0, sampler = 0;
  GLint blendSrcRgb = 0, blendDstRgb = 0, blendSrcA = 0, blendDstA = 0, blendEqRgb = 0, blendEqA = 0, unpackAlign = 4, unpackRow = 0, unpackBuffer = 0;
  GLboolean scissor = 0, depth = 0, cull = 0, blend = 0, stencil = 0, polyOffset = 0, alphaCoverage = 0, colorMask[4] = {1, 1, 1, 1}, depthMask = 1;

  void save() {
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFb);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFb);
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &arrayBuffer);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
    glGetIntegerv(GL_SAMPLER_BINDING, &sampler);
    glGetIntegerv(GL_BLEND_SRC_RGB, &blendSrcRgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &blendDstRgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &blendSrcA);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &blendDstA);
    glGetIntegerv(GL_BLEND_EQUATION_RGB, &blendEqRgb);
    glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &blendEqA);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpackAlign);
    glGetIntegerv(GL_UNPACK_ROW_LENGTH, &unpackRow);
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpackBuffer);
    scissor = glIsEnabled(GL_SCISSOR_TEST);
    depth = glIsEnabled(GL_DEPTH_TEST);
    cull = glIsEnabled(GL_CULL_FACE);
    blend = glIsEnabled(GL_BLEND);
    stencil = glIsEnabled(GL_STENCIL_TEST);
    polyOffset = glIsEnabled(GL_POLYGON_OFFSET_FILL);
    alphaCoverage = glIsEnabled(GL_SAMPLE_ALPHA_TO_COVERAGE);
    glGetBooleanv(GL_COLOR_WRITEMASK, colorMask);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
  }
  static void enable(GLenum what, GLboolean on) { on ? glEnable(what) : glDisable(what); }
  void restore() const {
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(drawFb));
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(readFb));
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    glUseProgram(static_cast<GLuint>(program));
    glBindVertexArray(static_cast<GLuint>(vao));
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(arrayBuffer));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texture));
    glBindSampler(0, static_cast<GLuint>(sampler));
    glActiveTexture(static_cast<GLenum>(activeTexture));
    glBlendFuncSeparate(static_cast<GLenum>(blendSrcRgb), static_cast<GLenum>(blendDstRgb), static_cast<GLenum>(blendSrcA), static_cast<GLenum>(blendDstA));
    glBlendEquationSeparate(static_cast<GLenum>(blendEqRgb), static_cast<GLenum>(blendEqA));
    glPixelStorei(GL_UNPACK_ALIGNMENT, unpackAlign);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, unpackRow);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, static_cast<GLuint>(unpackBuffer));
    enable(GL_SCISSOR_TEST, scissor);
    enable(GL_DEPTH_TEST, depth);
    enable(GL_CULL_FACE, cull);
    enable(GL_BLEND, blend);
    enable(GL_STENCIL_TEST, stencil);
    enable(GL_POLYGON_OFFSET_FILL, polyOffset);
    enable(GL_SAMPLE_ALPHA_TO_COVERAGE, alphaCoverage);
    glColorMask(colorMask[0], colorMask[1], colorMask[2], colorMask[3]);
    glDepthMask(depthMask);
  }
};

GLuint shader(GLenum kind, const char* src) {
  GLuint s = glCreateShader(kind);
  glShaderSource(s, 1, &src, nullptr);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[1024] = {0};
    glGetShaderInfoLog(s, sizeof log, nullptr, log);
    std::fprintf(stderr, "fahrsim-sdl: the overlay's shader: %s\n", log);
  }
  return s;
}

/** the overlay (platform/overlay.h) over the whole window, premultiplied */
void drawOverlay(int width, int height) {
  Host& h = host();
  const OverlayImage img = sdlOverlay();
  if (!img.rgba || img.width <= 0 || img.height <= 0) return;
  if (!h.program) {
    const char* vs =
        "#version 300 es\n"
        "out vec2 uv;\n"
        "void main() {\n"
        "  vec2 p = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));\n"
        "  uv = vec2(p.x, 1.0 - p.y);\n"   // (the picture's top row first)
        "  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
        "}\n";
    const char* fs =
        "#version 300 es\n"
        "precision mediump float;\n"
        "uniform sampler2D picture;\n"
        "in vec2 uv;\n"
        "out vec4 color;\n"
        "void main() { color = texture(picture, uv); }\n";
    h.program = glCreateProgram();
    GLuint v = shader(GL_VERTEX_SHADER, vs), f = shader(GL_FRAGMENT_SHADER, fs);
    glAttachShader(h.program, v);
    glAttachShader(h.program, f);
    glLinkProgram(h.program);
    glDeleteShader(v);
    glDeleteShader(f);
    glGenVertexArrays(1, &h.vao);
    glGenTextures(1, &h.texture);
    glBindTexture(GL_TEXTURE_2D, h.texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    h.overlayVersion = img.version + 1;
  }
  glBindTexture(GL_TEXTURE_2D, h.texture);
  glBindSampler(0, 0);
  if (img.version != h.overlayVersion || img.width != h.overlayW || img.height != h.overlayH) {
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    if (img.width != h.overlayW || img.height != h.overlayH) glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, img.width, img.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.rgba);
    else glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, img.width, img.height, GL_RGBA, GL_UNSIGNED_BYTE, img.rgba);
    h.overlayVersion = img.version;
    h.overlayW = img.width;
    h.overlayH = img.height;
  }
  glViewport(0, 0, width, height);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glDisable(GL_STENCIL_TEST);
  glDisable(GL_POLYGON_OFFSET_FILL);
  glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glEnable(GL_BLEND);
  glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
  glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  glUseProgram(h.program);
  glUniform1i(glGetUniformLocation(h.program, "picture"), 0);
  glBindVertexArray(h.vao);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void present() {
  Host& h = host();
  int pw = 0, ph = 0;
  SDL_GetWindowSizeInPixels(h.window, &pw, &ph);
  uint32_t fbo = 0;
  int bw = 0, bh = 0;
  plat::gl::native::drawingBuffer(fbo, bw, bh);
  Saved s;
  s.save();
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
  glDisable(GL_SCISSOR_TEST);
  if (fbo && bw > 0 && bh > 0) {
    // (the canvas stretched to the window, as a browser shows a canvas whose buffer is smaller than its box)
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    glBlitFramebuffer(0, 0, bw, bh, 0, 0, pw, ph, GL_COLOR_BUFFER_BIT, bw == pw && bh == ph ? GL_NEAREST : GL_LINEAR);
  } else {
    glClearColor(0.086f, 0.125f, 0.169f, 1);   // (the page's colour, #16202b)
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT);
  }
  drawOverlay(pw, ph);
  s.restore();
  SDL_GL_SwapWindow(h.window);
}

}  // namespace

bool open(const Options& options, std::string* why) {
  Host& h = host();
  if (h.window) return true;
  // (OpenGL ES through EGL wherever SDL can: the same driver path as the off-screen host and as Android)
  SDL_SetHint(SDL_HINT_VIDEO_FORCE_EGL, "1");
  SDL_SetHint(SDL_HINT_APP_NAME, "Fahrsim Baunatal 2");
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
    if (why) *why = SDL_GetError();
    return false;
  }
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
  SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
  // (no alpha in the window: a compositor would see through it; no depth: the game draws into its own framebuffer)
  SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 0);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  h.window = SDL_CreateWindow(options.title.c_str(), options.width, options.height, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  if (!h.window) {
    if (why) *why = SDL_GetError();
    SDL_Quit();
    return false;
  }
  h.gl = SDL_GL_CreateContext(h.window);
  if (!h.gl || !SDL_GL_MakeCurrent(h.window, h.gl)) {
    if (why) *why = SDL_GetError();
    SDL_DestroyWindow(h.window);
    h.window = nullptr;
    SDL_Quit();
    return false;
  }
  SDL_GL_SetSwapInterval(options.vsync ? 1 : 0);
  plat::gl::native::adoptCurrentContext();
  {
    // (the game's framebuffer, as main_web makes the context on the page's canvas before the game)
    int pw = 0, ph = 0;
    SDL_GetWindowSizeInPixels(h.window, &pw, &ph);
    plat::gl::createContext("view", std::max(1, pw), std::max(1, ph));
  }
  // (the hands that are not there are no reason to fail)
  SDL_InitSubSystem(SDL_INIT_GAMEPAD);
  if (options.audio && SDL_InitSubSystem(SDL_INIT_AUDIO)) {
    SDL_AudioSpec spec{SDL_AUDIO_F32, 2, static_cast<int>(h.audioRate)};
    h.audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (h.audio) SDL_ResumeAudioStreamDevice(h.audio);
    else std::fprintf(stderr, "fahrsim-sdl: no sound (%s)\n", SDL_GetError());
  }
  sized(false);
  return true;
}

void attach(const dom::El& canvas) {
  Host& h = host();
  h.canvas = canvas;
  if (!canvas.parentElement()) dom::body().append(canvas);
  sized(true);
}

bool frame() {
  Host& h = host();
  if (!h.window) return false;
  SDL_Event ev;
  while (SDL_PollEvent(&ev)) event(ev);
  if (h.quit) return false;
  readPads();
  // what is due in the host's queue (answers of fetch, timers): for a while, not for ever – the picture waits
  const double until = plat::nowMs() + 12;
  while (plat::native::pump() > 0 && plat::nowMs() < until) {
  }
  const int ran = plat::native::runFrames();
  h.callbacks += ran;
  if (ran > 0) h.pictures++;
  feedAudio();
  present();
  return !h.quit;
}

long callbacksRun() { return host().callbacks; }
long picturesShown() { return host().pictures; }

bool screenshot(const std::string& path) {
  uint32_t fbo = 0;
  int w = 0, h = 0;
  plat::gl::native::drawingBuffer(fbo, w, h);
  if (!fbo) return false;
  return plat::gl::savePng(path, w, h);
}

std::string rendererName() { return plat::gl::rendererName(); }

std::string videoDriver() {
  const char* d = SDL_GetCurrentVideoDriver();
  return d ? d : "none";
}

void close() {
  Host& h = host();
  for (const Pad& p : h.pads) SDL_CloseGamepad(p.pad);
  h.pads.clear();
  plat::native::setGamepads({});
  if (h.audio) SDL_DestroyAudioStream(h.audio);
  h.audio = nullptr;
  if (h.gl) SDL_GL_DestroyContext(h.gl);
  h.gl = nullptr;
  if (h.window) SDL_DestroyWindow(h.window);
  h.window = nullptr;
  h.canvas = dom::El();
  SDL_Quit();
}

}  // namespace fs::sdl
