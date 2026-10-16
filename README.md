# klayout-wasm

[KLayout](https://www.klayout.de)'s layout engine compiled to WebAssembly,
driven from Rust, for building layout viewers and editors that run in a
browser or a VS Code webview.

This is an **unofficial** build. It is not affiliated with or endorsed by the
KLayout project. KLayout's own source is used unmodified as a git submodule;
the few edits Emscripten needs are applied to copies at build time.

## Status

| Phase | Scope | State |
|---|---|---|
| 0 | `tl`, `gsi`, `db`, GDS2 + OASIS readers/writers; read a layout from Rust | **done** |
| 1 | `laybasic` + `layview` + `edt` + `ant`: render, mouse input, all edit modes | **done**: headless (Node) and in Chrome |
| 2 | Rust API: shapes, cells, transactions, undo, export, layer properties | planned |
| 3 | npm package, VS Code webview example, CI, release | planned |

Phase 0, run under Node, against native KLayout 0.30.12 on the same files:

```
ringo.gds:         dbu 0.001,  9 cells (1 top), 12 layers, 144 shapes   (native: identical)
drcFullTest_1.oas: dbu 0.001, 24 cells (1 top),  2 layers,  23 shapes   (native: identical)
garbage input rejected: Stream has unknown format: data
```

Phase 1, headless under Node (`examples/view.rs`): KLayout's Qt-free view
renders `ringo.gds` at 800x600, its own box tool draws a shape from synthetic
clicks, and undo/redo remove and restore it.

```
modes: ["select", "move", "ruler", "polygon", "box", "text", "path", "instance", "partial"]
drawing on layer 1/0
shapes: 144 before, 145 after drawing     -> native KLayout reads back: box (9515,2269;16288,5655) on 1/0
shapes after undo: 144
shapes after redo: 145
```

The view runs in synchronous mode, so there is no redraw thread and no need
for SharedArrayBuffer or cross-origin isolation.

Phase 1 in the browser (`web/`), checked by hand in Chrome at 1920x829: the
layout renders with KLayout's stipples and labels; box mode shows KLayout's
rubber band while dragging and commits the box (144 -> 145 shapes); the wheel
zooms about the cursor; undo/redo work; select mode highlights the box.

Not yet: Delete/Copy/Paste. In KLayout these are main-window menu commands,
not canvas keys, so the Delete key does nothing; Phase 2 exposes
`cut`/`copy`/`paste`/`select_all`/`cancel` as API calls.

## Layout

```
crates/klayout-sys/   build.rs compiles KLayout; cpp/ is the C ABI; src/ binds it safely
crates/klayout-web/   the browser module: one view behind `klw_*` exports
web/                  minimal demo page: canvas, input forwarding, mode buttons
vendor/klayout/       KLayout v0.30.12 (submodule, unmodified)
vendor/libexpat/      Expat R_2_9_0 (submodule) - KLayout's XML parser without Qt
```

## Building

Requirements: Rust (pinned in `rust-toolchain.toml`), Emscripten 6.0.12
(`emsdk`), Node 22+.

```sh
git submodule update --init --depth 1
source ~/emsdk/emsdk_env.sh
export CC_wasm32_unknown_emscripten=emcc CXX_wasm32_unknown_emscripten=em++ AR_wasm32_unknown_emscripten=emar
cargo build -p klayout-sys --example smoke --target wasm32-unknown-emscripten --release
node target/wasm32-unknown-emscripten/release/examples/smoke.js
```

The browser demo:

```sh
cargo build -p klayout-web --target wasm32-unknown-emscripten --release
cp target/wasm32-unknown-emscripten/release/klayout_web.{js,wasm} web/
cp vendor/klayout/samples/lvs/ringo.gds web/
python3 -m http.server -d web 8765   # then open http://127.0.0.1:8765
```

A clean build compiles ~330 KLayout translation units and takes about three
minutes on a 16-core machine.

## How the build works

- **Source lists come from KLayout's own `.pro` files.** `build.rs` evaluates
  them under a Qt-free configuration (`HAVE_QT=0`, no curl, git2, threads or
  scripting), so a KLayout upgrade is a submodule bump. The evaluator panics on
  any qmake construct it does not know, rather than silently dropping sources.
- **Patches are exact and counted.** Each edit in `PATCHES` must match exactly
  once or the build fails. Currently two, both platform branches KLayout lacks
  for Emscripten (`tlTimer.cc` memory size, `tlThreads.cc` timed join). Both
  are candidates for upstreaming.
- **One object, so registrations survive.** KLayout registers its stream
  formats from static objects nothing references by name. rustc's Emscripten
  linker ignores `+whole-archive`, so everything is partially linked into a
  single `klayout.o`, which the shim's references pull in whole.
- **Exceptions are native WebAssembly exceptions** (legacy encoding), matching
  Rust's `wasm32-unknown-emscripten` target. Every shim entry point catches all
  C++ exceptions; none cross into Rust.

## Known gaps

- The browser `.wasm` is 27 MB uncompressed, 6.4 MB gzipped. Nothing has been done about size
  yet: `-Oz`, `wasm-opt`, and dropping the scripting declarations (`gsiDecl*`)
  are the obvious first steps.
- Expat's entropy comes from `getentropy`; untested under every embedding.
- Frames are rendered at CSS pixels; no devicePixelRatio handling yet.
- The demo page runs KLayout on the main thread; a Web Worker comes with the
  npm package.

## License

GPL-3.0-or-later, as required by KLayout (GPL-2.0-or-later source files,
GPL-3.0 as distributed). Anything that embeds the WebAssembly module is a
combined work under the GPL. Expat is MIT.
