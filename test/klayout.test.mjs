// The whole JavaScript API, headless, against KLayout's sample layouts.
// Every check is an assertion; the run stops at the first mismatch.
//
//   bun test/klayout.test.mjs [path/to/klayout.js]     (or node)

import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { resolve } from "node:path";
import { pathToFileURL } from "node:url";

const root = new URL("..", import.meta.url).pathname;
const modulePath = resolve(process.argv[2] ?? `${root}/build/klayout.js`);
const { default: createKlayout } = await import(pathToFileURL(modulePath).href);
const sample = (path) => new Uint8Array(readFileSync(`${root}/vendor/klayout/${path}`));
const ringo = sample("samples/lvs/ringo.gds");

const k = await createKlayout();
const types = (changes) => changes.map((c) => c.type);
const NONE = {};
const LEFT = { left: true };

// Reading files: counts match native KLayout 0.30.12 on the same files.
console.log("KLayout", k.klayoutVersion);
assert.deepEqual(k.inspect(ringo), { format: "GDS2", dbu: 0.001, cells: 9, topCells: 1, layers: 12, shapes: 144 });
const oasis = k.inspect(sample("testdata/drc/drcFullTest_1.oas"));
assert.deepEqual(oasis, { format: "OASIS", dbu: 0.001, cells: 24, topCells: 1, layers: 2, shapes: 23 });
assert.throws(() => k.inspect(new TextEncoder().encode("garbage")), /Stream has unknown format/);
console.log("inspect: ringo.gds and drcFullTest_1.oas match native KLayout; garbage rejected");

const view = new k.View(true);
view.resize(800, 600);
assert.deepEqual(types(view.changes()), [], "nothing has happened yet");
view.open(ringo);
assert.deepEqual(types(view.changes()), ["layoutChanged", "layerPanelChanged"]);
assert.deepEqual(view.modes(), ["select", "move", "ruler", "polygon", "box", "text", "path", "instance", "partial"]);

// Values are checked as they are read; errors are JavaScript Errors.
assert.throws(() => view.command("explode"), /command: unknown value "explode"/);
assert.throws(() => view.shapes("NOPE", { layer: 1, datatype: 0 }), /no cell named NOPE/);
assert.throws(() => view.setCurrentLayer({ layer: 1.5, datatype: 0 }), /layer: expected a 32-bit integer/);
assert.throws(() => view.edit("x", [{ op: "insertShape", cell: "RINGO", layer: { layer: 1, datatype: 0 } }]), /missing field "shape"/);
console.log("bad input is rejected with the field named");

// Reading the layout.
const cells = view.cells();
const layers = view.layers();
const top = cells.find((c) => c.top).name;
assert.equal(view.dbu(), 0.001);
assert.equal(cells.length, 9);
assert.equal(layers.length, 12);
let decoded = 0;
for (const cell of cells) for (const layer of layers) decoded += view.shapes(cell.name, layer).length;
assert.equal(decoded, 144);
const placed = view.instances(top);
console.log(`${top}: ${placed.length} instances, e.g.`, JSON.stringify(placed[0]));
assert.equal(placed.length, 11);

// Rendering, and KLayout's box tool driven by synthetic clicks.
const blank = view.frame();
assert.equal(blank.rgba.length, 800 * 600 * 4);
const before = Uint8Array.from(blank.rgba);
assert.equal(view.setCurrentLayer({ layer: 1, datatype: 0 }), true);
view.switchMode("box");
assert.deepEqual(view.changes(), [{ type: "modeChanged", mode: "box" }]);
const click = (x, y) => {
  view.mouse("move", x, y, NONE);
  view.mouse("press", x, y, LEFT);
  view.mouse("release", x, y, LEFT);
};
view.mouse("enter", 0, 0, NONE);
click(300, 250);
for (let step = 1; step <= 10; step++) view.mouse("move", 300 + 20 * step, 250 + 10 * step, NONE);
click(500, 350);
assert.deepEqual(view.changes(), [{ type: "layoutChanged", history: { undo: "Create box", redo: null } }]);
const boxes = view.shapes(top, { layer: 1, datatype: 0 }).filter((s) => s.kind === "box");
console.log("box drawn by KLayout's tool:", JSON.stringify(boxes));
assert.equal(boxes.length, 1);
const after = view.frame().rgba;
let changed = 0;
for (let i = 0; i < after.length; i += 4) if (after[i] !== before[i] || after[i + 1] !== before[i + 1] || after[i + 2] !== before[i + 2]) changed++;
console.log(`frame: ${changed} pixels changed by the new box`);
assert.ok(changed > 1000);
view.undo();
assert.equal(view.shapes(top, { layer: 1, datatype: 0 }).filter((s) => s.kind === "box").length, 0);
view.redo();
view.command("cancel");
view.changes();

