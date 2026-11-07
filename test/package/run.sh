#!/usr/bin/env bash
# Packs the module as npm would publish it, installs the tarball into a
# scratch project, type-checks test/package/consumer.ts against the shipped
# klayout.d.ts and runs it on ringo.gds.
#
#   test/package/run.sh            (after a build; see README "Building")
set -euo pipefail

root=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

cd "$root"
tarball=$(npm pack --silent --pack-destination "$work")
tar -tzf "$work/$tarball" | sort

cd "$work"
cat > package.json <<'JSON'
{ "name": "consumer", "private": true, "type": "module" }
JSON
npm install --silent --no-audit --no-fund "./$tarball" typescript@5 @types/node@22
cp "$root/test/package/consumer.ts" .
npx tsc --strict --module nodenext --target es2022 --types node consumer.ts
node consumer.js "$root/vendor/klayout/samples/lvs/ringo.gds"
