// Forwards canvas input to KLayout's view and paints the frames it renders.
// Everything about modes, snapping, selection and editing happens in KLayout;
// this page only translates browser events into protocol requests.

import createKlayout from "./klayout_web.js";
import { connect } from "./klayout.js";

const $ = (id) => document.getElementById(id);
const canvas = $("canvas");
const ctx = canvas.getContext("2d");
const status = (text) => ($("status").textContent = text);

const k = await connect(createKlayout);
let mode = "select";
let shapes = "";

// Every response reports what changed, whoever changed it.
function call(request, payload) {
  const response = k.call(request, payload);
  for (const event of response.events) {
    if (event.type === "layout_changed") {
      const { undo, redo } = event.history;
      $("undo").disabled = !undo;
      $("undo").title = undo ? `Undo ${undo}` : "";
      $("redo").disabled = !redo;
      $("redo").title = redo ? `Redo ${redo}` : "";
    } else if (event.type === "mode_changed") {
      mode = event.mode;
      for (const b of $("modes").children) b.setAttribute("aria-pressed", String(b.textContent === mode));
    } else if (event.type === "selection_changed") {
      const n = k.call({ type: "selection" }).reply.selection.length;
      shapes = n ? `${n} selected` : "";
    }
  }
  return response;
}

let pending = false;
function paint() {
  if (pending) return;
  pending = true;
  requestAnimationFrame(() => {
    pending = false;
    try {
      const { reply, payload } = call({ type: "frame" });
      const { width: w, height: h } = reply;
      if (canvas.width !== w || canvas.height !== h) { canvas.width = w; canvas.height = h; }
      ctx.putImageData(new ImageData(new Uint8ClampedArray(payload).slice(), w, h), 0, 0);
      status(`${w}×${h} · mode ${mode}${shapes ? ` · ${shapes}` : ""}`);
    } catch (err) {
      status(String(err));
    }
  });
}

function input(f) {
  return async (e) => {
    try { await f(e); } catch (err) { status(String(err)); }
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
const at = (e) => { const r = canvas.getBoundingClientRect(); return { x: e.clientX - r.left, y: e.clientY - r.top }; };
const mouse = (event, e, pressed) => call({ type: "mouse", event, ...at(e), buttons: buttons(e, pressed) });

function resize() {
  const r = canvas.getBoundingClientRect();
  call({ type: "resize", width: Math.max(1, Math.floor(r.width)), height: Math.max(1, Math.floor(r.height)) });
  paint();
}

function open(bytes) {
  call({ type: "open" }, bytes);
  applyLayer();
}

function applyLayer() {
  const [layer, datatype] = $("layer").value.split("/").map(Number);
  const { found } = call({ type: "set_current_layer", layer: { layer, datatype: datatype || 0 } }).reply;
  if (!found) status(`layer ${$("layer").value} is not in this layout`);
}

for (const name of k.hello.modes) {
  const b = document.createElement("button");
  b.textContent = name;
  b.setAttribute("aria-pressed", String(name === mode));
  b.onclick = input(() => {
    call({ type: "switch_mode", mode: name });
    canvas.focus();
  });
  $("modes").append(b);
}

// Keys KLayout's tools handle themselves.
const KEYS = { Escape: "escape", Tab: "tab", Enter: "enter", ArrowLeft: "left", ArrowUp: "up", ArrowRight: "right", ArrowDown: "down" };
// Desktop KLayout's Edit menu shortcuts.
const SHORTCUTS = { x: "cut", c: "copy", v: "paste_move", a: "select_all" };

function key(e) {
  const command = e.ctrlKey || e.metaKey ? SHORTCUTS[e.key.toLowerCase()] : undefined;
  if (command) return { type: "command", command };
  if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === "z") return { type: e.shiftKey ? "redo" : "undo" };
  if (e.key === "Delete" || e.key === "Backspace") return { type: "command", command: "delete" };
  if (e.key === "Escape") return { type: "command", command: "cancel" };
  const named = KEYS[e.key];
  if (named) return { type: "key", key: named, buttons: buttons(e, false) };
  if (e.key.length === 1 && !e.ctrlKey && !e.metaKey) return { type: "key", key: { char: e.key }, buttons: buttons(e, false) };
  return null;
}

canvas.addEventListener("pointermove", input((e) => mouse("move", e, false)));
canvas.addEventListener("pointerdown", input((e) => {
  canvas.setPointerCapture(e.pointerId);
  canvas.focus();
  mouse("press", e, true);
}));
canvas.addEventListener("pointerup", input((e) => mouse("release", e, true)));
canvas.addEventListener("dblclick", input((e) => mouse("double_click", e, true)));
canvas.addEventListener("pointerenter", input(() => call({ type: "mouse", event: "enter", x: 0, y: 0 })));
canvas.addEventListener("pointerleave", input(() => call({ type: "mouse", event: "leave", x: 0, y: 0 })));
canvas.addEventListener("contextmenu", (e) => e.preventDefault());
canvas.addEventListener("wheel", input((e) => {
  e.preventDefault();
  // Qt reports 120 per wheel notch; browsers report ~100 pixels.
  const horizontal = Math.abs(e.deltaX) > Math.abs(e.deltaY);
  const delta = Math.round(-(horizontal ? e.deltaX : e.deltaY) * 1.2);
  call({ type: "wheel", delta, horizontal, ...at(e), buttons: buttons(e, false) });
}), { passive: false });
canvas.addEventListener("keydown", input((e) => {
  const request = key(e);
  if (!request) return;
  e.preventDefault();
  call(request);
}));

$("file").onchange = input(async () => {
  const file = $("file").files[0];
  if (file) open(new Uint8Array(await file.arrayBuffer()));
});
$("layer").onchange = input(applyLayer);
$("undo").onclick = input(() => call({ type: "undo" }));
$("redo").onclick = input(() => call({ type: "redo" }));
$("fit").onclick = input(() => call({ type: "zoom_fit" }));
for (const b of document.querySelectorAll("[data-command]")) {
  b.onclick = input(() => {
    call({ type: "command", command: b.dataset.command });
    canvas.focus();
  });
}
new ResizeObserver(resize).observe(canvas);

resize();
open(new Uint8Array(await (await fetch("./ringo.gds")).arrayBuffer()));
paint();
