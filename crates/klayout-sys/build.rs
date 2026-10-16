//! Builds the parts of KLayout this crate needs, straight from the vendored
//! source tree, with no Qt, qmake or CMake.
//!
//! The source list of each module is read from KLayout's own qmake `.pro`
//! file rather than copied here, so moving to a new KLayout release is a
//! submodule bump. The `.pro` evaluator understands exactly the constructs
//! those files use and panics on anything else: an upgrade that introduces a
//! new construct should fail loudly, not silently drop sources.

use std::collections::HashMap;
use std::env;
use std::fs;
use std::path::{Path, PathBuf};

/// KLayout's build switches as qmake would see them. Everything that needs
/// Qt, a network, git, threads or a scripting language is off.
const CONFIG: &[(&str, &str)] = &[
    ("HAVE_QT", "0"),
    ("HAVE_QTBINDINGS", "0"),
    ("HAVE_CURL", "0"),
    ("HAVE_GIT2", "0"),
    ("HAVE_EXPAT", "1"),
    ("HAVE_PNG", "0"),
    ("HAVE_PTHREADS", "0"),
    ("HAVE_64BIT_COORD", "0"),
    ("HAVE_RUBY", "0"),
    ("HAVE_PYTHON", "0"),
];

/// Scopes that are true for an Emscripten build. Any other bare scope
/// (`win32`, `macx`, `msvc`, ...) is false.
const TRUE_SCOPES: &[&str] = &["unix"];

/// Each KLayout module: library name, `.pro` file under `src/`, and the
/// `MAKE_*` define its own sources are compiled with.
const MODULES: &[(&str, &str, &str)] = &[
    ("kl_tl", "tl/tl/tl.pro", "MAKE_TL_LIBRARY"),
    ("kl_gsi", "gsi/gsi/gsi.pro", "MAKE_GSI_LIBRARY"),
    ("kl_db", "db/db/db.pro", "MAKE_DB_LIBRARY"),
    ("kl_gds2", "plugins/streamers/gds2/db_plugin/db_plugin.pro", "MAKE_DB_PLUGIN_LIBRARY"),
    ("kl_oasis", "plugins/streamers/oasis/db_plugin/db_plugin.pro", "MAKE_DB_PLUGIN_LIBRARY"),
    // The Qt-free view, as the `klayout.lay` Python module builds it, minus
    // lym (macros: pulls in the Python and Ruby interpreters) and img.
    ("kl_rdb", "rdb/rdb/rdb.pro", "MAKE_RDB_LIBRARY"),
    ("kl_laybasic", "laybasic/laybasic/laybasic.pro", "MAKE_LAYBASIC_LIBRARY"),
    ("kl_layview", "layview/layview/layview.pro", "MAKE_LAYVIEW_LIBRARY"),
    ("kl_edt", "edt/edt/edt.pro", "MAKE_EDT_LIBRARY"),
    ("kl_ant", "ant/ant/ant.pro", "MAKE_ANT_LIBRARY"),
];

/// Source edits KLayout needs to build for Emscripten: (file under `src/`,
/// exact text, replacement). Each must match exactly once, so an upgrade
/// that moves the code fails the build instead of silently skipping it.
/// The submodule itself is never modified; patched copies are compiled.
const PATCHES: &[(&str, &str, &str)] = &[
    (
        // Thread::wait() with a timeout uses pthread_timedjoin_np, a glibc
        // extension Emscripten lacks; poll as on Windows and macOS.
        "tl/tl/tlThreads.cc",
        "#if defined(_WIN32) || defined(__APPLE__)",
        "#if defined(_WIN32) || defined(__APPLE__) || defined(__EMSCRIPTEN__)",
    ),
    (
    // memory_size() knows Windows, macOS and Linux and #errors otherwise.
    // In the browser the process's memory is the wasm linear memory.
    "tl/tl/tlTimer.cc",
    "#else\n#  error Unsupported platform\n#endif",
    "#elif defined(__EMSCRIPTEN__)\n\n  return size_t (__builtin_wasm_memory_size (0)) * 65536;\n\n\
     #else\n#  error Unsupported platform\n#endif",
    ),
];

