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

if (names.length !== cells) throw new Error(`View.cells() has ${names.length}, inspect() says ${cells}`);
console.log(`package: klayout-wasm on KLayout ${k.klayoutVersion}: ${summary.format}, ${cells} cells, OASIS export ${oasis.length} bytes`);
