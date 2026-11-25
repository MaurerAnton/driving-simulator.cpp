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
| `--touch` | the pointer counts as coarse (a touch screen): the touch controls are shown from the start, and work with the mouse too |

Without a map on disk (and without `--offline`) the map is the one the 1.7.7 site serves
(`https://maureranton.github.io/driving-simulator/map/`), fetched as the browser would.
localStorage (settings, progress) is kept in SDL's folder for the app (`SDL_GetPrefPath`:
`~/.local/share/MaurerAnton/fahrsim2/localStorage.txt` on Linux).

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
  draws it over the whole frame.  `src/ui/sdlhud.*` paints the HUD into it with the native 2D
  canvas, and – while the driver uses fingers – the **touch controls**: the steering bar, the
  accelerator, the brake (the clutch with a manual gearbox), the indicators, the starter, ▲ ▼,
  D (first gear) and R, the handbrake and the camera; the tip line is a button too.  The window
  offers every finger and the left mouse button to `fs::sdlOverlayPointerHook` first: what lands
  on a control is the overlay's and feeds `Input` as the browser's controls do (`Input::touch`,
  `touched()`, `press(code)`); everything else goes to the page.
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
mouse, fingers and gamepads have not been tried by hand yet.  `--touch --shot=…` shows the
touch controls painted over the drive (2026-11-25); pressing them has not been tried by hand.

Not yet:
* **The HTML menus are not seen** – only the small HUD and the touch controls are painted
  (ui/sdlhud).  The game starts straight into a drive (`--start=…`); everything that needs a
  click on the page's menus cannot be reached.
* Speech (spoken instructions) is silent: the native host has no speech synthesis.
* The network is the `curl` program on the desktop (Java's HttpURLConnection on Android), asked on
  up to four threads of its own: the picture goes on while a region's tiles download.
* No region is guessed from the IP address (the first start in the browser does that).
* Pointer lock, fullscreen, vibration, tilt steering, geolocation: not wired.
* Only the touch controls' main part is painted: the page's "⋯" sheet (lights, wipers, horn,
  the map …) is not; those are keys on the desktop.
* The compressor is a simple one (no make-up gain); oscillators are not band-limited.

## Android (experimental)

An APK of the same program: `src/app/main_android.cpp` is the entry
(`ANativeActivity_onCreate` → `SDL_AndroidNativeActivity` → main_sdl.cpp's main), built as
`libmain.so` by `cmake/android.cmake` with the SDL3 fork **j4niwzis/SDL** (commit `91c08de8`,
NativeActivity profile: `SDL_ANDROID_NATIVE_ACTIVITY`, static, no Vulkan, no camera, the NDK's
`native_app_glue`) – the setup progressive-chat uses, without its self-made NDK: the game is
C++26 without modules, so the runner's NDK builds it.  No Java is compiled: the activity's classes
(`org.libsdl.nativeapp.NativeActivity`) are written as a `classes.dex` by the fork's own dex
writer (`build-scripts/native-activity/dex`, C++23 with `import std` – built in CI with clang 23
and CMake 4).  `tools/android/apk.sh` links `android/AndroidManifest.xml` and the icon
(`android/res`) against the SDK's `android.jar` with aapt2, puts in the dex, the stripped
library and the assets, lines the archive up and signs it with a key made for the build.

**Building** happens in GitHub Actions only (`.github/workflows/android.yml`; this machine has
no room for an NDK): every push to the branch `v21/android`, or by hand
(`gh workflow run android.yml --ref <branch>`).  The APK is the run's artifact
`fahrsim2-<commit>-arm64-v8a` (and `…-armeabi-v7a` for 32-bit phones):

    gh run download <run id> -n fahrsim2-<commit>-arm64-v8a
    adb install -r fahrsim2-<commit>-arm64-v8a.apk        # or open the file on the phone
    adb logcat -s fahrsim                                  # what it says (stdout and stderr go there)

It is signed with a fresh test key each build: an APK of another build is installed over it only
after the old one is removed (`adb uninstall io.github.maureranton.fahrsim2`).

**What it is:** arm64-v8a or armeabi-v7a, Android 8.0 (API 26) or newer, OpenGL ES 3.0, held sideways.  The
site's files (`web/`) and the fonts are the APK's assets, read through SDL (`SDL_LoadFile`: a
relative path is an asset there); localStorage is kept in the app's own folder; the map comes
from the 1.7.7 site over the network (Java's `HttpURLConnection` through JNI –
`src/platform/sdl/android_net.cpp`), so the first drive needs the internet and waits for the map.
The game starts straight into a free drive, with the touch controls; the window is laid out in
CSS pixels of the display's scale, as a phone's browser does.  stdout and stderr go to logcat
(tag `fahrsim`), and a crash says its signal there before Android's tombstone.

**Not tried on a device yet** (built in CI only, no phone or emulator here).  Not there: the
menus (as on the desktop), the region chooser (`--region` cannot be given to an APK), the
pause/menu button, speech, vibration, tilt steering, geolocation; a release key.
Going to the background stops the pictures and writes localStorage; whether the GL context
survives that on every phone is not known.
