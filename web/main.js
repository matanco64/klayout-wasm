// Forwards canvas input to KLayout's view and paints the frames it renders.
// Everything about modes, snapping, selection and editing happens in KLayout.

import createKlayout from "./klayout_web.js";

const $ = (id) => document.getElementById(id);
const canvas = $("canvas");
const ctx = canvas.getContext("2d");
const status = (text) => ($("status").textContent = text);

const k = await createKlayout();
const call = (name, ...args) => {
  const result = k[`_klw_${name}`](...args);
  if (result === -1) throw new Error(`${name}: ${k.UTF8ToString(k._klw_error())}`);
  return result;
};
const withString = (s, f) => {
  const p = k.stringToNewUTF8(s);
  try { return f(p); } finally { k._free(p); }
};

// lay::ShiftButton etc.
const SHIFT = 0x01, CONTROL = 0x02, ALT = 0x04, LEFT = 0x08, MIDDLE = 0x10, RIGHT = 0x20;
const modifiers = (e) => (e.shiftKey ? SHIFT : 0) | (e.ctrlKey || e.metaKey ? CONTROL : 0) | (e.altKey ? ALT : 0);
const held = (e) => (e.buttons & 1 ? LEFT : 0) | (e.buttons & 2 ? RIGHT : 0) | (e.buttons & 4 ? MIDDLE : 0);
const pressed = (e) => [LEFT, MIDDLE, RIGHT][e.button] ?? 0;
const at = (e) => { const r = canvas.getBoundingClientRect(); return [e.clientX - r.left, e.clientY - r.top]; };

let pending = false;
function paint() {
  if (pending) return;
  pending = true;
  requestAnimationFrame(() => {
    pending = false;
    const ptr = k._klw_frame();
    if (!ptr) return status(k.UTF8ToString(k._klw_error()));
    const w = k._klw_frame_width(), h = k._klw_frame_height();
    if (canvas.width !== w || canvas.height !== h) { canvas.width = w; canvas.height = h; }
    ctx.putImageData(new ImageData(new Uint8ClampedArray(k.HEAPU8.buffer, ptr, w * h * 4).slice(), w, h), 0, 0);
    status(`${w}×${h} · mode ${k.UTF8ToString(k._klw_mode())} · ${k._klw_shape_count()} shapes`);
  });
}

function input(f) {
  return (e) => {
    try { f(e); } catch (err) { status(String(err)); }
    paint();
  };
}

function resize() {
  const r = canvas.getBoundingClientRect();
  call("resize", Math.max(1, Math.floor(r.width)), Math.max(1, Math.floor(r.height)));
  paint();
}

function load(bytes) {
  const p = k._malloc(bytes.length);
  k.HEAPU8.set(bytes, p);
  try { call("load", p, bytes.length); } finally { k._free(p); }
  applyLayer();
  paint();
}

function applyLayer() {
  const [layer, datatype] = $("layer").value.split("/").map(Number);
  const found = call("set_current_layer", layer, datatype || 0);
  if (!found) status(`layer ${$("layer").value} is not in this layout`);
}

function buildModes() {
  const names = k.UTF8ToString(k._klw_modes()).split("\n").filter(Boolean);
  for (const name of names) {
    const b = document.createElement("button");
    b.textContent = name;
    b.onclick = input(() => {
      withString(name, (p) => call("switch_mode", p));
      for (const other of $("modes").children) other.setAttribute("aria-pressed", String(other === b));
      canvas.focus();
    });
    $("modes").append(b);
  }
}

// Qt key codes for the keys KLayout's tools react to.
const QT_KEYS = {
  Escape: 0x01000000, Tab: 0x01000001, Backspace: 0x01000003, Enter: 0x01000004,
  Delete: 0x01000007, ArrowLeft: 0x01000012, ArrowUp: 0x01000013, ArrowRight: 0x01000014, ArrowDown: 0x01000015,
};
const qtKey = (e) => QT_KEYS[e.key] ?? (e.key.length === 1 ? e.key.toUpperCase().charCodeAt(0) : 0);

canvas.addEventListener("pointermove", input((e) => call("mouse", 0, ...at(e), held(e) | modifiers(e))));
canvas.addEventListener("pointerdown", input((e) => {
  canvas.setPointerCapture(e.pointerId);
  canvas.focus();
  call("mouse", 1, ...at(e), pressed(e) | modifiers(e));
}));
canvas.addEventListener("pointerup", input((e) => call("mouse", 2, ...at(e), pressed(e) | modifiers(e))));
canvas.addEventListener("dblclick", input((e) => call("mouse", 3, ...at(e), LEFT | modifiers(e))));
canvas.addEventListener("pointerenter", input(() => call("mouse", 4, 0, 0, 0)));
canvas.addEventListener("pointerleave", input(() => call("mouse", 5, 0, 0, 0)));
canvas.addEventListener("contextmenu", (e) => e.preventDefault());
canvas.addEventListener("wheel", input((e) => {
  e.preventDefault();
  // Qt reports 120 per wheel notch; browsers report ~100 pixels.
  const horizontal = Math.abs(e.deltaX) > Math.abs(e.deltaY);
  const delta = Math.round(-(horizontal ? e.deltaX : e.deltaY) * 1.2);
  call("wheel", delta, horizontal ? 1 : 0, ...at(e), held(e) | modifiers(e));
}), { passive: false });
canvas.addEventListener("keydown", input((e) => {
  const key = qtKey(e);
  if (!key) return;
  e.preventDefault();
  call("key", key, modifiers(e));
}));

$("file").onchange = input(async () => {
  const file = $("file").files[0];
  if (file) load(new Uint8Array(await file.arrayBuffer()));
});
$("layer").onchange = input(applyLayer);
$("undo").onclick = input(() => call("undo"));
$("redo").onclick = input(() => call("redo"));
$("fit").onclick = input(() => call("zoom_fit"));
new ResizeObserver(resize).observe(canvas);

call("new", 1);
buildModes();
resize();
load(new Uint8Array(await (await fetch("./ringo.gds")).arrayBuffer()));