fn main() {
    let manifest = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
    let root = manifest.join("../..").canonicalize().unwrap();
    let src = root.join("vendor/klayout/src");
    let expat = root.join("vendor/libexpat/expat/lib");
    let out = PathBuf::from(env::var("OUT_DIR").unwrap());

    if !src.join("klayout.pri").exists() || !expat.join("xmlparse.c").exists() {
        panic!("vendored sources missing; run `git submodule update --init --depth 1`");
    }

    let target = env::var("TARGET").unwrap();
    if target != "wasm32-unknown-emscripten" {
        // Host builds are for tooling (cargo check, clippy, docs); KLayout
        // itself is only built for the browser target.
        println!("cargo:warning=klayout-sys only builds KLayout for wasm32-unknown-emscripten");
        return;
    }

    build_expat(&expat, &out);

    let config: HashMap<&str, &str> = CONFIG.iter().copied().collect();
    let includes = [
        src.join("tl/tl"),
        src.join("gsi/gsi"),
        src.join("db/db"),
        src.join("version"),
        src.join("plugins/common"),
        src.join("plugins/streamers/gds2/db_plugin"),
        src.join("plugins/streamers/oasis/db_plugin"),
        src.join("rdb/rdb"),
        src.join("laybasic/laybasic"),
        src.join("layview/layview"),
        src.join("edt/edt"),
        src.join("ant/ant"),
        expat.clone(),
    ];

    let mut patches_left: Vec<&str> = PATCHES.iter().map(|(file, ..)| *file).collect();
    let mut archives = Vec::new();
    for (lib, pro, make) in MODULES {
        let pro = src.join(pro);
        let sources: Vec<PathBuf> = pro_sources(&pro, &config)
            .into_iter()
            .map(|source| patched(&src, &out, source, &mut patches_left))
            .collect();
        let mut build = cxx_build(&includes);
        build.define(make, None).files(&sources);
        build.compile(lib);
        archives.push(out.join(format!("lib{lib}.a")));
        println!("cargo:rerun-if-changed={}", pro.display());
    }

    assert!(patches_left.is_empty(), "patches for files no module compiles: {patches_left:?}");

    let mut shim = cxx_build(&includes);
    shim.file(manifest.join("cpp/shim.cc")).file(manifest.join("cpp/view.cc")).include(manifest.join("cpp"));
    shim.compile("kl_shim");
    archives.push(out.join("libkl_shim.a"));

    merge(&cxx_build(&includes), &archives, &out);
    println!("cargo:rustc-link-search=native={}", out.display());
    println!("cargo:rustc-link-lib=static=klayout");
    println!("cargo:rerun-if-changed=cpp");
    println!("cargo:rerun-if-changed=build.rs");
}

/// `source`, or a patched copy of it under `OUT_DIR` if [`PATCHES`] has
/// edits for it. The copy keeps its own directory on the include path
/// through the module includes, so its local `#include`s still resolve.
fn patched(src: &Path, out: &Path, source: PathBuf, left: &mut Vec<&str>) -> PathBuf {
    let rel = source.strip_prefix(src).unwrap().to_str().unwrap().to_owned();
    let edits: Vec<_> = PATCHES.iter().filter(|(file, ..)| *file == rel).collect();
    if edits.is_empty() {
        return source;
    }
    let mut text = fs::read_to_string(&source).unwrap();
    for (_, find, replace) in edits {
        let hits = text.matches(find).count();
        assert_eq!(hits, 1, "patch for {rel} matched {hits} times, expected 1");
        text = text.replacen(find, replace, 1);
    }
    left.retain(|file| *file != rel);
    let copy = out.join("patched").join(&rel);
    fs::create_dir_all(copy.parent().unwrap()).unwrap();
    fs::write(&copy, text).unwrap();
    copy
}

/// A C++ build with the flags every KLayout translation unit shares.
fn cxx_build(includes: &[PathBuf]) -> cc::Build {
    let mut b = cc::Build::new();
    b.cpp(true)
        .std("c++17")
        .includes(includes)
        // Must match the Rust side; see .cargo/config.toml.
        .flag("-fwasm-exceptions")
        .flag("-sWASM_LEGACY_EXCEPTIONS=1")
        // zlib comes from Emscripten's port; the link side is in
        // .cargo/config.toml.
        .flag("-sUSE_ZLIB=1")
        .define("HAVE_EXPAT", None)
        // KLayout is far too large to iterate on at -O0, and a debug build
        // of it is not what anyone debugs in the browser.
        .opt_level(2)
        .debug(false)
        .warnings(false)
        .cargo_metadata(false);
    b
}

/// Partially links every archive, whole, into one relocatable object and
/// packages it as `libklayout.a`.
///
/// KLayout registers stream formats and gsi classes from static objects that
/// nothing references by name, so a normal archive link drops them. rustc's
/// Emscripten linker ignores `+whole-archive`, and a `rustc-link-arg` from a
/// -sys crate never reaches its dependents. One object sidesteps both: the
/// shim references `db::Layout`, which pulls in the object, and with it every
/// registration.
fn merge(cxx: &cc::Build, archives: &[PathBuf], out: &Path) {
    let object = out.join("klayout.o");
    let status = cxx
        .get_compiler()
        .to_command()
        .arg("-r")
        .arg("-Wl,--whole-archive")
        .args(archives)
        .arg("-Wl,--no-whole-archive")
        .arg("-o")
        .arg(&object)
        .status()
        .expect("running the partial link");
    assert!(status.success(), "partial link of KLayout failed");

    let library = out.join("libklayout.a");
    let _ = fs::remove_file(&library);
    let status = cxx
        .get_archiver()
        .arg("rcs")
        .arg(&library)
        .arg(&object)
        .status()
        .expect("running the archiver");
    assert!(status.success(), "archiving klayout.o failed");
}

