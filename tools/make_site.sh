#!/usr/bin/env bash
# Assembles the demo page into a self-contained static site, ready to copy to
# any file server:
#
#   tools/make_site.sh [build-dir] [out-dir]        (defaults: build, site)
#
# The output holds the page, the module, a sample layout and precompressed
# .br/.gz copies of everything large. klayout.wasm is ~28 MB raw and ~6 MB
# compressed, and compressing it on every request costs the server more than
# the transfer saves, so serve the precompressed files: in Caddy,
# `file_server { precompressed br gzip }`; in nginx, `gzip_static on`.
#
# The page links to the source. That link is not decoration: the module is a
# combined work with KLayout, and the GPL requires the corresponding source to
# be offered to anyone the binary is served to; web/index.html carries it.
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
build=${1:-$root/build}
out=${2:-$root/site}

for f in klayout.js klayout.wasm; do
  [ -f "$build/$f" ] || { echo "missing $build/$f -- build first (see README)" >&2; exit 1; }
done
sample=$root/vendor/klayout/samples/lvs/ringo.gds
[ -f "$sample" ] || { echo "missing $sample -- run: git submodule update --init --depth 1" >&2; exit 1; }

rm -rf "$out"
mkdir -p "$out"
cp "$build/klayout.js" "$build/klayout.wasm" "$sample" "$root/web/index.html" "$root/web/main.js" "$out/"
cp "$root/LICENSE" "$out/LICENSE.txt"

# Precompressed copies. Brotli if present; gzip always.
for f in klayout.wasm klayout.js main.js index.html ringo.gds; do
  gzip -9 -k -n "$out/$f"
  if command -v brotli >/dev/null; then brotli -q 11 -k "$out/$f"; fi
done

du -sh "$out"/klayout.wasm* | sed 's/^/  /'
echo "site: $out"
