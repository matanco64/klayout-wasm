//! Layout data as plain Rust values. Coordinates are integer database units
//! (see [`crate::View::dbu`]), as KLayout stores them.

#[cfg(feature = "serde")]
use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Default)]
#[cfg_attr(feature = "serde", derive(Serialize, Deserialize))]
pub struct Point {
    pub x: i32,
    pub y: i32,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Default)]
#[cfg_attr(feature = "serde", derive(Serialize, Deserialize))]
pub struct Vector {
    pub x: i32,
    pub y: i32,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
#[cfg_attr(feature = "serde", derive(Serialize, Deserialize))]
pub struct Rect {
    pub left: i32,
    pub bottom: i32,
    pub right: i32,
    pub top: i32,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
#[cfg_attr(feature = "serde", derive(Serialize, Deserialize))]
pub struct Edge {
    pub from: Point,
    pub to: Point,
}

/// A GDS layer/datatype pair: how shapes name their layer.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, PartialOrd, Ord)]
#[cfg_attr(feature = "serde", derive(Serialize, Deserialize))]
pub struct LayerSpec {
    pub layer: i32,
    pub datatype: i32,
}

/// A layer the layout contains.
#[derive(Debug, Clone, PartialEq, Eq)]
#[cfg_attr(feature = "serde", derive(Serialize, Deserialize))]
pub struct Layer {
    #[cfg_attr(feature = "serde", serde(flatten))]
    pub spec: LayerSpec,
    /// OASIS layer name, if any.
    pub name: Option<String>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
#[cfg_attr(feature = "serde", derive(Serialize, Deserialize))]
pub struct Cell {
    pub name: String,
    /// Not instantiated by any other cell.
    pub top: bool,
    /// Including child cells; `None` for an empty cell.
    pub bbox: Option<Rect>,
}

/// KLayout stores polygons in a normal form: the hull clockwise and holes
/// counterclockwise, each starting at its lowest-leftmost point, without
/// redundant points. A polygon given in another form reads back normalized;
/// erasing by value matches either form.
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
#[cfg_attr(feature = "serde", derive(Serialize, Deserialize))]
pub struct Polygon {
    pub hull: Vec<Point>,
    pub holes: Vec<Vec<Point>>,
}

#[derive(Debug, Clone, PartialEq, Eq, Hash)]
#[cfg_attr(feature = "serde", derive(Serialize, Deserialize))]
pub struct Path {
    pub points: Vec<Point>,
    pub width: i32,
    pub begin_extension: i32,
    pub end_extension: i32,
    pub round: bool,
}

/// KLayout's eight fixed orientations: rotations counterclockwise, and
/// mirroring at the x axis followed by the rotation (M45 = mirror, R90).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Default)]
#[cfg_attr(feature = "serde", derive(Serialize, Deserialize))]
#[repr(u8)]
pub enum Orientation {
    #[default]
    R0 = 0,
    R90 = 1,
    R180 = 2,
    R270 = 3,
    M0 = 4,
    M45 = 5,
    M90 = 6,
    M135 = 7,
}

impl Orientation {
    pub(crate) fn from_code(code: u8) -> Option<Self> {
        use Orientation::*;
        [R0, R90, R180, R270, M0, M45, M90, M135].get(usize::from(code)).copied()
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
#[cfg_attr(feature = "serde", derive(Serialize, Deserialize))]
#[cfg_attr(feature = "serde", serde(rename_all = "snake_case"))]
pub enum HAlign {
    Left = 0,
    Center = 1,
    Right = 2,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
#[cfg_attr(feature = "serde", derive(Serialize, Deserialize))]
#[cfg_attr(feature = "serde", serde(rename_all = "snake_case"))]
pub enum VAlign {
    Top = 0,
    Center = 1,
    Bottom = 2,
}

#[derive(Debug, Clone, PartialEq, Eq, Hash)]
#[cfg_attr(feature = "serde", derive(Serialize, Deserialize))]
pub struct Text {
    pub string: String,
    pub position: Point,
    #[cfg_attr(feature = "serde", serde(default))]
    pub orientation: Orientation,
    /// Text height; 0 means unspecified.
    #[cfg_attr(feature = "serde", serde(default))]
    pub size: i32,
    #[cfg_attr(feature = "serde", serde(default))]
    pub font: Option<u32>,
    #[cfg_attr(feature = "serde", serde(default))]
    pub halign: Option<HAlign>,
    #[cfg_attr(feature = "serde", serde(default))]
    pub valign: Option<VAlign>,
}

/// A geometric object on a layer. GDS layouts use boxes, polygons, paths
/// and texts; edges, edge pairs and points occur in KLayout-made data.
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
#[cfg_attr(feature = "serde", derive(Serialize, Deserialize))]
#[cfg_attr(feature = "serde", serde(tag = "kind", rename_all = "snake_case"))]
pub enum Shape {
    Box(Rect),
    Polygon(Polygon),
    Path(Path),
    Text(Text),
    Edge(Edge),
    EdgePair { first: Edge, second: Edge },
    Point(Point),
}

/// Placement of a child cell: mirror at the x axis (if `mirror`), then
/// rotate by `angle` degrees counterclockwise, scale by `magnification`,
/// then move by `displacement`.
#[derive(Debug, Clone, Copy, PartialEq)]
#[cfg_attr(feature = "serde", derive(Serialize, Deserialize))]
pub struct Transform {
    pub displacement: Vector,
    #[cfg_attr(feature = "serde", serde(default))]
    pub angle: f64,
    #[cfg_attr(feature = "serde", serde(default))]
    pub mirror: bool,
    #[cfg_attr(feature = "serde", serde(default = "one"))]
    pub magnification: f64,
}

#[cfg(feature = "serde")]
fn one() -> f64 {
    1.0
}

impl Default for Transform {
    fn default() -> Self {
        Self { displacement: Vector::default(), angle: 0.0, mirror: false, magnification: 1.0 }
    }
}

/// A regular array: placements at `i * a + j * b` for `i < na`, `j < nb`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[cfg_attr(feature = "serde", derive(Serialize, Deserialize))]
pub struct Array {
    pub a: Vector,
    pub b: Vector,
    pub na: u64,
    pub nb: u64,
}

/// A placement of the cell named `cell` inside another cell.
#[derive(Debug, Clone, PartialEq)]
#[cfg_attr(feature = "serde", derive(Serialize, Deserialize))]
pub struct Instance {
    pub cell: String,
    #[cfg_attr(feature = "serde", serde(default))]
    pub transform: Transform,
    #[cfg_attr(feature = "serde", serde(default))]
    pub array: Option<Array>,
}

/// One change to the layout. Shapes and instances are identified by value:
/// erasing removes one object equal to the one given.
#[derive(Debug, Clone, PartialEq)]
#[cfg_attr(feature = "serde", derive(Serialize, Deserialize))]
#[cfg_attr(feature = "serde", serde(tag = "op", rename_all = "snake_case"))]
pub enum Edit {
    /// Creates the layer if the layout lacks it.
    InsertShape { cell: String, layer: LayerSpec, shape: Shape },
    EraseShape { cell: String, layer: LayerSpec, shape: Shape },
    /// `instance.cell` is the child; `cell` is where it is placed.
    InsertInstance { cell: String, instance: Instance },
    EraseInstance { cell: String, instance: Instance },
    CreateCell { name: String },
    DeleteCell { name: String },
    RenameCell { name: String, new_name: String },
}

/// What the editing tools have selected.
#[derive(Debug, Clone, PartialEq)]
#[cfg_attr(feature = "serde", derive(Serialize, Deserialize))]
#[cfg_attr(feature = "serde", serde(tag = "kind", rename_all = "snake_case"))]
pub enum Selected {
    Shape { cell: String, layer: LayerSpec, shape: Shape },
    /// `cell` holds the instance.
    Instance { cell: String, instance: Instance },
}

/// Desktop KLayout's Edit menu commands, acting on the selection.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[cfg_attr(feature = "serde", derive(Serialize, Deserialize))]
#[cfg_attr(feature = "serde", serde(rename_all = "snake_case"))]
pub enum Command {
    Delete = 0,
    Cut = 1,
    Copy = 2,
    /// Pastes in place.
    Paste = 3,
    /// Pastes and attaches the pasted objects to the mouse until a click.
    PasteMove = 4,
    SelectAll = 5,
    UnselectAll = 6,
    /// Escape: aborts edits, clears the selection, returns to select mode.
    Cancel = 7,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[cfg_attr(feature = "serde", derive(Serialize, Deserialize))]
#[cfg_attr(feature = "serde", serde(rename_all = "snake_case"))]
pub enum Format {
    Gds2 = 0,
    Oasis = 1,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
#[cfg_attr(feature = "serde", derive(Serialize, Deserialize))]
pub struct Rgb {
    pub r: u8,
    pub g: u8,
    pub b: u8,
}

impl Rgb {
    pub(crate) fn from_argb(argb: u32) -> Self {
        let [_, r, g, b] = argb.to_be_bytes();
        Self { r, g, b }
    }
}

/// An entry in KLayout's layer panel. Ids are only valid until the panel
/// changes (loading a layout or layer properties, a new layer appearing).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
#[cfg_attr(feature = "serde", derive(Serialize, Deserialize))]
#[cfg_attr(feature = "serde", serde(transparent))]
pub struct LayerEntryId(pub u64);

#[derive(Debug, Clone, PartialEq, Eq)]
#[cfg_attr(feature = "serde", derive(Serialize, Deserialize))]
pub struct LayerEntry {
    pub id: LayerEntryId,
    pub parent: Option<LayerEntryId>,
    /// A group of other entries rather than a layer.
    pub group: bool,
    /// What the panel displays: the name from the layer properties, or the source.
    pub label: String,
    /// KLayout's source expression, e.g. `1/0@1`.
    pub source: String,
    /// The layout layer this entry draws, if it resolves to one.
    pub layer: Option<LayerSpec>,
    pub visible: bool,
    pub fill: Rgb,
    pub frame: Rgb,
}

/// What undo and redo would do next, as KLayout labels it.
#[derive(Debug, Clone, PartialEq, Eq, Default)]
#[cfg_attr(feature = "serde", derive(Serialize, Deserialize))]
pub struct History {
    pub undo: Option<String>,
    pub redo: Option<String>,
}

/// A snapshot for noticing changes. Between two snapshots, `revision`
/// differs if the layout was edited, undone or redone, `selection` if the
/// selection changed, `layer_panel` if the layer panel did.
#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct ViewState {
    pub revision: (u64, u64),
    pub history: History,
    pub selection: u64,
    pub layer_panel: u64,
    pub mode: String,
}

/// Mouse buttons held or pressed, and keyboard modifiers.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
#[cfg_attr(feature = "serde", derive(Serialize, Deserialize))]
#[cfg_attr(feature = "serde", serde(default))]
pub struct Buttons {
    pub left: bool,
    pub middle: bool,
    pub right: bool,
    pub shift: bool,
    pub control: bool,
    pub alt: bool,
}

impl Buttons {
    pub const LEFT: Self = Self { left: true, middle: false, right: false, shift: false, control: false, alt: false };
    pub const NONE: Self = Self { left: false, middle: false, right: false, shift: false, control: false, alt: false };

    /// As `lay::ShiftButton | ... | lay::RightButton`.
    pub(crate) fn bits(self) -> u32 {
        [self.shift, self.control, self.alt, self.left, self.middle, self.right]
            .iter()
            .enumerate()
            .map(|(bit, &on)| u32::from(on) << bit)
            .sum()
    }
}

/// A pointer event, in canvas pixels.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[cfg_attr(feature = "serde", derive(Serialize, Deserialize))]
#[cfg_attr(feature = "serde", serde(rename_all = "snake_case"))]
pub enum Mouse {
    Move = 0,
    Press = 1,
    Release = 2,
    DoubleClick = 3,
    Enter = 4,
    Leave = 5,
}

/// The keys KLayout's tools react to.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[cfg_attr(feature = "serde", derive(Serialize, Deserialize))]
#[cfg_attr(feature = "serde", serde(rename_all = "snake_case"))]
pub enum Key {
    Escape,
    Tab,
    Backspace,
    Enter,
    Delete,
    Left,
    Up,
    Right,
    Down,
    /// A printable key, as typed.
    Char(char),
}

impl Key {
    /// The Qt key code KLayout expects.
    pub(crate) fn qt_code(self) -> u32 {
        match self {
            Key::Escape => 0x0100_0000,
            Key::Tab => 0x0100_0001,
            Key::Backspace => 0x0100_0003,
            Key::Enter => 0x0100_0004,
            Key::Delete => 0x0100_0007,
            Key::Left => 0x0100_0012,
            Key::Up => 0x0100_0013,
            Key::Right => 0x0100_0014,
            Key::Down => 0x0100_0015,
            // Qt codes letters by their upper case.
            Key::Char(c) => c.to_uppercase().next().map_or(0, u32::from),
        }
    }
}
