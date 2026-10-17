//! KLayout compiled to WebAssembly: raw bindings to the C++ shim in `cpp/`,
//! and safe owning wrappers: [`Layout`], a bare layout database, and
//! [`View`], KLayout's layout view with its editing tools. Structured data
//! crosses the boundary in the encoding of `cpp/codec.h`; [`model`] holds
//! the Rust types. KLayout itself is only linked for
//! `wasm32-unknown-emscripten`.

mod codec;
pub mod model;

pub use model::*;

/// The KLayout release compiled in.
pub const KLAYOUT_VERSION: &str = env!("KLAYOUT_VERSION");

use std::ffi::CStr;
use std::fmt;
use std::os::raw::c_char;
use std::ptr::NonNull;

#[repr(C)]
pub struct RawLayout {
    _private: [u8; 0],
}

unsafe extern "C" {
    pub fn kl_last_error() -> *const c_char;
    pub fn kl_layout_new() -> *mut RawLayout;
    pub fn kl_layout_free(layout: *mut RawLayout);
    pub fn kl_layout_read(layout: *mut RawLayout, data: *const u8, len: usize) -> i32;
    pub fn kl_layout_dbu(layout: *const RawLayout) -> f64;
    pub fn kl_layout_cell_count(layout: *const RawLayout) -> usize;
    pub fn kl_layout_top_cell_count(layout: *const RawLayout) -> usize;
    pub fn kl_layout_layer_count(layout: *const RawLayout) -> usize;
    pub fn kl_layout_shape_count(layout: *const RawLayout) -> usize;
}

/// A KLayout C++ exception, caught at the boundary.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Error(pub String);

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(&self.0)
    }
}

impl std::error::Error for Error {}

fn last_error() -> Error {
    // SAFETY: the shim always returns a valid, NUL-terminated string that
    // lives until the next failing call on this thread.
    Error(unsafe { CStr::from_ptr(kl_last_error()) }.to_string_lossy().into_owned())
}

/// An owned `db::Layout`.
pub struct Layout(NonNull<RawLayout>);

impl Layout {
    pub fn new() -> Result<Self, Error> {
        // SAFETY: no preconditions; null means the constructor threw.
        NonNull::new(unsafe { kl_layout_new() }).map(Self).ok_or_else(last_error)
    }

    /// Reads GDS2 or OASIS from memory; the format is detected.
    pub fn read(&mut self, data: &[u8]) -> Result<(), Error> {
        // SAFETY: `data` is valid for `len` bytes for the duration of the call.
        match unsafe { kl_layout_read(self.0.as_ptr(), data.as_ptr(), data.len()) } {
            0 => Ok(()),
            _ => Err(last_error()),
        }
    }

    pub fn dbu(&self) -> f64 {
        unsafe { kl_layout_dbu(self.0.as_ptr()) }
    }

    pub fn cell_count(&self) -> usize {
        unsafe { kl_layout_cell_count(self.0.as_ptr()) }
    }

    pub fn top_cell_count(&self) -> usize {
        unsafe { kl_layout_top_cell_count(self.0.as_ptr()) }
    }

    pub fn layer_count(&self) -> usize {
        unsafe { kl_layout_layer_count(self.0.as_ptr()) }
    }

    pub fn shape_count(&self) -> usize {
        unsafe { kl_layout_shape_count(self.0.as_ptr()) }
    }
}

impl Drop for Layout {
    fn drop(&mut self) {
        // SAFETY: we own the pointer and free it exactly once.
        unsafe { kl_layout_free(self.0.as_ptr()) }
    }
}

#[repr(C)]
pub struct RawView {
    _private: [u8; 0],
}

