// The JavaScript API, and the TypeScript types the build emits for it
// (klayout.d.ts). Every value type is defined here once; convert.cc reads
// and writes exactly these shapes. Coordinates are integer database units.

#include "klayout_wasm.h"

#include <emscripten/bind.h>

namespace kl
{
// Types that only appear inside other types still need registering to be
// emitted.
EMSCRIPTEN_DECLARE_VAL_TYPE (Vector)
EMSCRIPTEN_DECLARE_VAL_TYPE (Edge)
EMSCRIPTEN_DECLARE_VAL_TYPE (Layer)
EMSCRIPTEN_DECLARE_VAL_TYPE (Cell)
EMSCRIPTEN_DECLARE_VAL_TYPE (Polygon)
EMSCRIPTEN_DECLARE_VAL_TYPE (Path)
EMSCRIPTEN_DECLARE_VAL_TYPE (Orientation)
EMSCRIPTEN_DECLARE_VAL_TYPE (Text)
EMSCRIPTEN_DECLARE_VAL_TYPE (Shape)
EMSCRIPTEN_DECLARE_VAL_TYPE (Transform)
EMSCRIPTEN_DECLARE_VAL_TYPE (InstanceArray)
EMSCRIPTEN_DECLARE_VAL_TYPE (Instance)
EMSCRIPTEN_DECLARE_VAL_TYPE (Edit)
EMSCRIPTEN_DECLARE_VAL_TYPE (Selected)
EMSCRIPTEN_DECLARE_VAL_TYPE (LayerEntry)
EMSCRIPTEN_DECLARE_VAL_TYPE (Change)
}

