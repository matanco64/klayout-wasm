// Two layouts side by side, with what differs between them marked.
// k.diff and k.xor do the comparing; this page lists the cells that differ,
// shows the chosen cell in two read-only views that pan and zoom together,
// and draws the differences as markers: removed on the left, added on the
// right. Differences are per cell, in that cell's own coordinates, so a
// change inside a child cell is found under that child, not its parents.
//
//   compare.html?a=<url>&b=<url>    loads the two files from URLs (CORS applies)

import createKlayout from "./klayout.js";

const $ = (id) => document.getElementById(id);
const status = (text) => ($("status").textContent = text);

const EXCEPTIONS = new Uint8Array([0, 97, 115, 109, 1, 0, 0, 0, 1, 4, 1, 96, 0, 0, 3, 2, 1, 0, 10, 7, 1, 5, 0, 6, 64, 11, 11]);
if (!WebAssembly.validate(EXCEPTIONS)) {
  $("stage").dataset.loading = "This browser does not support WebAssembly exceptions. Use a current Chrome, Edge, Firefox or Safari.";
  throw new Error("no WebAssembly exception support");
}

const k = await createKlayout();
$("version").textContent = ` ${k.klayoutVersion}`;

const REMOVED = "#ef4444";
const ADDED = "#22c55e";
const XOR = "#f59e0b";
// KLayout draws every marker on every frame; past this many the view crawls.
const MAX_MARKERS = 5000;

// One side of the comparison: a file, the view showing it, and its canvas.
function side(letter, title) {
  const canvas = $(`canvas${letter}`);
  return {
    letter, title, canvas, ctx: canvas.getContext("2d"), view: new k.View(false),
    name: "", bytes: null, cells: new Map(), shown: false,
  };
}
const A = side("A", "Before");
const B = side("B", "After");
const sides = [A, B];
const other = (s) => (s === A ? B : A);

let diff = null;
let cell = null;            // the name of the cell shown
let refit = false;          // a file was loaded: the views are no longer on the same box
const xorCache = new Map();

let pending = false;
function paint() {
  if (pending) return;
  pending = true;
  requestAnimationFrame(() => {
    pending = false;
    for (const s of sides) {
      if (!s.shown) {
        s.ctx.clearRect(0, 0, s.canvas.width, s.canvas.height);
        continue;
      }
      const { width: w, height: h, rgba } = s.view.frame();
      if (s.canvas.width !== w || s.canvas.height !== h) { s.canvas.width = w; s.canvas.height = h; }
      s.ctx.putImageData(new ImageData(new Uint8ClampedArray(rgba), w, h), 0, 0);
    }
  });
}

function guard(f) {
  return async (e) => {
    try { await f(e); } catch (err) { status(String(err.message ?? err)); }
    paint();
  };
}

// The other view follows this one. Both canvases are the same size, so the
// same box is the same picture.
function follow(s) {
  const o = other(s);
  if (s.shown && o.shown) o.view.zoomBox(s.view.viewport());
}

const buttons = (e, pressed) => ({
  left: pressed ? e.button === 0 : (e.buttons & 1) !== 0,
  middle: pressed ? e.button === 1 : (e.buttons & 4) !== 0,
  right: pressed ? e.button === 2 : (e.buttons & 2) !== 0,
  shift: e.shiftKey,
  control: e.ctrlKey || e.metaKey,
  alt: e.altKey,
});