unsafe extern "C" {
    pub fn kl_view_new(editable: i32) -> *mut RawView;
    pub fn kl_view_free(view: *mut RawView);
    pub fn kl_view_load(view: *mut RawView, path: *const c_char) -> i32;
    pub fn kl_view_resize(view: *mut RawView, width: u32, height: u32) -> i32;
    pub fn kl_view_zoom_fit(view: *mut RawView) -> i32;
    pub fn kl_view_mode_names(view: *mut RawView) -> *const c_char;
    pub fn kl_view_mode_name(view: *mut RawView) -> *const c_char;
    pub fn kl_view_switch_mode(view: *mut RawView, name: *const c_char) -> i32;
    pub fn kl_view_mouse(view: *mut RawView, kind: i32, x: f64, y: f64, buttons: u32) -> i32;
    pub fn kl_view_wheel(view: *mut RawView, delta: i32, horizontal: i32, x: f64, y: f64, buttons: u32) -> i32;
    pub fn kl_view_key(view: *mut RawView, key: u32, buttons: u32) -> i32;
    pub fn kl_view_frame(view: *mut RawView, width: *mut u32, height: *mut u32) -> *const u8;
    pub fn kl_view_set_current_layer(view: *mut RawView, layer: i32, datatype: i32) -> i32;
    pub fn kl_view_save(view: *mut RawView, path: *const c_char) -> i32;
    pub fn kl_view_shape_count(view: *mut RawView) -> usize;
    pub fn kl_view_undo(view: *mut RawView) -> i32;
    pub fn kl_view_redo(view: *mut RawView) -> i32;
    pub fn kl_view_out(view: *mut RawView, len: *mut usize) -> *const u8;
    pub fn kl_view_dbu(view: *mut RawView) -> f64;
    pub fn kl_view_export(view: *mut RawView, format: i32) -> i32;
    pub fn kl_view_cells(view: *mut RawView) -> i32;
    pub fn kl_view_layers(view: *mut RawView) -> i32;
    pub fn kl_view_shapes(view: *mut RawView, cell: *const c_char, layer: i32, datatype: i32) -> i32;
    pub fn kl_view_instances(view: *mut RawView, cell: *const c_char) -> i32;
    pub fn kl_view_edit(view: *mut RawView, description: *const c_char, data: *const u8, len: usize) -> i32;
    pub fn kl_view_command(view: *mut RawView, command: i32) -> i32;
    pub fn kl_view_selection(view: *mut RawView) -> i32;
    pub fn kl_view_layer_panel(view: *mut RawView) -> i32;
    pub fn kl_view_set_layer_visible(view: *mut RawView, id: u64, visible: i32) -> i32;
    pub fn kl_view_load_layer_properties(view: *mut RawView, path: *const c_char) -> i32;
    pub fn kl_view_save_layer_properties(view: *mut RawView, path: *const c_char) -> i32;
    pub fn kl_view_state(view: *mut RawView) -> i32;
}

/// A rendered frame: tightly packed RGBA, row-major, top row first.
pub struct Frame<'a> {
    pub width: u32,
    pub height: u32,
    pub rgba: &'a [u8],
}

fn status(code: i32) -> Result<(), Error> {
    if code == 0 { Ok(()) } else { Err(last_error()) }
}

fn c_string(s: &str) -> Result<std::ffi::CString, Error> {
    std::ffi::CString::new(s).map_err(|_| Error(format!("NUL byte in {s:?}")))
}

/// KLayout's Qt-free `lay::LayoutView`, drawing synchronously.
pub struct View(NonNull<RawView>);

impl View {
    pub fn new(editable: bool) -> Result<Self, Error> {
        NonNull::new(unsafe { kl_view_new(editable as i32) }).map(Self).ok_or_else(last_error)
    }

    /// Loads GDS2 or OASIS from a path in the Emscripten filesystem.
    pub fn load(&mut self, path: &str) -> Result<(), Error> {
        let path = c_string(path)?;
        status(unsafe { kl_view_load(self.0.as_ptr(), path.as_ptr()) })
    }

    pub fn resize(&mut self, width: u32, height: u32) -> Result<(), Error> {
        status(unsafe { kl_view_resize(self.0.as_ptr(), width, height) })
    }

    pub fn zoom_fit(&mut self) -> Result<(), Error> {
        status(unsafe { kl_view_zoom_fit(self.0.as_ptr()) })
    }

    pub fn mode_names(&mut self) -> Vec<String> {
        let text = unsafe { CStr::from_ptr(kl_view_mode_names(self.0.as_ptr())) };
        text.to_string_lossy().lines().map(str::to_owned).collect()
    }

    pub fn mode_name(&mut self) -> String {
        unsafe { CStr::from_ptr(kl_view_mode_name(self.0.as_ptr())) }.to_string_lossy().into_owned()
    }

    pub fn switch_mode(&mut self, name: &str) -> Result<(), Error> {
        let name = c_string(name)?;
        status(unsafe { kl_view_switch_mode(self.0.as_ptr(), name.as_ptr()) })
    }

    /// `buttons`: held buttons for moves, the changing button for press and release.
    pub fn mouse(&mut self, event: Mouse, x: f64, y: f64, buttons: Buttons) -> Result<(), Error> {
        status(unsafe { kl_view_mouse(self.0.as_ptr(), event as i32, x, y, buttons.bits()) })
    }

