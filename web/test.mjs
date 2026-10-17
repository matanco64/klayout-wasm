// Headless protocol test: drives the browser module exactly as a page
// would, through web/klayout.js, and checks replies, payloads and events.
//
//   node web/test.mjs [path/to/klayout_web.js]

import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { resolve } from "node:path";
import { pathToFileURL } from "node:url";
import { connect } from "./klayout.js";

const root = new URL("..", import.meta.url).pathname;
const modulePath = resolve(process.argv[2] ?? `${root}/target/wasm32-unknown-emscripten/release/klayout_web.js`);
const { default: createKlayout } = await import(pathToFileURL(modulePath).href);
const ringo = readFileSync(`${root}/vendor/klayout/samples/lvs/ringo.gds`);

const types = (events) => events.map((e) => e.type);
const k = await connect(createKlayout);
console.log("hello:", k.hello);
assert.equal(k.hello.protocol, 1);
assert.ok(k.hello.modes.includes("box"));

// A wrong protocol version and malformed requests are rejected, typed.
assert.throws(() => k.call({ type: "hello", protocol: 99, editable: true }), /protocol 99/);
// (serde cannot reject stray fields on field-less requests like zoom_fit.)
assert.throws(() => k.call({ type: "resize", width: 1, height: 1, depth: 1 }), /bad request: unknown field `depth`/);
assert.throws(() => k.call({ type: "command", command: "explode" }), /bad request: unknown variant `explode`/);

const opened = k.call({ type: "open" }, ringo);
console.log("open:", opened.reply.cells.length, "cells,", opened.reply.layers.length, "layers; events", types(opened.events));
assert.deepEqual(types(opened.events), ["layout_changed", "layer_panel_changed"]);
const top = opened.reply.cells.find((c) => c.top).name;

k.call({ type: "resize", width: 640, height: 480 });
const frame = k.call({ type: "frame" });
assert.deepEqual(frame.reply, { type: "frame", width: 640, height: 480 });
assert.equal(frame.payload.length, 640 * 480 * 4);

// Draw a box with KLayout's own box tool; the edit shows up as an event.
assert.equal(k.call({ type: "set_current_layer", layer: { layer: 1, datatype: 0 } }).reply.found, true);
assert.deepEqual(types(k.call({ type: "switch_mode", mode: "box" }).events), ["mode_changed"]);
const click = (x, y) => {
  k.call({ type: "mouse", event: "move", x, y });
  k.call({ type: "mouse", event: "press", x, y, buttons: { left: true } });
  return k.call({ type: "mouse", event: "release", x, y, buttons: { left: true } });
};
click(200, 150);
k.call({ type: "mouse", event: "move", x: 300, y: 250 });
const drawn = click(300, 250);
console.log("box drawn by the tool:", drawn.events);
assert.deepEqual(drawn.events, [{ type: "layout_changed", history: { undo: "Create box", redo: null } }]);
const boxes = k.call({ type: "shapes", cell: top, layer: { layer: 1, datatype: 0 } }).reply.shapes.filter((s) => s.kind === "box");
console.log("boxes on 1/0 in", top, boxes);
assert.equal(boxes.length, 1);

// Typed edits from outside, in one undo step.
const edit = k.call({
  type: "edit",
  description: "from JS",
  edits: [
    { op: "create_cell", name: "JS" },
    { op: "insert_shape", cell: "JS", layer: { layer: 300, datatype: 1 }, shape: { kind: "path", points: [{ x: 0, y: 0 }, { x: 5000, y: 0 }], width: 400, begin_extension: 0, end_extension: 0, round: true } },
    { op: "insert_shape", cell: "JS", layer: { layer: 300, datatype: 1 }, shape: { kind: "text", string: "hi", position: { x: 0, y: 0 } } },
    { op: "insert_instance", cell: top, instance: { cell: "JS", transform: { displacement: { x: 0, y: 20000 } } } },
  ],
});
console.log("edit events:", types(edit.events));
assert.deepEqual(types(edit.events), ["layout_changed", "layer_panel_changed"]);
const js = k.call({ type: "shapes", cell: "JS", layer: { layer: 300, datatype: 1 } }).reply.shapes;
console.log("JS cell:", JSON.stringify(js));
assert.equal(js.length, 2);

// Selection and edit commands.
k.call({ type: "command", command: "cancel" });
assert.deepEqual(types(k.call({ type: "command", command: "select_all" }).events), ["selection_changed"]);
const selection = k.call({ type: "selection" }).reply.selection;
console.log("selected:", selection.length, "objects, e.g.", JSON.stringify(selection[0]));
const del = k.call({ type: "command", command: "delete" });
assert.deepEqual(types(del.events), ["layout_changed", "selection_changed"]);
assert.equal(del.events[0].history.undo, "Delete");
k.call({ type: "undo" });

// Export round trip.
const gds = k.call({ type: "export", format: "gds2" }).payload;
const oas = k.call({ type: "export", format: "oasis" }).payload;
console.log("export:", gds.length, "bytes GDS2,", oas.length, "bytes OASIS");
const reopened = k.call({ type: "open" }, oas);
assert.ok(reopened.reply.cells.some((c) => c.name === "JS"));

// Layer panel and .lyp.
const entries = k.call({ type: "layer_panel" }).reply.entries;
console.log("layer panel:", entries.length, "entries, e.g.", JSON.stringify(entries[0]));
const hide = k.call({ type: "set_layer_visible", id: entries[0].id, visible: false });
assert.deepEqual(types(hide.events), ["layout_changed", "layer_panel_changed"]);
const lyp = k.call({ type: "save_layer_properties" }).payload;
assert.match(new TextDecoder().decode(lyp), /<layer-properties>/);
k.call({ type: "load_layer_properties" }, lyp);
console.log(".lyp:", lyp.length, "bytes");
console.log("ok");
