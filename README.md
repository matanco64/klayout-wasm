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
| 1 | `laybasic` + `edt` + `ant`: render to a canvas, mouse input, all edit modes | next |
| 2 | Rust API: shapes, cells, transactions, undo, export, layer properties | planned |
| 3 | npm package, VS Code webview example, CI, release | planned |

Phase 0, run under Node, against native KLayout 0.30.12 on the same files:

```
ringo.gds:         dbu 0.001,  9 cells (1 top), 12 layers, 144 shapes   (native: identical)
drcFullTest_1.oas: dbu 0.001, 24 cells (1 top),  2 layers,  23 shapes   (native: identical)
garbage input rejected: Stream has unknown format: data
```

## Layout

```
crates/klayout-sys/   build.rs compiles KLayout; cpp/shim.cc is the C ABI; src/ binds it
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

- The smoke `.wasm` is 19.6 MB uncompressed. Nothing has been done about size
  yet: `-Oz`, `wasm-opt`, and dropping the scripting declarations (`gsiDecl*`)
  are the obvious first steps.
- Expat's entropy comes from `getentropy`; untested under every embedding.
- Only reading is exercised so far.

## License

GPL-3.0-or-later, as required by KLayout (GPL-2.0-or-later source files,
GPL-3.0 as distributed). Anything that embeds the WebAssembly module is a
combined work under the GPL. Expat is MIT.