// One edit with every shape kind, a new cell, a new layer and instances.
const cell = "API_TEST";
const layer = { layer: 200, datatype: 0 };
const shapes = [
  { kind: "box", left: 0, bottom: 0, right: 1000, top: 500 },
  {
    kind: "polygon",
    hull: [{ x: 0, y: 0 }, { x: 0, y: 3000 }, { x: 3000, y: 3000 }, { x: 3000, y: 0 }],
    // KLayout's normal form: holes counterclockwise.
    holes: [[{ x: 1000, y: 1000 }, { x: 2000, y: 1000 }, { x: 2000, y: 2000 }, { x: 1000, y: 2000 }]],
  },
  { kind: "path", points: [{ x: 0, y: 5000 }, { x: 4000, y: 5000 }, { x: 4000, y: 9000 }], width: 200, beginExtension: 100, endExtension: 0, round: false },
  { kind: "text", string: "VDD", position: { x: 500, y: 250 }, orientation: "M90", size: 300, font: null, halign: "center", valign: "bottom" },
  { kind: "edge", from: { x: 0, y: 0 }, to: { x: 10, y: 10 } },
  { kind: "edgePair", first: { from: { x: 0, y: 0 }, to: { x: 0, y: 10 } }, second: { from: { x: 5, y: 10 }, to: { x: 5, y: 0 } } },
  { kind: "point", x: 7, y: 8 },
];
const rotated = { cell, transform: { displacement: { x: 50000, y: 0 }, angle: 30, mirror: true, magnification: 2 }, array: null };
const arrayed = {
  cell,
  transform: { displacement: { x: 0, y: -20000 }, angle: 0, mirror: false, magnification: 1 },
  array: { a: { x: 5000, y: 0 }, b: { x: 0, y: 12000 }, na: 4, nb: 2 },
};
view.edit("API test", [
  { op: "createCell", name: cell },
  ...shapes.map((shape) => ({ op: "insertShape", cell, layer, shape })),
  { op: "insertInstance", cell: top, instance: rotated },
  { op: "insertInstance", cell: top, instance: arrayed },
]);
assert.deepEqual(types(view.changes()), ["layoutChanged", "layerPanelChanged"]);
const back = view.shapes(cell, layer);
for (const shape of shapes) assert.ok(back.some((b) => isDeepStrictEqual(b, shape)), `did not round-trip: ${JSON.stringify(shape)}\nread back: ${JSON.stringify(back)}`);
assert.equal(back.length, shapes.length);
console.log(`all ${shapes.length} shape kinds round-trip exactly`);
const inst = view.instances(top);
const found = inst.find((i) => i.cell === cell && !i.array);
assert.ok(Math.abs(found.transform.angle - 30) < 1e-9 && found.transform.mirror && found.transform.magnification === 2);
assert.ok(inst.some((i) => isDeepStrictEqual(i, arrayed)));
assert.deepEqual(view.history(), { undo: "API test", redo: null });

// All or nothing.
const missing = { kind: "box", left: 1, bottom: 1, right: 2, top: 2 };
assert.throws(
  () => view.edit("bad", [
    { op: "insertShape", cell, layer, shape: missing },
    { op: "eraseShape", cell, layer, shape: missing },
    { op: "eraseShape", cell, layer, shape: missing },
  ]),
  /no such shape in API_TEST on 200\/0/,
);
assert.equal(view.shapes(cell, layer).length, shapes.length);
assert.deepEqual(view.history(), { undo: "API test", redo: null }, "a failed edit leaves no undo step");
assert.deepEqual(view.changes(), []);
console.log("a failing edit applies nothing and leaves no undo step");

