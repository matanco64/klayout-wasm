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
| 2 | Rust API and browser protocol: shapes, cells, instances, transactions, edit commands, selection, export, layer panel, change events | **done**: headless (Node, Bun) and in Chrome |
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

Phase 2, headless (`examples/api.rs`, every check an assertion):

```
dbu 0.001, 9 cells, 12 layers, top RINGO
decoded 144 shapes over every cell and layer (view counts 144)
all 4 shape kinds round-trip exactly
history after edit: History { undo: Some("API test"), redo: None }
failing edit rejected: no such shape in API_TEST on 200/0     (and nothing applied)
erase, rename, undo x3, redo: ok
Gds2: 10538 bytes, 10 cells, 148 shapes
select all: 826 objects, 826 shapes
copy + paste: 148 -> 974 shapes
select all + delete: 148 -> 0 shapes
.lyp: 6191 bytes, round-trips visibility
```

and through the browser module's protocol (`web/test.mjs`, run with Bun or
Node): KLayout's box tool reports its edit as a `layout_changed` event with
the undo label "Create box"; JSON edits, select all + delete, GDS2/OASIS
export, the layer panel and `.lyp` round-trip. In Chrome: draw, select,
Delete key, Cmd+Z, Cmd+C, Cmd+V (paste follows the mouse until a click).

## The Rust API

`klayout_sys::View` wraps KLayout's view; `klayout_sys::model` holds the
data types (optionally `serde`, feature `serde`). Coordinates are integer
database units.

| Area | Calls |
|---|---|
| Input and rendering | `resize`, `mouse`, `wheel`, `key`, `frame`, `zoom_fit`, `switch_mode` |
| Reading | `dbu`, `cells`, `layers`, `shapes(cell, layer)`, `instances(cell)` |
| Writing | `edit(description, &[Edit])`: insert/erase shapes and instances, create/delete/rename cells, as one undo step, all or nothing |
| Edit menu | `command(Command)`: delete, cut, copy, paste, paste-move, select all, unselect all, cancel |
| Selection | `selection()`: shapes and instances, with their cell and layer |
| History | `undo`, `redo`, `state().history` (KLayout's undo/redo labels) |
| Files | `load`, `export(Format::Gds2 / Oasis)` to bytes |
| Layer panel | `layer_panel`, `set_layer_visible`, `load_layer_properties`, `save_layer_properties` (.lyp) |
| Changes | `state()`: compare two snapshots to see whether the layout, selection, layer panel or mode changed |

Shapes and instances are identified by value: `Edit::EraseShape` removes
one shape equal to the one given. KLayout normalizes polygons (hull
clockwise, holes counterclockwise), so a polygon reads back in that form.

## The browser protocol

`crates/klayout-web` exposes one entry point, `klw_call(json, payload)`,
returning a JSON response and optionally a binary payload; `web/klayout.js`
is a small client. Requests and replies are typed in
`crates/klayout-web/src/protocol.rs` (version 1; `hello` checks it). Every
response carries `events`: `layout_changed` (with undo/redo labels),
`selection_changed`, `layer_panel_changed`, `mode_changed`, whatever caused
them, including KLayout's own tools reacting to mouse input.

```js
const k = await connect(createKlayout);              // hello
k.call({ type: "open" }, gdsBytes);
k.call({ type: "edit", description: "add pin", edits: [
  { op: "insert_shape", cell: "TOP", layer: { layer: 1, datatype: 0 },
    shape: { kind: "box", left: 0, bottom: 0, right: 100, top: 100 } } ] });
const { payload } = k.call({ type: "export", format: "oasis" });
```

## Layout

```
crates/klayout-sys/   build.rs compiles KLayout; cpp/ is the C ABI; src/ binds it safely
crates/klayout-web/   the browser module: one view behind one JSON message entry point
web/                  klayout.js (protocol client), test.mjs (protocol test), minimal demo page
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
cargo build -p klayout-sys --example smoke --example api --target wasm32-unknown-emscripten --release
node target/wasm32-unknown-emscripten/release/examples/smoke.js
node target/wasm32-unknown-emscripten/release/examples/api.js
cargo build -p klayout-web --target wasm32-unknown-emscripten --release
bun web/test.mjs        # or: node web/test.mjs
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

- The browser `.wasm` is 27 MB uncompressed, 6.5 MB gzipped. Nothing has been done about size
  yet: `-Oz`, `wasm-opt`, and dropping the scripting declarations (`gsiDecl*`)
  are the obvious first steps.
- Expat's entropy comes from `getentropy`; untested under every embedding.
- Frames are rendered at CSS pixels; no devicePixelRatio handling yet.
- The demo page runs KLayout on the main thread; a Web Worker comes with the
  npm package. The protocol is already plain messages, so it can move as is.
- Not carried through the API yet: user properties on shapes and instances
  (GDS properties), and KLayout's PCells and libraries.
- The `layout_changed` event comes from KLayout's undo journal, so a change
  made outside an undo step would go unnoticed. Every path through this API
  and KLayout's tools uses one.
- serde cannot reject stray fields on requests that have none (`zoom_fit`,
  `undo`, ...); requests with fields reject unknown ones.

## License

GPL-3.0-or-later, as required by KLayout (GPL-2.0-or-later source files,
GPL-3.0 as distributed). Anything that embeds the WebAssembly module is a
combined work under the GPL. Expat is MIT.