for (const s of sides) {
  const { canvas, view } = s;
  const at = (e) => { const r = canvas.getBoundingClientRect(); return [e.clientX - r.left, e.clientY - r.top]; };
  // Most input (hovering) moves nothing; the other view follows only a viewport that changed.
  const forward = (f) => guard((e) => {
    if (!s.shown) return;
    const before = JSON.stringify(view.viewport());
    f(e);
    if (JSON.stringify(view.viewport()) !== before) follow(s);
  });
  canvas.addEventListener("pointermove", forward((e) => view.mouse("move", ...at(e), buttons(e, false))));
  canvas.addEventListener("pointerdown", forward((e) => {
    canvas.setPointerCapture(e.pointerId);
    view.mouse("press", ...at(e), buttons(e, true));
  }));
  canvas.addEventListener("pointerup", forward((e) => view.mouse("release", ...at(e), buttons(e, true))));
  canvas.addEventListener("pointerenter", forward(() => view.mouse("enter", 0, 0, {})));
  canvas.addEventListener("pointerleave", forward(() => view.mouse("leave", 0, 0, {})));
  canvas.addEventListener("contextmenu", (e) => e.preventDefault());
  canvas.addEventListener("wheel", forward((e) => {
    e.preventDefault();
    const horizontal = Math.abs(e.deltaX) > Math.abs(e.deltaY);
    view.wheel(Math.round(-(horizontal ? e.deltaX : e.deltaY) * 1.2), horizontal, ...at(e), buttons(e, false));
  }), { passive: false });
  new ResizeObserver(() => { size(s); paint(); }).observe(canvas);
}

function size({ canvas, view }) {
  const r = canvas.getBoundingClientRect();
  view.resize(Math.max(1, Math.floor(r.width)), Math.max(1, Math.floor(r.height)));
  // Frames come out in device pixels; the canvas keeps its CSS size.
  view.setDevicePixelRatio(devicePixelRatio);
}

// The outline of an instance: its cell's bounding box, placed. An array is
// outlined as a whole.
function instanceOutline(instance, cells) {
  const bbox = cells.get(instance.cell)?.bbox;
  if (!bbox) return null;
  const { displacement, angle = 0, mirror = false, magnification = 1 } = instance.transform ?? { displacement: { x: 0, y: 0 } };
  const rad = (angle * Math.PI) / 180;
  const [cos, sin] = [Math.cos(rad), Math.sin(rad)];
  const place = (x, y, dx = 0, dy = 0) => {
    if (mirror) y = -y;
    return {
      x: Math.round((x * cos - y * sin) * magnification + displacement.x + dx),
      y: Math.round((x * sin + y * cos) * magnification + displacement.y + dy),
    };
  };
  const corners = [[bbox.left, bbox.bottom], [bbox.left, bbox.top], [bbox.right, bbox.top], [bbox.right, bbox.bottom]];
  const array = instance.array;
  if (!array) return { kind: "polygon", hull: corners.map(([x, y]) => place(x, y)) };
  const points = [];
  for (const i of [0, array.na - 1]) {
    for (const j of [0, array.nb - 1]) {
      const [dx, dy] = [i * array.a.x + j * array.b.x, i * array.a.y + j * array.b.y];
      points.push(...corners.map(([x, y]) => place(x, y, dx, dy)));
    }
  }
  const xs = points.map((p) => p.x);
  const ys = points.map((p) => p.y);
  return { kind: "box", left: Math.min(...xs), bottom: Math.min(...ys), right: Math.max(...xs), top: Math.max(...ys) };
}

function objectMarkers(cellDiff, which, color) {
  const s = which === "A" ? A : B;
  const shapes = cellDiff.layers.flatMap((l) => l[`onlyIn${which}`]);
  const outlines = cellDiff[`instancesOnlyIn${which}`].map((i) => instanceOutline(i, s.cells)).filter(Boolean);
  return [
    ...shapes.map((shape) => ({ shape, color, lineWidth: 2, halo: true })),
    ...outlines.map((shape) => ({ shape, color, lineWidth: 2, ditherPattern: 5, halo: true })),
  ];
}

function xorMarkers(name) {
  if (!xorCache.has(name)) xorCache.set(name, k.xor(A.bytes, B.bytes, { cellA: name, cellB: name }));
  return xorCache.get(name).flatMap((l) => l.polygons).map((shape) => ({ shape, color: XOR, lineWidth: 1, ditherPattern: 3, halo: true }));
}

const count = (n, word) => `${n} ${word}${n === 1 ? "" : "s"}`;

