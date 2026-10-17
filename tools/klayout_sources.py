#!/usr/bin/env python3
"""Lists the KLayout sources to compile, read from KLayout's own qmake .pro
files, and writes patched copies of the few files Emscripten needs edited.

    klayout_sources.py <klayout src dir> <output dir>

Writes <output dir>/klayout_sources.cmake, defining KL_MODULES and, per
module, KL_<module>_SOURCES and KL_<module>_DEFINE.

Reading the .pro files rather than copying their lists here makes a KLayout
upgrade a submodule bump. The evaluator understands exactly the constructs
those files use and fails on anything else: an upgrade that introduces a
new construct should fail loudly, not silently drop sources. The submodule
itself is never modified.
"""

import os
import re
import sys

# KLayout's build switches as qmake would see them. Everything that needs
# Qt, a network, git, threads or a scripting language is off.
CONFIG = {
    "HAVE_QT": "0",
    "HAVE_QTBINDINGS": "0",
    "HAVE_CURL": "0",
    "HAVE_GIT2": "0",
    "HAVE_EXPAT": "1",
    "HAVE_PNG": "0",
    "HAVE_PTHREADS": "0",
    "HAVE_64BIT_COORD": "0",
    "HAVE_RUBY": "0",
    "HAVE_PYTHON": "0",
}

# Scopes that are true for an Emscripten build. Any other bare scope
# (win32, macx, msvc, ...) is false.
TRUE_SCOPES = {"unix"}

# Each module: name, .pro file under src/, and the MAKE_* define its own
# sources are compiled with. The Qt-free view, as KLayout's `klayout.lay`
# Python module builds it, minus lym (macros: pulls in the Python and Ruby
# interpreters) and img.
MODULES = [
    ("tl", "tl/tl/tl.pro", "MAKE_TL_LIBRARY"),
    ("gsi", "gsi/gsi/gsi.pro", "MAKE_GSI_LIBRARY"),
    ("db", "db/db/db.pro", "MAKE_DB_LIBRARY"),
    ("gds2", "plugins/streamers/gds2/db_plugin/db_plugin.pro", "MAKE_DB_PLUGIN_LIBRARY"),
    ("oasis", "plugins/streamers/oasis/db_plugin/db_plugin.pro", "MAKE_DB_PLUGIN_LIBRARY"),
    ("rdb", "rdb/rdb/rdb.pro", "MAKE_RDB_LIBRARY"),
    ("laybasic", "laybasic/laybasic/laybasic.pro", "MAKE_LAYBASIC_LIBRARY"),
    ("layview", "layview/layview/layview.pro", "MAKE_LAYVIEW_LIBRARY"),
    ("edt", "edt/edt/edt.pro", "MAKE_EDT_LIBRARY"),
    ("ant", "ant/ant/ant.pro", "MAKE_ANT_LIBRARY"),
]

# Source edits KLayout needs to build for Emscripten: file under src/, exact
# text, replacement. Each must match exactly once, so an upgrade that moves
# the code fails the build instead of silently skipping the edit. Both are
# candidates for upstreaming.
PATCHES = [
    (
        # Thread::wait() with a timeout uses pthread_timedjoin_np, a glibc
        # extension Emscripten lacks; poll as on Windows and macOS.
        "tl/tl/tlThreads.cc",
        "#if defined(_WIN32) || defined(__APPLE__)",
        "#if defined(_WIN32) || defined(__APPLE__) || defined(__EMSCRIPTEN__)",
    ),
    (
        # memory_size() knows Windows, macOS and Linux and #errors otherwise.
        # In the browser the process's memory is the wasm linear memory.
        "tl/tl/tlTimer.cc",
        "#else\n#  error Unsupported platform\n#endif",
        "#elif defined(__EMSCRIPTEN__)\n\n  return size_t (__builtin_wasm_memory_size (0)) * 65536;\n\n"
        "#else\n#  error Unsupported platform\n#endif",
    ),
]


def fail(message):
    sys.exit(f"klayout_sources.py: {message}")


def unsupported(pro, line):
    fail(f"{pro}: unsupported qmake construct: `{line}`")


def eval_condition(cond):
    """equals(VAR, "value"), its negation, or a bare scope name; None for
    anything else."""
    negated = cond.startswith("!")
    body = cond[1:] if negated else cond
    m = re.fullmatch(r"equals\(\s*(\w+)\s*,\s*\"?([^\")]*)\"?\s*\)", body)
    if m:
        value = CONFIG.get(m.group(1), "") == m.group(2)
    elif re.fullmatch(r"\w+", body):
        value = body in TRUE_SCOPES
    else:
        return None
    return value != negated


