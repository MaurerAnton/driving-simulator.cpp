# How the port is built

Fahrsim Baunatal 2 is the C++ rewrite of [driving-simulator](https://github.com/MaurerAnton/driving-simulator)
(TypeScript, three.js). It compiles to WebAssembly for the browser and to a native
program for tests and tools. This file is the working guide: read it before changing anything.

## The source of truth

The original at **v1.7.7** (`/home/user/driving-simulator`, read-only) is the specification.
A C++ file is a translation of the TypeScript file of the same path and name:

    src/world/lanes.ts   →   src/world/lanes.h + src/world/lanes.cpp

Every translated file starts with `// Origin: src/world/lanes.ts @ v1.7.7`. Where an earlier
translation exists (made from 0.7.0 or 0.8.0), bring it up to v1.7.7: `git -C /home/user/driving-simulator
diff 3420ad0 v1.7.7 -- src/world/lanes.ts` shows what changed since 0.8.0 (`ff37c5d` is 0.7.0).

Translate faithfully: the same names, the same order, the same numbers, the same comments where
they explain a rule. Do not "improve" behaviour. Do not leave stubs, TODOs or placeholder
bodies: a function is either translated or not there. A subsystem that is not ported yet stays
out as a whole, and the places that would call it keep a nullable hook of the original's name
(`RoadEvents* roadEvents = nullptr;` … `if (roadEvents) …`).

## Layout

    src/core/       geometry on the ground plane, the coroutine type (task.h), small helpers
    src/platform/   what the host provides: files, storage, clock, DOM, 2D canvas, audio, input
      web/          … in the browser (Emscripten, library_fahrsim.js)
      native/       … on this machine (files from disk, a DOM kept in memory, OpenGL ES off screen)
    src/engine/     the renderer and the scene graph (the part of three.js the game uses)
    src/world/      road network, lanes, map loading, tile format        (origin: src/world)
    src/sim/        vehicle dynamics, traffic, signals, rules, autopilot (origin: src/sim)
    src/render/     what is drawn: tiles, sky, vehicles, signs …         (origin: src/render)
    src/ui/         menus and the display, built on platform/dom.h       (origin: src/ui)
    src/game/       the game itself                                      (origin: src/game)
    src/app/        the two entry points: main_web.cpp, main_native.cpp
    web/            the page: index.html, the original's style sheets, the language files
    tests/          <name>_test.cpp, one executable each (plain assert())
    tools/          scripts; tools/dev/*.cpp are native tools; tools/diff/ compares with the original
    old/            the first draft's files that are not yet on the new foundation (not compiled)

Each directory under `src/` is one static library (`fs_world`, `fs_sim` …). A test names the
libraries it needs in a comment, so it builds even while another layer is being changed:

    // LINK: world core platform

(no such line: the test links everything).

## TypeScript → C++

| TypeScript | C++ |
| --- | --- |
| `number` | `double`; `int` only for counts, indices and ids |
| `string` | `std::string` (UTF-8) |
| `T[]` | `std::vector<T>` |
| `Map<K, V>` / `Set<K>` | `std::unordered_map` / `std::unordered_set`; `std::map` / `std::set` where the order of iteration matters (JS iterates in insertion order: keep that order where the result depends on it, e.g. with a vector beside the map) |
| `Float32Array`, `Uint16Array` … | `std::vector<float>`, `std::vector<uint16_t>` … |
| `T \| null`, `T \| undefined` | `T*` (not owning) for objects with identity; `std::optional<T>` for values |
| objects with identity (`Edge`, `Node`, `Lane`, a car of the traffic) | owned by one container with stable addresses (`std::vector<std::unique_ptr<T>>` or `std::deque<T>`), pointed to with raw `T*` everywhere else |
| `interface` of plain data | `struct` with the same field names and default values |
| `class` | `class`/`struct` with the same member names; `private` stays private where practical |
| closures, callbacks | `std::function` (or a lambda parameter) |
| `async`/`await`, `Promise<T>` | `Task<T>` with `co_await` (src/core/task.h) |
| `Math.random()` | `fs::rnd()` (src/core/random.h): one stream, seedable for tests |
| `performance.now()`, `Date.now()` | `plat::nowMs()`, `plat::dateMs()` |
| `console.warn` | `plat::warn(...)` |
| `fetch`, `localStorage` | `plat::fetch…`, `plat::storage()` (src/platform/plat.h) |
| DOM (`document.createElement`, `el(...)`) | `dom::El` (src/platform/dom.h): the same calls by nearly the same names |
| `CanvasRenderingContext2D` | `Canvas` / `Ctx2D` (src/platform/canvas.h) |
| `THREE.*` | the same names in `namespace fs` (src/engine/engine.h); objects are held by `std::shared_ptr`, made with `make<Mesh>(…)` |

Numbers must come out the same as in the original: keep `double` arithmetic in the simulation,
keep the order of operations, use `std::floor`/`std::round` exactly where the original uses
`Math.floor`/`Math.round` (note: `Math.round(-2.5)` is `-2`: use `fs::jsRound`).
`%` on negative numbers, `>>>`, `|0` and integer overflow follow JavaScript: see src/core/js.h.

Everything is in `namespace fs`. Identifiers keep the original's spelling (camelCase functions and
fields, PascalCase types, UPPER_CASE constants), so that a name seen in the TypeScript can be
searched for in the C++.

## Building and testing

    tools/t.sh lanes_test        build and run one test (or a tool of tools/dev)
    tools/cc.sh src/world/x.cpp  only check that a file compiles (quick)
    tools/check.sh               build everything and run all tests

The machine this is developed on is a tablet with four small cores and 8 GB of memory, and
several of us compile at the same time: build one target at a time (`tools/t.sh` uses two
compiler processes), never start a second build while one is running, and prefer `tools/cc.sh`
while writing.

Tests are plain programs with `assert()` (live in every native build). Mirror the original's
test file where there is one (`tests/world/lanes.test.ts` → `tests/lanes_test.cpp`): the same
cases, the same numbers.

**Comparing with the original** is the strongest check there is: `tools/diff/<name>.ts` runs the
original's code under node (`node tools/diff/<name>.ts` inside `/home/user/driving-simulator`, or
with its path given) and prints numbers; `tools/dev/<name>_dump.cpp` prints the same numbers from
the C++; `tools/diff/compare.py a.txt b.txt` compares them with a tolerance. Use it for anything
numeric: lane geometry, routes, vehicle dynamics, tile decoding.
The current map lies in `/home/user/driving-simulator/public/map` (map.json, w/, tiles/, net.bin.gz).

## Working together

Several agents work on this at once, each in its own git worktree and on its own branch, each
owning a list of files. Do not edit files you do not own; if you need something from another
layer (a field, a function), say so in your report rather than changing it. Commit often, with
short plain messages (one line saying what changed; no essays). Do not push; the lead merges.