// Shows `name` on each side that has it and marks what differs there.
function show(name) {
  cell = name;
  let fit = refit;
  refit = false;
  for (const s of sides) {
    s.shown = s.cells.has(name);
    s.view.clearMarkers();
    if (s.shown && s.view.currentCell() !== name) {
      s.view.showCell(name);
      fit = true;
    }
    $(`caption${s.letter}`).innerHTML = "";
    const label = document.createElement("b");
    label.textContent = s.title;
    $(`caption${s.letter}`).append(label, ` ${s.name} · ${s.shown ? name : `no cell ${name}`}`);
  }
  if (fit) fitBoth();
  mark();
}

// Fits both views to the union of the cell's two versions, so they show the
// same box and nothing either side has is off screen.
function fitBoth() {
  const boxes = sides.filter((s) => s.shown).map((s) => s.cells.get(cell).bbox).filter(Boolean);
  if (!boxes.length) return;
  const union = {
    left: Math.min(...boxes.map((b) => b.left)), bottom: Math.min(...boxes.map((b) => b.bottom)),
    right: Math.max(...boxes.map((b) => b.right)), top: Math.max(...boxes.map((b) => b.top)),
  };
  const margin = Math.max(union.right - union.left, union.top - union.bottom, 1) * 0.05;
  const box = { left: union.left - margin, bottom: union.bottom - margin, right: union.right + margin, top: union.top + margin };
  for (const s of sides) if (s.shown) s.view.zoomBox(box);
}

function mark() {
  if (!diff) return;
  const cellDiff = diff.cells.find((c) => c.cell === cell);
  const both = A.shown && B.shown;
  let markers = { A: [], B: [] };
  let text;
  if ($("xor").checked && both) {
    const m = xorMarkers(cell);
    markers = { A: m, B: m };
    text = m.length ? `${cell}: geometry differs in ${count(m.length, "region")} (this cell and everything below it)` : `${cell}: same geometry`;
  } else if (cellDiff) {
    markers = { A: objectMarkers(cellDiff, "A", REMOVED), B: objectMarkers(cellDiff, "B", ADDED) };
    text = `${cell}: ${markers.A.length} only before, ${markers.B.length} only after`;
  } else {
    text = both ? `${cell}: no differences in this cell itself` : `${cell} is only in the ${A.shown ? "first" : "second"} file`;
  }
  for (const s of sides) {
    const m = markers[s.letter];
    if (m.length > MAX_MARKERS) text += ` · showing ${MAX_MARKERS} of ${m.length} markers ${s.title.toLowerCase()}`;
    s.view.setMarkers(m.slice(0, MAX_MARKERS));
  }
  status(text);
}

function options() {
  return { ignoreTextDetails: $("ignoreTextDetails").checked, ignoreDuplicates: $("ignoreDuplicates").checked };
}

// Recomputes the comparison and the list of cells; keeps the cell shown if it still exists.
function compare() {
  if (!A.bytes || !B.bytes) {
    $("stage").dataset.loading = `Choose the ${A.bytes ? "After" : "Before"} file too.`;
    return;
  }
  if ("loading" in $("stage").dataset) {
    // The canvases have a size only once they are displayed, and the first fit needs it.
    delete $("stage").dataset.loading;
    sides.forEach(size);
  }
  xorCache.clear();
  diff = k.diff(A.bytes, B.bytes, options());

  const list = $("cells");
  list.replaceChildren();
  const add = (name, note) => list.add(new Option(`${name}  ${note}`, name));
  for (const c of diff.cells) {
    const [removed, added] = ["A", "B"].map((w) => c.layers.reduce((n, l) => n + l[`onlyIn${w}`].length, 0) + c[`instancesOnlyIn${w}`].length);
    add(c.cell, `−${removed} +${added}`);
  }
  for (const name of diff.cellsOnlyInA) add(name, "removed");
  for (const name of diff.cellsOnlyInB) add(name, "added");

  const parts = [];
  if (diff.identical) parts.push("The layouts are identical.");
  else parts.push(`${count(diff.cells.length, "cell")} changed, ${diff.cellsOnlyInB.length} added, ${diff.cellsOnlyInA.length} removed.`);
  if (diff.dbu) parts.push(`Database units differ: ${diff.dbu.a} and ${diff.dbu.b} µm.`);
  const layerNames = (layers) => layers.map((l) => `${l.layer}/${l.datatype}`).join(", ");
  if (diff.layersOnlyInB.length) parts.push(`New layers: ${layerNames(diff.layersOnlyInB)}.`);
  if (diff.layersOnlyInA.length) parts.push(`Layers gone: ${layerNames(diff.layersOnlyInA)}.`);
  $("summary").textContent = parts.join(" ");

  const known = (name) => name && (A.cells.has(name) || B.cells.has(name));
  const first = known(cell) ? cell : list.options[0]?.value ?? [...B.cells.values()].find((c) => c.top)?.name ?? [...B.cells.keys()][0];
  list.value = first;
  if (first) show(first);
}