    /// `delta` in Qt units: 120 per wheel notch, positive away from the user.
    pub fn wheel(&mut self, delta: i32, horizontal: bool, x: f64, y: f64, buttons: Buttons) -> Result<(), Error> {
        status(unsafe { kl_view_wheel(self.0.as_ptr(), delta, horizontal as i32, x, y, buttons.bits()) })
    }

    pub fn key(&mut self, key: Key, buttons: Buttons) -> Result<(), Error> {
        status(unsafe { kl_view_key(self.0.as_ptr(), key.qt_code(), buttons.bits()) })
    }

    /// Pumps the view and returns the current image.
    pub fn frame(&mut self) -> Result<Frame<'_>, Error> {
        let (mut width, mut height) = (0, 0);
        let data = unsafe { kl_view_frame(self.0.as_ptr(), &mut width, &mut height) };
        if data.is_null() {
            return Err(last_error());
        }
        // SAFETY: the shim owns `width * height * 4` bytes behind `data`, kept
        // alive until the next call on this view, which needs `&mut self`.
        let rgba = unsafe { std::slice::from_raw_parts(data, width as usize * height as usize * 4) };
        Ok(Frame { width, height, rgba })
    }

    /// Selects the layer drawing tools create shapes on. `Ok(false)` if the
    /// view does not show that layer.
    pub fn set_current_layer(&mut self, layer: i32, datatype: i32) -> Result<bool, Error> {
        match unsafe { kl_view_set_current_layer(self.0.as_ptr(), layer, datatype) } {
            -1 => Err(last_error()),
            found => Ok(found == 1),
        }
    }

    /// Writes the shown layout; the format follows the file extension.
    pub fn save(&mut self, path: &str) -> Result<(), Error> {
        let path = c_string(path)?;
        status(unsafe { kl_view_save(self.0.as_ptr(), path.as_ptr()) })
    }

    pub fn shape_count(&mut self) -> usize {
        unsafe { kl_view_shape_count(self.0.as_ptr()) }
    }

    pub fn undo(&mut self) -> Result<(), Error> {
        status(unsafe { kl_view_undo(self.0.as_ptr()) })
    }

    pub fn redo(&mut self) -> Result<(), Error> {
        status(unsafe { kl_view_redo(self.0.as_ptr()) })
    }

    /// Runs a shim call that leaves encoded data behind, and decodes it.
    fn query<T>(&mut self, code: i32, decode: impl FnOnce(&mut codec::Reader) -> Result<T, Error>) -> Result<T, Error> {
        status(code)?;
        let mut reader = codec::Reader::new(self.out());
        let value = decode(&mut reader)?;
        reader.finish()?;
        Ok(value)
    }

    fn out(&mut self) -> &[u8] {
        let mut len = 0;
        let data = unsafe { kl_view_out(self.0.as_ptr(), &mut len) };
        if len == 0 {
            return &[];
        }
        // SAFETY: the shim keeps `len` bytes behind `data` until the next
        // call on this view, which needs `&mut self`.
        unsafe { std::slice::from_raw_parts(data, len) }
    }

    /// Database unit in micrometers.
    pub fn dbu(&mut self) -> Result<f64, Error> {
        let dbu = unsafe { kl_view_dbu(self.0.as_ptr()) };
        if dbu > 0.0 { Ok(dbu) } else { Err(last_error()) }
    }

    /// The layout as a GDS2 or OASIS stream.
    pub fn export(&mut self, format: Format) -> Result<Vec<u8>, Error> {
        status(unsafe { kl_view_export(self.0.as_ptr(), format as i32) })?;
        Ok(self.out().to_vec())
    }

    pub fn cells(&mut self) -> Result<Vec<Cell>, Error> {
        let code = unsafe { kl_view_cells(self.0.as_ptr()) };
        self.query(code, |r| {
            r.list(|r| {
                let (name, top, empty, bbox) = (r.str()?, r.bool()?, r.bool()?, r.rect()?);
                Ok(Cell { name, top, bbox: (!empty).then_some(bbox) })
            })
        })
    }

    pub fn layers(&mut self) -> Result<Vec<Layer>, Error> {
        let code = unsafe { kl_view_layers(self.0.as_ptr()) };
        self.query(code, |r| {
            r.list(|r| {
                let (spec, name) = (r.layer()?, r.str()?);
                Ok(Layer { spec, name: (!name.is_empty()).then_some(name) })
            })
        })
    }

    /// The shapes `cell` itself holds on `layer`, not those of its children.
    pub fn shapes(&mut self, cell: &str, layer: LayerSpec) -> Result<Vec<Shape>, Error> {
        let cell = c_string(cell)?;
        let code = unsafe { kl_view_shapes(self.0.as_ptr(), cell.as_ptr(), layer.layer, layer.datatype) };
        self.query(code, |r| r.list(codec::Reader::shape))
    }

    /// The child cells placed in `cell`.
    pub fn instances(&mut self, cell: &str) -> Result<Vec<Instance>, Error> {
        let cell = c_string(cell)?;
        let code = unsafe { kl_view_instances(self.0.as_ptr(), cell.as_ptr()) };
        self.query(code, |r| r.list(codec::Reader::instance))
    }

    /// Applies `edits` as one undo step named `description`: all of them,
    /// or none if any fails.
    pub fn edit(&mut self, description: &str, edits: &[Edit]) -> Result<(), Error> {
        let mut data = codec::Writer::default();
        for edit in edits {
            data.edit(edit)?;
        }
        let description = c_string(description)?;
        status(unsafe { kl_view_edit(self.0.as_ptr(), description.as_ptr(), data.0.as_ptr(), data.0.len()) })
    }

    pub fn command(&mut self, command: Command) -> Result<(), Error> {
        status(unsafe { kl_view_command(self.0.as_ptr(), command as i32) })
    }

    pub fn selection(&mut self) -> Result<Vec<Selected>, Error> {
        let code = unsafe { kl_view_selection(self.0.as_ptr()) };
        self.query(code, |r| {
            r.list(|r| match r.u8()? {
                0 => Ok(Selected::Shape { cell: r.str()?, layer: r.layer()?, shape: r.shape()? }),
                1 => Ok(Selected::Instance { cell: r.str()?, instance: r.instance()? }),
                _ => Err(Error("malformed data from KLayout".into())),
            })
        })
    }

    /// The layer panel, depth first.
    pub fn layer_panel(&mut self) -> Result<Vec<LayerEntry>, Error> {
        let code = unsafe { kl_view_layer_panel(self.0.as_ptr()) };
        self.query(code, |r| {
            r.list(|r| {
                let id = LayerEntryId(r.u64()?);
                let (has_parent, parent) = (r.bool()?, LayerEntryId(r.u64()?));
                let (group, label, source) = (r.bool()?, r.str()?, r.str()?);
                let (has_layer, layer) = (r.bool()?, r.layer()?);
                Ok(LayerEntry {
                    id,
                    parent: has_parent.then_some(parent),
                    group,
                    label,
                    source,
                    layer: has_layer.then_some(layer),
                    visible: r.bool()?,
                    fill: Rgb::from_argb(r.u32()?),
                    frame: Rgb::from_argb(r.u32()?),
                })
            })
        })
    }

    pub fn set_layer_visible(&mut self, id: LayerEntryId, visible: bool) -> Result<(), Error> {
        status(unsafe { kl_view_set_layer_visible(self.0.as_ptr(), id.0, visible as i32) })
    }

    /// Replaces the layer panel with a `.lyp` file from the Emscripten filesystem.
    pub fn load_layer_properties(&mut self, path: &str) -> Result<(), Error> {
        let path = c_string(path)?;
        status(unsafe { kl_view_load_layer_properties(self.0.as_ptr(), path.as_ptr()) })
    }

    pub fn save_layer_properties(&mut self, path: &str) -> Result<(), Error> {
        let path = c_string(path)?;
        status(unsafe { kl_view_save_layer_properties(self.0.as_ptr(), path.as_ptr()) })
    }

    pub fn state(&mut self) -> Result<ViewState, Error> {
        let code = unsafe { kl_view_state(self.0.as_ptr()) };
        self.query(code, |r| {
            let revision = (r.u64()?, r.u64()?);
            let (can_undo, undo) = (r.bool()?, r.str()?);
            let (can_redo, redo) = (r.bool()?, r.str()?);
            Ok(ViewState {
                revision,
                history: History { undo: can_undo.then_some(undo), redo: can_redo.then_some(redo) },
                selection: r.u64()?,
                layer_panel: r.u64()?,
                mode: r.str()?,
            })
        })
    }
}

impl Drop for View {
    fn drop(&mut self) {
        unsafe { kl_view_free(self.0.as_ptr()) }
    }
}
