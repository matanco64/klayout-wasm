// Forwards canvas input to KLayout's view and paints the frames it renders.
// Everything about modes, snapping, selection and editing happens in KLayout;
// this page only translates browser events into View calls.

import createKlayout from "./klayout.js";

const $ = (id) => document.getElementById(id);
const canvas = $("canvas");
const ctx = canvas.getContext("2d");
const status = (text) => ($("status").textContent = text);

const k = await createKlayout();
const view = new k.View(true);
let selected = 0;

// Reflects what changed since the last look, whoever changed it.
function react() {
  for (const change of view.changes()) {
    if (change.type === "layoutChanged") {
      const { undo, redo } = change.history;
      $("undo").disabled = !undo;
      $("undo").title = undo ? `Undo ${undo}` : "";
      $("redo").disabled = !redo;
      $("redo").title = redo ? `Redo ${redo}` : "";
    } else if (change.type === "modeChanged") {
      for (const b of $("modes").children) b.setAttribute("aria-pressed", String(b.textContent === change.mode));
    } else if (change.type === "selectionChanged") {
      selected = view.selection().length;
    }
  }
}

let pending = false;
function paint() {
  if (pending) return;
  pending = true;
  requestAnimationFrame(() => {
    pending = false;
    try {
      const { width: w, height: h, rgba } = view.frame();
      if (canvas.width !== w || canvas.height !== h) { canvas.width = w; canvas.height = h; }
      ctx.putImageData(new ImageData(new Uint8ClampedArray(rgba), w, h), 0, 0);
      status(`${w}×${h} · mode ${view.mode()}${selected ? ` · ${selected} selected` : ""}`);
    } catch (err) {
      status(String(err));
    }
  });
}

function input(f) {
  return async (e) => {
    try { await f(e); } catch (err) { status(String(err)); }
    react();
    paint();
  };
}

const buttons = (e, pressed) => ({
  left: pressed ? e.button === 0 : (e.buttons & 1) !== 0,
  middle: pressed ? e.button === 1 : (e.buttons & 4) !== 0,
  right: pressed ? e.button === 2 : (e.buttons & 2) !== 0,
  shift: e.shiftKey,
  control: e.ctrlKey || e.metaKey,
  alt: e.altKey,
});
const at = (e) => { const r = canvas.getBoundingClientRect(); return [e.clientX - r.left, e.clientY - r.top]; };

function resize() {
  const r = canvas.getBoundingClientRect();
  view.resize(Math.max(1, Math.floor(r.width)), Math.max(1, Math.floor(r.height)));
  // Frames come out in device pixels; the canvas keeps its CSS size.
  view.setDevicePixelRatio(devicePixelRatio);
  paint();
}

function open(bytes) {
  view.open(bytes);
  applyLayer();
}

function applyLayer() {
  const [layer, datatype] = $("layer").value.split("/").map(Number);
  if (!view.setCurrentLayer({ layer, datatype: datatype || 0 })) status(`layer ${$("layer").value} is not in this layout`);
}

for (const name of view.modes()) {
  const b = document.createElement("button");
  b.textContent = name;
  b.setAttribute("aria-pressed", String(name === view.mode()));
  b.onclick = input(() => {
    view.switchMode(name);
    canvas.focus();
  });
  $("modes").append(b);
}

// Keys KLayout's tools handle themselves.
const KEYS = { Escape: "escape", Tab: "tab", Enter: "enter", ArrowLeft: "left", ArrowUp: "up", ArrowRight: "right", ArrowDown: "down" };
// Desktop KLayout's Edit menu shortcuts.
const SHORTCUTS = { x: "cut", c: "copy", v: "pasteMove", a: "selectAll" };

function key(e) {
  const modified = e.ctrlKey || e.metaKey;
  const command = modified ? SHORTCUTS[e.key.toLowerCase()] : undefined;
  if (command) return () => view.command(command);
  if (modified && e.key.toLowerCase() === "z") return () => (e.shiftKey ? view.redo() : view.undo());
  if (e.key === "Delete" || e.key === "Backspace") return () => view.command("delete");
  if (e.key === "Escape") return () => view.command("cancel");
  const named = KEYS[e.key];
  if (named) return () => view.key(named, buttons(e, false));
  if (e.key.length === 1 && !modified) return () => view.key({ char: e.key }, buttons(e, false));
  return null;
}

canvas.addEventListener("pointermove", input((e) => view.mouse("move", ...at(e), buttons(e, false))));
canvas.addEventListener("pointerdown", input((e) => {
  canvas.setPointerCapture(e.pointerId);
  canvas.focus();
  view.mouse("press", ...at(e), buttons(e, true));
}));
canvas.addEventListener("pointerup", input((e) => view.mouse("release", ...at(e), buttons(e, true))));
canvas.addEventListener("dblclick", input((e) => view.mouse("doubleClick", ...at(e), buttons(e, true))));
canvas.addEventListener("pointerenter", input(() => view.mouse("enter", 0, 0, {})));
canvas.addEventListener("pointerleave", input(() => view.mouse("leave", 0, 0, {})));
canvas.addEventListener("contextmenu", (e) => e.preventDefault());
canvas.addEventListener("wheel", input((e) => {
  e.preventDefault();
  // Qt reports 120 per wheel notch; browsers report ~100 pixels.
  const horizontal = Math.abs(e.deltaX) > Math.abs(e.deltaY);
  const delta = Math.round(-(horizontal ? e.deltaX : e.deltaY) * 1.2);
  view.wheel(delta, horizontal, ...at(e), buttons(e, false));
}), { passive: false });
canvas.addEventListener("keydown", input((e) => {
  const action = key(e);
  if (!action) return;
  e.preventDefault();
  action();
}));

$("file").onchange = input(async () => {
  const file = $("file").files[0];
  if (file) open(new Uint8Array(await file.arrayBuffer()));
});
$("layer").onchange = input(applyLayer);
$("undo").onclick = input(() => view.undo());
$("redo").onclick = input(() => view.redo());
$("fit").onclick = input(() => view.zoomFit());
for (const b of document.querySelectorAll("[data-command]")) {
  b.onclick = input(() => {
    view.command(b.dataset.command);
    canvas.focus();
  });
}
new ResizeObserver(resize).observe(canvas);

resize();
open(new Uint8Array(await (await fetch("./ringo.gds")).arrayBuffer()));
react();
paint();