function load(s, bytes, name) {
  s.view.open(bytes);
  s.bytes = bytes;
  s.name = name;
  s.cells = new Map(s.view.cells().map((c) => [c.name, c]));
  refit = true;
}

// A second file for the first visit: the sample with a few edits a designer
// might make, so the page has something to show.
function editedSample(bytes) {
  const v = new k.View(true);
  try {
    v.open(bytes);
    const top = v.cells().find((c) => c.top);
    const edits = [];
    // The last placement of the largest child: easy to see at full view.
    const area = new Map(v.cells().map((c) => [c.name, c.bbox ? (c.bbox.right - c.bbox.left) * (c.bbox.top - c.bbox.bottom) : 0]));
    const removed = v.instances(top.name).reduce((best, i) => (!best || area.get(i.cell) >= area.get(best.cell) ? i : best), null);
    if (removed) edits.push({ op: "eraseInstance", cell: top.name, instance: removed });
    for (const layer of v.layers()) {
      const box = v.shapes(top.name, layer).find((s) => s.kind === "box");
      if (!box) continue;
      const spec = { layer: layer.layer, datatype: layer.datatype };
      const height = box.top - box.bottom;
      edits.push({ op: "eraseShape", cell: top.name, layer: spec, shape: box });
      edits.push({ op: "insertShape", cell: top.name, layer: spec, shape: { ...box, top: box.top + height } });
      edits.push({ op: "insertShape", cell: top.name, layer: spec, shape: {
        kind: "box", left: top.bbox.left, bottom: top.bbox.top + height, right: top.bbox.left + 4 * height, top: top.bbox.top + 2 * height } });
      break;
    }
    v.edit("sample edits", edits);
    return v.export("gds2");
  } finally {
    v.delete();
  }
}

async function fetchBytes(url) {
  const response = await fetch(url);
  if (!response.ok) throw new Error(`${url}: ${response.status} ${response.statusText}`);
  return new Uint8Array(await response.arrayBuffer());
}

for (const s of sides) {
  $(`file${s.letter}`).onchange = guard(async () => {
    const file = $(`file${s.letter}`).files[0];
    if (!file) return;
    try {
      load(s, new Uint8Array(await file.arrayBuffer()), file.name);
    } finally {
      // A file KLayout rejects leaves the old layout shown, but without its markers.
      compare();
    }
  });
}
$("cells").onchange = guard(() => show($("cells").value));
$("xor").onchange = guard(mark);
$("ignoreTextDetails").onchange = guard(compare);
$("ignoreDuplicates").onchange = guard(compare);
$("fit").onclick = guard(() => { if (diff) fitBoth(); });

try {
  const query = new URLSearchParams(location.search);
  const base = (url) => new URL(url, location.href).pathname.split("/").pop() || url;
  if (query.has("a") !== query.has("b")) throw new Error("Give both files: compare.html?a=<url>&b=<url>");
  if (query.get("a") && query.get("b")) {
    const [a, b] = await Promise.all([fetchBytes(query.get("a")), fetchBytes(query.get("b"))]);
    load(A, a, base(query.get("a")));
    load(B, b, base(query.get("b")));
  } else {
    const sample = await fetchBytes("./ringo.gds");
    load(A, sample, "ringo.gds");
    load(B, editedSample(sample), "ringo.gds with sample edits");
  }
  compare();
} catch (err) {
  $("stage").dataset.loading = String(err.message ?? err);
  status(String(err.message ?? err));
}
paint();