// Erase by value, rename, undo, redo.
view.edit("erase", [{ op: "eraseShape", cell, layer, shape: shapes[0] }]);
assert.equal(view.shapes(cell, layer).length, shapes.length - 1);
view.edit("rename", [{ op: "renameCell", name: cell, newName: "RENAMED" }]);
assert.ok(view.cells().some((c) => c.name === "RENAMED"));
view.undo();
view.undo();
view.undo();
assert.ok(!view.cells().some((c) => c.name === cell), "undo removes the whole API test edit");
view.redo();
assert.equal(view.shapes(cell, layer).length, shapes.length);

// Export, read back.
const gds = view.export("gds2");
const summary = k.inspect(gds);
console.log(`export: ${gds.length} bytes GDS2, ${view.export("oasis").length} bytes OASIS`);
assert.equal(summary.cells, view.cells().length);
assert.equal(summary.shapes, 144 + 1 + shapes.length);
assert.equal(k.inspect(view.export("oasis")).cells, view.cells().length);

// Edit menu commands. Every hierarchy level is shown, so select all reaches
// into child cells, once per instance path, as on desktop.
const count = () => view.cells().reduce((n, c) => n + view.layers().reduce((m, l) => m + view.shapes(c.name, l).length, 0), 0);
const total = count();
view.changes();
view.command("selectAll");
assert.deepEqual(types(view.changes()), ["selectionChanged"]);
const selected = view.selection();
const selectedShapes = selected.filter((s) => s.kind === "shape").length;
console.log(`select all: ${selected.length} objects, e.g.`, JSON.stringify(selected[0]));
view.command("copy");
assert.equal(view.selection().length, 0, "copy clears the selection, as in KLayout");
view.command("paste");
assert.equal(count(), total + selectedShapes);
view.undo();
view.command("selectAll");
view.changes();
view.command("delete");
const deleted = view.changes();
assert.deepEqual(types(deleted), ["layoutChanged", "selectionChanged"]);
assert.equal(deleted[0].history.undo, "Delete");
// Every layer is visible, so select all + delete removes every shape that
// KLayout's editor can select: all but edges and edge pairs (DRC result
// types, unselectable on desktop too).
const left = view.cells().flatMap((c) => view.layers().flatMap((l) => view.shapes(c.name, l).map((s) => s.kind)));
assert.deepEqual(left.sort(), ["edge", "edgePair"]);
view.undo();
assert.equal(count(), total);
console.log(`copy + paste, select all + delete, undo: ok (${total} shapes)`);

// Layer panel and .lyp.
const panel = view.layerPanel();
console.log(`layer panel: ${panel.length} entries, e.g.`, JSON.stringify(panel[0]));
assert.ok(panel.some((e) => isDeepStrictEqual(e.layer, layer)));
const entry = panel.find((e) => e.layer);
view.changes();
view.setLayerVisible(entry.id, false);
assert.deepEqual(types(view.changes()), ["layoutChanged", "layerPanelChanged"]);
assert.equal(view.layerPanel().find((e) => e.id === entry.id).visible, false);
const lyp = view.saveLayerProperties();
assert.match(new TextDecoder().decode(lyp), /<layer-properties>/);
view.setLayerVisible(entry.id, true);
view.loadLayerProperties(lyp);
assert.equal(view.layerPanel().find((e) => e.source === entry.source).visible, false, ".lyp keeps the hidden layer");
console.log(`.lyp: ${lyp.length} bytes, round-trips visibility`);

// Reopening replaces the layout.
view.open(gds);
assert.deepEqual(types(view.changes()), ["layoutChanged", "layerPanelChanged"]);
assert.ok(view.cells().some((c) => c.name === cell));

view.delete();
console.log("ok");

function isDeepStrictEqual(a, b) {
  try {
    assert.deepStrictEqual(a, b);
    return true;
  } catch {
    return false;
  }
}
