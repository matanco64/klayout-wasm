//! The messages between JavaScript and the module, as JSON. Requests and
//! replies carry a `type` tag in snake case; layout data uses the types of
//! `klayout_sys::model`. Bump [`PROTOCOL`] on any incompatible change.

use klayout_sys::{
    Buttons, Cell, Command, Edit, Format, History, Instance, Key, Layer, LayerEntry, LayerEntryId, LayerSpec, Mouse,
    Selected, Shape,
};
use serde::{Deserialize, Serialize};

pub const PROTOCOL: u32 = 1;

#[derive(Debug, Deserialize)]
#[serde(tag = "type", rename_all = "snake_case", deny_unknown_fields)]
pub enum Request {
    /// Must come first: checks the protocol version and creates the view.
    Hello { protocol: u32, editable: bool },
    /// Payload: GDS2 or OASIS bytes, format detected from the content.
    Open,
    /// Reply payload: the layout.
    Export { format: Format },
    /// Canvas size in pixels.
    Resize { width: u32, height: u32 },
    /// Reply payload: RGBA pixels, `width * height * 4` bytes, top row first.
    Frame,
    /// `buttons`: held buttons for moves, the changing button for press and release.
    Mouse {
        event: Mouse,
        x: f64,
        y: f64,
        #[serde(default)]
        buttons: Buttons,
    },
    /// `delta`: 120 per wheel notch, positive away from the user.
    Wheel {
        delta: i32,
        #[serde(default)]
        horizontal: bool,
        x: f64,
        y: f64,
        #[serde(default)]
        buttons: Buttons,
    },
    Key {
        key: Key,
        #[serde(default)]
        buttons: Buttons,
    },
    ZoomFit,
    /// One of the names `hello` returned.
    SwitchMode { mode: String },
    /// The layer drawing tools create shapes on.
    SetCurrentLayer { layer: LayerSpec },
    Undo,
    Redo,
    Command { command: Command },
    Cells,
    Layers,
    /// The shapes `cell` itself holds on `layer`.
    Shapes { cell: String, layer: LayerSpec },
    /// The child cells placed in `cell`.
    Instances { cell: String },
    Selection,
    /// One undo step: all edits apply, or none.
    Edit { description: String, edits: Vec<Edit> },
    LayerPanel,
    SetLayerVisible { id: LayerEntryId, visible: bool },
    /// Payload: a `.lyp` file.
    LoadLayerProperties,
    /// Reply payload: a `.lyp` file.
    SaveLayerProperties,
}

#[derive(Debug, Serialize)]
#[serde(tag = "type", rename_all = "snake_case")]
pub enum Reply {
    Hello { protocol: u32, klayout: &'static str, modes: Vec<String> },
    Ok,
    Opened { dbu: f64, cells: Vec<Cell>, layers: Vec<Layer> },
    /// The payload holds the result.
    Bytes,
    /// The payload holds the pixels.
    Frame { width: u32, height: u32 },
    CurrentLayer { found: bool },
    Cells { cells: Vec<Cell> },
    Layers { layers: Vec<Layer> },
    Shapes { shapes: Vec<Shape> },
    Instances { instances: Vec<Instance> },
    Selection { selection: Vec<Selected> },
    LayerPanel { entries: Vec<LayerEntry> },
    Error { message: String },
}

/// Changes since the previous response, whatever caused them.
#[derive(Debug, Serialize)]
#[serde(tag = "type", rename_all = "snake_case")]
pub enum Event {
    /// Edited, undone, redone or opened; re-read what you show.
    LayoutChanged { history: History },
    SelectionChanged,
    LayerPanelChanged,
    ModeChanged { mode: String },
}

#[derive(Debug, Serialize)]
pub struct Response {
    pub reply: Reply,
    pub events: Vec<Event>,
}