def pro_sources(pro):
    """The SOURCES of a qmake .pro file under CONFIG, as absolute paths."""
    with open(pro) as f:
        text = f.read()

    # Logical lines: comments dropped, `\` continuations joined.
    lines, current = [], ""
    for raw in text.splitlines():
        line = raw.split("#", 1)[0].rstrip()
        if line.endswith("\\"):
            current += line[:-1] + " "
        else:
            lines.append(current + line)
            current = ""

    # Each frame: (enclosing scope active, this scope's own condition).
    frames = []
    active = lambda: not frames or (frames[-1][0] and frames[-1][1])
    sources = []

    for line in (l.strip() for l in lines):
        if not line:
            continue

        if line.startswith("}"):
            if not frames:
                unsupported(pro, line)
            outer, own = frames.pop()
            rest = line[1:].strip()
            if not rest:
                continue
            if rest.endswith("{") and rest[:-1].strip() == "else":
                frames.append((outer, not own))
                continue
            unsupported(pro, line)

        if line.endswith("{"):
            # Like qmake, don't evaluate conditions inside an inactive scope:
            # Qt-only blocks use constructs (greaterThan(QT_MAJOR_VERSION, 4))
            # that mean nothing here.
            outer = active()
            own = True
            if outer:
                own = eval_condition(line[:-1].strip())
                if own is None:
                    unsupported(pro, line)
            frames.append((outer, own))
            continue

        if line.startswith("include("):
            continue

        if "=" not in line:
            unsupported(pro, line)
        lhs, values = line.split("=", 1)
        lhs = lhs.rstrip()
        op = lhs[-1] if lhs[-1] in "+-" else "="
        var = lhs.rstrip("+-").strip()
        if var != "SOURCES" or not active():
            continue
        values = values.split()
        if op == "=":
            sources = values
        elif op == "+":
            sources += values
        else:
            sources = [s for s in sources if s not in values]

    if frames:
        fail(f"{pro}: unbalanced braces")
    if not sources:
        fail(f"{pro}: no SOURCES found")
    directory = os.path.dirname(pro)
    return [os.path.join(directory, s) for s in sources]


def patched(src, out, source, left):
    """source, or a patched copy of it under out/ if PATCHES has edits for
    it. KLayout's module directories are on the include path, so the copy's
    local #includes still resolve."""
    rel = os.path.relpath(source, src)
    edits = [(find, replace) for (file, find, replace) in PATCHES if file == rel]
    if not edits:
        return source
    with open(source) as f:
        text = f.read()
    for find, replace in edits:
        hits = text.count(find)
        if hits != 1:
            fail(f"patch for {rel} matched {hits} times, expected 1")
        text = text.replace(find, replace, 1)
    left.discard(rel)
    copy = os.path.join(out, "patched", rel)
    os.makedirs(os.path.dirname(copy), exist_ok=True)
    # Rewrite only on change, so the build doesn't recompile it every time.
    old = open(copy).read() if os.path.exists(copy) else None
    if old != text:
        with open(copy, "w") as f:
            f.write(text)
    return copy


def main():
    if len(sys.argv) != 3:
        fail("usage: klayout_sources.py <klayout src dir> <output dir>")
    src, out = (os.path.abspath(a) for a in sys.argv[1:])
    os.makedirs(out, exist_ok=True)
    left = {file for (file, _, _) in PATCHES}
    cmake = ["# Generated by tools/klayout_sources.py; do not edit.\n"]
    cmake.append("set(KL_MODULES " + " ".join(name for name, _, _ in MODULES) + ")\n")
    pro_files = []
    for name, pro, define in MODULES:
        pro = os.path.join(src, pro)
        pro_files.append(pro)
        sources = [patched(src, out, s, left) for s in pro_sources(pro)]
        cmake.append(f"set(KL_{name}_DEFINE {define})\n")
        cmake.append(f"set(KL_{name}_SOURCES\n" + "".join(f'  "{s}"\n' for s in sources) + ")\n")
    if left:
        fail(f"patches for files no module compiles: {sorted(left)}")
    cmake.append("set(KL_PRO_FILES\n" + "".join(f'  "{p}"\n' for p in pro_files) + ")\n")
    with open(os.path.join(out, "klayout_sources.cmake"), "w") as f:
        f.write("".join(cmake))


if __name__ == "__main__":
    main()
