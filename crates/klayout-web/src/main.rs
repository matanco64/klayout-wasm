//! The browser module: one KLayout view, driven from JavaScript through the
//! `klw_*` functions below. JavaScript forwards canvas input and paints the
//! frames; every decision about layout, modes and editing stays in KLayout.
//!
//! Errors are reported as a negative return value; `klw_error` returns the
//! message as a NUL-terminated string.

use klayout_sys::{Mouse, View};
use std::cell::RefCell;
use std::ffi::{CStr, CString, c_char};

struct State {
    view: View,
    frame: (u32, u32, *const u8),
    text: CString,
    error: CString,
}

thread_local! {
    static STATE: RefCell<Option<State>> = const { RefCell::new(None) };
}

/// Where uploaded layouts are written in the Emscripten filesystem.
const LAYOUT_PATH: &str = "/layout.bin";

fn with<R>(fail: R, f: impl FnOnce(&mut State) -> Result<R, klayout_sys::Error>) -> R {
    STATE.with_borrow_mut(|state| match state.as_mut() {
        None => fail,
        Some(state) => f(state).unwrap_or_else(|e| {
            state.error = CString::new(e.0).unwrap_or_default();
            fail
        }),
    })
}

fn ok(result: Result<(), klayout_sys::Error>) -> Result<i32, klayout_sys::Error> {
    result.map(|()| 0)
}

/// Creates the view. Returns 0, or -1 if KLayout could not construct it.
#[unsafe(no_mangle)]
pub extern "C" fn klw_new(editable: i32) -> i32 {
    match View::new(editable != 0) {
        Ok(view) => {
            STATE.set(Some(State {
                view,
                frame: (0, 0, std::ptr::null()),
                text: CString::default(),
                error: CString::default(),
            }));
            0
        }
        Err(_) => -1,
    }
}

/// Loads GDS2 or OASIS bytes (detected from the content) and fits them.
///
/// # Safety
/// `data` must point to `len` readable bytes.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn klw_load(data: *const u8, len: usize) -> i32 {
    // SAFETY: guaranteed by the caller.
    let bytes = unsafe { std::slice::from_raw_parts(data, len) };
    with(-1, |s| {
        std::fs::write(LAYOUT_PATH, bytes).map_err(|e| klayout_sys::Error(e.to_string()))?;
        ok(s.view.load(LAYOUT_PATH))
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn klw_resize(width: u32, height: u32) -> i32 {
    with(-1, |s| ok(s.view.resize(width, height)))
}

#[unsafe(no_mangle)]
pub extern "C" fn klw_zoom_fit() -> i32 {
    with(-1, |s| ok(s.view.zoom_fit()))
}

/// Newline-separated mode names, valid until the next text-returning call.
#[unsafe(no_mangle)]
pub extern "C" fn klw_modes() -> *const c_char {
    with(std::ptr::null(), |s| {
        s.text = CString::new(s.view.mode_names().join("\n")).unwrap_or_default();
        Ok(s.text.as_ptr())
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn klw_mode() -> *const c_char {
    with(std::ptr::null(), |s| {
        s.text = CString::new(s.view.mode_name()).unwrap_or_default();
        Ok(s.text.as_ptr())
    })
}

/// # Safety
/// `name` must be a NUL-terminated string.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn klw_switch_mode(name: *const c_char) -> i32 {
    // SAFETY: guaranteed by the caller.
    let name = unsafe { CStr::from_ptr(name) }.to_string_lossy();
    with(-1, |s| ok(s.view.switch_mode(&name)))
}

/// `kind`: 0 move, 1 press, 2 release, 3 double click, 4 enter, 5 leave.
#[unsafe(no_mangle)]
pub extern "C" fn klw_mouse(kind: i32, x: f64, y: f64, buttons: u32) -> i32 {
    let event = match kind {
        0 => Mouse::Move,
        1 => Mouse::Press,
        2 => Mouse::Release,
        3 => Mouse::DoubleClick,
        4 => Mouse::Enter,
        5 => Mouse::Leave,
        _ => return -1,
    };
    with(-1, |s| ok(s.view.mouse(event, x, y, buttons)))
}

#[unsafe(no_mangle)]
pub extern "C" fn klw_wheel(delta: i32, horizontal: i32, x: f64, y: f64, buttons: u32) -> i32 {
    with(-1, |s| ok(s.view.wheel(delta, horizontal != 0, x, y, buttons)))
}

/// `key` is a Qt key code, as KLayout expects.
#[unsafe(no_mangle)]
pub extern "C" fn klw_key(key: u32, buttons: u32) -> i32 {
    with(-1, |s| ok(s.view.key(key, buttons)))
}

/// Renders the current frame. Returns a pointer to `width * height * 4`
/// RGBA bytes (see `klw_frame_width`/`klw_frame_height`), valid until the
/// next call into the module, or null on error.
#[unsafe(no_mangle)]
pub extern "C" fn klw_frame() -> *const u8 {
    with(std::ptr::null(), |s| {
        let frame = s.view.frame()?;
        s.frame = (frame.width, frame.height, frame.rgba.as_ptr());
        Ok(s.frame.2)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn klw_frame_width() -> u32 {
    with(0, |s| Ok(s.frame.0))
}

#[unsafe(no_mangle)]
pub extern "C" fn klw_frame_height() -> u32 {
    with(0, |s| Ok(s.frame.1))
}

/// Returns 1 if the view shows `layer/datatype` and it is now current, 0 if
/// the view does not show it, -1 on error.
#[unsafe(no_mangle)]
pub extern "C" fn klw_set_current_layer(layer: i32, datatype: i32) -> i32 {
    with(-1, |s| s.view.set_current_layer(layer, datatype).map(i32::from))
}

#[unsafe(no_mangle)]
pub extern "C" fn klw_undo() -> i32 {
    with(-1, |s| ok(s.view.undo()))
}

#[unsafe(no_mangle)]
pub extern "C" fn klw_redo() -> i32 {
    with(-1, |s| ok(s.view.redo()))
}

#[unsafe(no_mangle)]
pub extern "C" fn klw_shape_count() -> usize {
    with(0, |s| Ok(s.view.shape_count()))
}

#[unsafe(no_mangle)]
pub extern "C" fn klw_error() -> *const c_char {
    with(std::ptr::null(), |s| Ok(s.error.as_ptr()))
}

fn main() {}