fn build_expat(expat: &Path, out: &Path) {
    // The handful of facts expat's CMake would have detected.
    fs::write(
        out.join("expat_config.h"),
        "#define BYTEORDER 1234\n\
         #define HAVE_GETENTROPY 1\n\
         #define HAVE_MEMMOVE 1\n\
         #define HAVE_STDINT_H 1\n\
         #define HAVE_STDLIB_H 1\n\
         #define HAVE_STRING_H 1\n\
         #define HAVE_UNISTD_H 1\n\
         #define XML_DTD 1\n\
         #define XML_GE 1\n\
         #define XML_NS 1\n\
         #define XML_CONTEXT_BYTES 1024\n",
    )
    .unwrap();
    cc::Build::new()
        .include(out)
        .include(expat)
        .define("HAVE_EXPAT_CONFIG_H", None)
        .define("XML_STATIC", None)
        .files(
            ["xmlparse.c", "xmlrole.c", "xmltok.c", "xcs.c", "random_getentropy.c"]
                .iter()
                .map(|f| expat.join(f)),
        )
        .opt_level(2)
        .debug(false)
        .warnings(false)
        .cargo_metadata(false)
        .compile("kl_expat");
    println!("cargo:rustc-link-lib=static=kl_expat");
}

/// The `SOURCES` of a qmake `.pro` file under `config`, as absolute paths.
fn pro_sources(pro: &Path, config: &HashMap<&str, &str>) -> Vec<PathBuf> {
    let text = fs::read_to_string(pro).unwrap_or_else(|e| panic!("{}: {e}", pro.display()));
    let dir = pro.parent().unwrap();

    // Logical lines: comments dropped, `\` continuations joined.
    let mut lines = Vec::new();
    let mut current = String::new();
    for raw in text.lines() {
        let line = raw.split('#').next().unwrap().trim_end();
        match line.strip_suffix('\\') {
            Some(head) => {
                current.push_str(head);
                current.push(' ');
            }
            None => {
                current.push_str(line);
                lines.push(std::mem::take(&mut current));
            }
        }
    }

    // Each frame: (enclosing scope active, this scope's own condition).
    let mut frames: Vec<(bool, bool)> = Vec::new();
    let active = |frames: &[(bool, bool)]| frames.last().is_none_or(|&(outer, own)| outer && own);
    let mut sources: Vec<String> = Vec::new();

    for line in lines.iter().map(|l| l.trim()).filter(|l| !l.is_empty()) {

        if let Some(rest) = line.strip_prefix('}') {
            let (outer, own) = frames.pop().unwrap_or_else(|| unsupported(pro, line));
            let rest = rest.trim();
            if rest.is_empty() {
                continue;
            }
            if rest.trim_end_matches('{').trim() == "else" && rest.ends_with('{') {
                frames.push((outer, !own));
                continue;
            }
            unsupported(pro, line);
        }

        if let Some(cond) = line.strip_suffix('{') {
            // Like qmake, don't evaluate conditions inside an inactive scope:
            // Qt-only blocks use constructs (`greaterThan(QT_MAJOR_VERSION, 4)`)
            // that mean nothing here.
            let outer = active(&frames);
            let own = !outer
                || eval_condition(cond.trim(), config).unwrap_or_else(|| unsupported(pro, line));
            frames.push((outer, own));
            continue;
        }

        if line.starts_with("include(") {
            continue;
        }

        let Some((lhs, values)) = line.split_once('=') else { unsupported(pro, line) };
        let (var, op) = match lhs.trim_end().chars().last() {
            Some('+') => (lhs.trim_end().trim_end_matches('+').trim(), '+'),
            Some('-') => (lhs.trim_end().trim_end_matches('-').trim(), '-'),
            _ => (lhs.trim(), '='),
        };
        if var != "SOURCES" || !active(&frames) {
            continue;
        }
        let values = values.split_whitespace().map(str::to_owned);
        match op {
            '=' => sources = values.collect(),
            '+' => sources.extend(values),
            _ => {
                let removed: Vec<String> = values.collect();
                sources.retain(|s| !removed.contains(s));
            }
        }
    }

    assert!(frames.is_empty(), "{}: unbalanced braces", pro.display());
    assert!(!sources.is_empty(), "{}: no SOURCES found", pro.display());
    sources.iter().map(|s| dir.join(s)).collect()
}

fn unsupported(pro: &Path, line: &str) -> ! {
    panic!("{}: unsupported qmake construct: `{line}`", pro.display())
}

/// `equals(VAR, "value")`, its negation, or a bare scope name. `None` for
/// anything else.
fn eval_condition(cond: &str, config: &HashMap<&str, &str>) -> Option<bool> {
    let (negated, body) = match cond.strip_prefix('!') {
        Some(rest) => (true, rest),
        None => (false, cond),
    };
    let value = if let Some(args) = body.strip_prefix("equals(").and_then(|a| a.strip_suffix(')')) {
        let (var, want) = args.split_once(',')?;
        let want = want.trim().trim_matches('"');
        let have = config.get(var.trim()).copied().unwrap_or("");
        have == want
    } else if body.chars().all(|c| c.is_ascii_alphanumeric() || c == '_') {
        TRUE_SCOPES.contains(&body)
    } else {
        return None;
    };
    Some(value != negated)
}