EMSCRIPTEN_BINDINGS (klayout)
{
  using namespace emscripten;
  using namespace kl;

  register_type<Point> ("Point", "{ x: number; y: number }");
  register_type<Vector> ("Vector", "{ x: number; y: number }");
  register_type<Rect> ("Rect", "{ left: number; bottom: number; right: number; top: number }");
  register_type<Edge> ("Edge", "{ from: Point; to: Point }");
  // A GDS layer/datatype pair: how shapes name their layer.
  register_type<LayerSpec> ("LayerSpec", "{ layer: number; datatype: number }");
  // name: the OASIS layer name, if any.
  register_type<Layer> ("Layer", "LayerSpec & { name: string | null }");
  // top: not placed in any other cell. bbox: including children; null if empty.
  register_type<Cell> ("Cell", "{ name: string; top: boolean; bbox: Rect | null }");
  // KLayout's normal form: hull clockwise, holes counterclockwise, each from
  // the lowest-leftmost point. Other forms are accepted and read back normalized.
  register_type<Polygon> ("Polygon", "{ hull: Point[]; holes?: Point[][] }");
  register_type<Path> ("Path", "{ points: Point[]; width: number; beginExtension?: number; endExtension?: number; round?: boolean }");
  // Rn: rotation by n degrees counterclockwise. Mn: mirror at the axis at n degrees.
  register_type<Orientation> ("Orientation", "'R0' | 'R90' | 'R180' | 'R270' | 'M0' | 'M45' | 'M90' | 'M135'");
  register_type<Text> ("Text", "{ string: string; position: Point; orientation?: Orientation; size?: number; "
                               "font?: number | null; halign?: 'left' | 'center' | 'right' | null; "
                               "valign?: 'top' | 'center' | 'bottom' | null }");
  register_type<Shape> ("Shape", "({ kind: 'box' } & Rect) | ({ kind: 'polygon' } & Polygon) | ({ kind: 'path' } & Path) | "
                                 "({ kind: 'text' } & Text) | ({ kind: 'edge' } & Edge) | "
                                 "{ kind: 'edgePair'; first: Edge; second: Edge } | ({ kind: 'point' } & Point)");
  // Mirror at the x axis, rotate by angle degrees counterclockwise, scale, then displace.
  register_type<Transform> ("Transform", "{ displacement: Vector; angle?: number; mirror?: boolean; magnification?: number }");
  // Placements at i * a + j * b for i < na, j < nb.
  register_type<InstanceArray> ("InstanceArray", "{ a: Vector; b: Vector; na: number; nb: number }");
  // cell: the child cell placed.
  register_type<Instance> ("Instance", "{ cell: string; transform?: Transform; array?: InstanceArray | null }");
  // Shapes and instances are identified by value. insertShape creates a missing layer.
  register_type<Edit> ("Edit", "{ op: 'insertShape' | 'eraseShape'; cell: string; layer: LayerSpec; shape: Shape } | "
                               "{ op: 'insertInstance' | 'eraseInstance'; cell: string; instance: Instance } | "
                               "{ op: 'createCell' | 'deleteCell'; name: string } | "
                               "{ op: 'renameCell'; name: string; newName: string }");
  register_type<Selected> ("Selected", "{ kind: 'shape'; cell: string; layer: LayerSpec; shape: Shape } | "
                                       "{ kind: 'instance'; cell: string; instance: Instance }");
  // id: valid until the panel changes. label: what the panel shows. source: e.g. '1/0@1'.
  // layer: what the entry draws, null for groups and wildcards. present: the
  // layout has that layer (drawing on one it lacks creates it).
  register_type<LayerEntry> ("LayerEntry", "{ id: number; parent: number | null; group: boolean; label: string; "
                                           "source: string; layer: LayerSpec | null; present: boolean; visible: boolean; "
                                           "fill: string; frame: string }");
  register_type<History> ("History", "{ undo: string | null; redo: string | null }");
  register_type<Change> ("Change", "{ type: 'layoutChanged'; history: History } | { type: 'selectionChanged' } | "
                                   "{ type: 'layerPanelChanged' } | { type: 'modeChanged'; mode: string }");
  register_type<Bytes> ("Uint8Array");
  // Held buttons for moves; the changing button for press and release.
  register_type<Buttons> ("Buttons", "{ left?: boolean; middle?: boolean; right?: boolean; shift?: boolean; control?: boolean; alt?: boolean }");
  register_type<MouseAction> ("MouseAction", "'move' | 'press' | 'release' | 'doubleClick' | 'enter' | 'leave'");
  register_type<Key> ("Key", "'escape' | 'tab' | 'backspace' | 'enter' | 'delete' | 'left' | 'up' | 'right' | 'down' | { char: string }");
  // rgba: width * height * 4 bytes, top row first; a view into module memory, valid until the next call.
  register_type<Frame> ("Frame", "{ width: number; height: number; rgba: Uint8Array }");
  register_type<Command> ("Command", "'delete' | 'cut' | 'copy' | 'paste' | 'pasteMove' | 'selectAll' | 'unselectAll' | 'cancel'");
  register_type<Format> ("Format", "'gds2' | 'oasis'");
  register_type<CellList> ("Cell[]");
  register_type<LayerList> ("Layer[]");
  register_type<ShapeList> ("Shape[]");
  register_type<InstanceList> ("Instance[]");
  register_type<EditList> ("Edit[]");
  register_type<SelectionList> ("Selected[]");
  register_type<LayerEntryList> ("LayerEntry[]");
  register_type<ChangeList> ("Change[]");
  register_type<StringList> ("string[]");
  register_type<LayoutSummary> ("LayoutSummary", "{ format: string; dbu: number; cells: number; topCells: number; layers: number; shapes: number }");

  // Display and editing settings; each field is optional and only the ones
  // given change. grid: the display grid in microns. editGrid: the editing
  // snap grid in microns, null to follow the display grid. connectAngle:
  // the angles new paths and polygons may take.
  register_type<ViewConfig> ("ViewConfig", "{ grid?: number; editGrid?: number | null; snapToObjects?: boolean; "
                                           "gridVisible?: boolean; background?: string; "
                                           "connectAngle?: 'any' | 'diagonal' | 'ortho' }");
  register_type<OptionalString> ("OptionalString", "string | null");
  constant ("klayoutVersion", std::string (KLAYOUT_VERSION));

  function ("inspect(bytes)", &inspect);

  class_<View> ("View")
    .constructor<bool> ()  // editable
    // Loading
    .function ("open(bytes)", &View::open)
    .function ("export(format)", &View::export_layout)
    // Rendering and input
    .function ("resize(width, height)", &View::resize)
    .function ("frame", &View::frame)
    .function ("mouse(action, x, y, buttons)", &View::mouse)
    .function ("wheel(delta, horizontal, x, y, buttons)", &View::wheel)
    .function ("key(key, buttons)", &View::key)
    .function ("zoomFit", &View::zoom_fit)
    // Frames come out at ratio times the CSS size given to resize; mouse
    // input stays in CSS pixels. One ratio per module instance.
    .function ("setDevicePixelRatio(ratio)", &View::set_device_pixel_ratio)
    .function ("configure(config)", &View::configure)
    .function ("viewport", &View::viewport)
    .function ("zoomBox(box)", &View::zoom_box)
    // The layout point under a CSS pixel.
    .function ("pointAt(x, y)", &View::point_at)
    .function ("showCell(name)", &View::show_cell)
    .function ("currentCell", &View::current_cell)
    .function ("modes", &View::modes)
    .function ("mode", &View::mode)
    .function ("switchMode(mode)", &View::switch_mode)
    .function ("setCurrentLayer(layer)", &View::set_current_layer)
    // Editing
    .function ("undo", &View::undo)
    .function ("redo", &View::redo)
    .function ("history", &View::history)
    .function ("command(command)", &View::command)
    .function ("edit(description, edits)", &View::edit)
    // Reading
    .function ("dbu", &View::dbu)
    .function ("cells", &View::cells)
    .function ("layers", &View::layers)
    .function ("shapes(cell, layer)", &View::shapes)
    .function ("instances(cell)", &View::instances)
    .function ("selection", &View::selection)
    // Layer panel
    .function ("layerPanel", &View::layer_panel)
    .function ("setLayerVisible(id, visible)", &View::set_layer_visible)
    .function ("loadLayerProperties(lyp)", &View::load_layer_properties)
    .function ("saveLayerProperties", &View::save_layer_properties)
    // What changed since the last call, by any route.
    .function ("changes", &View::changes);
}
