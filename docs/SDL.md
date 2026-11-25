# The game in a window of its own (SDL3, experimental)

`fahrsim-sdl` is the native build of the game (the same code the tests run: `src/platform/native`)
with a real window: SDL3 makes the window, an OpenGL ES 3.0 context, the loudspeakers and the
hands.  It is off by default; CI, the browser build and the tests are not touched by it.  Android
is meant to follow through SDL's Android port, so `src/platform/sdl` uses only SDL's portable calls.

## Building

Needs SDL3 (3.2 or newer; `pkg-config --modversion sdl3`), EGL and GLES 3 (as the native tests do).

    FS_CMAKE_ARGS=-DFS_SDL3=ON tools/t.sh sdl_smoke_test     # configure once with the option, build and run the smoke test
    tools/t.sh fahrsim-sdl --start=free --vehicle=car         # build and run the game (the option stays in build/)

(by hand, elsewhere: `cmake -B build -G Ninja -DFS_SDL3=ON && cmake --build build -t fahrsim-sdl`)

## Running

The command line is the page's address: every `--name=value` becomes `?name=value`, so
`fahrsim-sdl --start=free --vehicle=car --lang=en` is the page opened with
`?start=free&vehicle=car&lang=en`.  Without `--start` a free drive starts (the menus are not painted
yet, see below).  The program's own options:

| option | |
|---|---|
| `--region=ID` | open that region (from map-assets over the network; settings: region) |
| `--map=DIR` | the site's own map (map.json, tiles …); otherwise `MAP_DIR`, `public/map`, or the original's `public/map` beside the repository |
| `--site=DIR` | the site's other files – signs/, parts/, lang/, data/ (default: the repository's `web/`) |
| `--offline` | nothing from the network |
| `--size=WxH` | the window's size in points (1280x720) |
| `--frames=N`, `--shot=PATH` | stop N pictures after the game is ready (writing the last frame as a PNG) |
| `--mute`, `--novsync` | no sound; do not wait for the screen's refresh |

Without a display, `SDL_VIDEODRIVER=offscreen` draws without a window (for the smoke test and
for pictures).

## How it fits

* `src/platform/sdl/sdl_host.cpp` – the window, the context (`plat::gl::native::adoptCurrentContext`:
  `gl_native.cpp` makes no EGL context of its own then, only the game's framebuffer), the loop
  (events → host queue `plat::native::pump()` → `plat::native::runFrames()`, which runs what
  `requestAnimationFrame` holds, the game's frame among it → sound → present), the hands.
* The renderer draws unchanged into its framebuffer (the "canvas's drawing buffer", whose size
  the game sets from innerWidth × devicePixelRatio and its pixel-ratio cap); the window shows it
  stretched to the window (`glBlitFramebuffer`), as a browser stretches the canvas.
* Over it, the interface: `src/platform/overlay.h` – `fs::sdlOverlayHook` hands back an RGBA
  picture (premultiplied, top row first, with a version that changes with its pixels); the window
  draws it over the whole frame.  Nothing sets it yet: the native 2D canvas and a HUD that paints
  are the next step (branch v21/canvas).
* Keys go to the page's body (and up to window) with the browser's `code` and `key`; the mouse
  as pointer events (`pointerType` 'mouse'), `wheel` (100 a notch), `click`, `dblclick`,
  `contextmenu` at the canvas; fingers as pointer events ('touch') and touchstart/move/end;
  gamepads as `navigator.getGamepads()` in the 'standard' layout (the triggers are buttons 6
  and 7); the window's size as innerWidth/innerHeight, `devicePixelRatio`
  (SDL_GetWindowPixelDensity) and the 'resize' event; focus lost as 'blur'.
* Sound: `audioNative::mix()` (audio_native.cpp) plays the Web Audio graphs the game builds –
  oscillators (also PeriodicWaves), buffer sources, gains, biquad filters, delays, stereo
  panners, a plain compressor, every setting's automation and nodes that move settings – into
  an SDL audio stream (48 kHz stereo, about 0.1 s ahead).

## What works, what does not (yet)

Works: the 3D world, driving with keys, mouse look, the wheel, gamepads, resizing, the sound
the game makes with Web Audio (engine, tyres, wind, clicks, signals …).

Tried on the development tablet (2026-11-25) only with `SDL_VIDEODRIVER=offscreen` (the agent's
shell reaches neither the Wayland nor the X display): the smoke test passes on the Mali-G52
(Panfrost) – 120 of 120 frames, state "drive", no GL error –, `--shot` writes the expected
picture of the car on the start place, and SDL's `disk` audio driver
(`SDL_AUDIO_DRIVER=disk SDL_AUDIO_DISK_OUTPUT_FILE=…`) records the idling engine.  Keys,
mouse, fingers and gamepads have not been tried by hand yet.

Not yet:
* **Nothing of the HTML interface is seen** – menus, HUD, signs' texts on the 2D canvas.  The
  game starts straight into a drive (`--start=…`); everything that needs a click on the page
  cannot be reached.  (The overlay seam is there; v21/canvas fills it.)
* Speech (spoken instructions) is silent: the native host has no speech synthesis.
* The network is the `curl` program, and a fetch waits for its answer (the picture stands while
  a region's tiles download).  On Android this must become SDL's / the platform's own download.
* No region is guessed from the IP address (the first start in the browser does that).
* Pointer lock, fullscreen, vibration, tilt steering, geolocation: not wired.
* The compressor is a simple one (no make-up gain); oscillators are not band-limited.
* localStorage is in memory: settings do not survive the program.
