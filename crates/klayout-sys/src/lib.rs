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
