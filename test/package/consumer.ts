// Type-checked against the packed package by test/package/run.sh: if the
// published klayout.d.ts stops matching this use, the check fails.
import createKlayout from "klayout-wasm";
import { readFileSync } from "node:fs";

const k = await createKlayout();
const gds = new Uint8Array(readFileSync(process.argv[2]));

const summary = k.inspect(gds);
const cells: number = summary.cells;

const view = new k.View(false);
view.open(gds);
const names: string[] = view.cells().map((c) => c.name);
const oasis: Uint8Array = view.export("oasis");
view.delete();

// The same layout as GDS2 and as OASIS: only what OASIS cannot hold differs.
const same: boolean = k.diff(gds, oasis, { ignoreTextDetails: true, ignoreDuplicates: true }).identical;
const changedCells: string[] = k.diff(gds, oasis, {}).cells.map((c) => c.cell);
const regions: number = k.xor(gds, oasis, {}).reduce((n, l) => n + l.polygons.length, 0);
if (!same || regions !== 0) throw new Error(`GDS2 and OASIS of one layout differ: ${changedCells.join(", ")}; ${regions} XOR regions`);

if (names.length !== cells) throw new Error(`View.cells() has ${names.length}, inspect() says ${cells}`);
console.log(`package: klayout-wasm on KLayout ${k.klayoutVersion}: ${summary.format}, ${cells} cells, OASIS export ${oasis.length} bytes`);
