//! Raw bindings to the KLayout C++ shim in `cpp/shim.cc`, plus [`Layout`], a
//! thin owning wrapper. KLayout itself is only linked for
//! `wasm32-unknown-emscripten`.

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
}

/// Mouse buttons and modifiers, as `lay::LeftButton` etc.
pub mod buttons {
    pub const SHIFT: u32 = 0x01;
    pub const CONTROL: u32 = 0x02;
    pub const ALT: u32 = 0x04;
    pub const LEFT: u32 = 0x08;
    pub const MIDDLE: u32 = 0x10;
    pub const RIGHT: u32 = 0x20;
}

/// A pointer event, in canvas pixels.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Mouse {
    Move = 0,
    Press = 1,
    Release = 2,
    DoubleClick = 3,
    Enter = 4,
    Leave = 5,
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

    pub fn mouse(&mut self, event: Mouse, x: f64, y: f64, buttons: u32) -> Result<(), Error> {
        status(unsafe { kl_view_mouse(self.0.as_ptr(), event as i32, x, y, buttons) })
    }

    pub fn wheel(&mut self, delta: i32, horizontal: bool, x: f64, y: f64, buttons: u32) -> Result<(), Error> {
        status(unsafe { kl_view_wheel(self.0.as_ptr(), delta, horizontal as i32, x, y, buttons) })
    }

    pub fn key(&mut self, key: u32, buttons: u32) -> Result<(), Error> {
        status(unsafe { kl_view_key(self.0.as_ptr(), key, buttons) })
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
}

impl Drop for View {
    fn drop(&mut self) {
        unsafe { kl_view_free(self.0.as_ptr()) }
    }
}
