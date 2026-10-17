//! The browser module: one KLayout view behind one message-passing entry
//! point. JavaScript sends a JSON [`Request`], optionally with a binary
//! payload (a layout or a `.lyp` file), and reads back a JSON [`Response`]
//! and, for some replies, a binary payload (a frame, an exported layout).
//! The same messages can travel unchanged over `postMessage` to a worker.
//!
//! ```text
//! klw_call(json, json_len, payload, payload_len) -> response JSON (UTF-8, klw_response_len bytes)
//! klw_payload() / klw_payload_len()              -> the reply's payload, if the reply says so
//! ```
//!
//! Both returned buffers stay valid until the next `klw_call`.

mod protocol;

use klayout_sys::{Error, View, ViewState};
use protocol::{Event, PROTOCOL, Reply, Request, Response};
use std::cell::RefCell;

struct Session {
    view: View,
    seen: ViewState,
}

#[derive(Default)]
struct State {
    session: Option<Session>,
    response: Vec<u8>,
    payload: Payload,
}

/// The reply's binary payload. A frame stays in KLayout's buffer rather
/// than being copied.
#[derive(Default)]
enum Payload {
    #[default]
    None,
    Owned(Vec<u8>),
    Borrowed(*const u8, usize),
}

thread_local! {
    static STATE: RefCell<State> = RefCell::new(State::default());
}

/// Where uploaded files go in the Emscripten filesystem.
const LAYOUT_PATH: &str = "/layout.bin";
const LYP_PATH: &str = "/layers.lyp";

fn io(e: std::io::Error) -> Error {
    Error(e.to_string())
}

fn handle(state: &mut State, request: Request, input: &[u8]) -> Result<Reply, Error> {
    if let Request::Hello { protocol, editable } = request {
        if protocol != PROTOCOL {
            return Err(Error(format!("protocol {protocol} requested; this module speaks {PROTOCOL}")));
        }
        let mut view = View::new(editable)?;
        let seen = view.state()?;
        let modes = view.mode_names();
        state.session = Some(Session { view, seen });
        return Ok(Reply::Hello { protocol: PROTOCOL, klayout: klayout_sys::KLAYOUT_VERSION, modes });
    }
    let session = state.session.as_mut().ok_or_else(|| Error("send hello first".into()))?;
    let view = &mut session.view;
    Ok(match request {
        Request::Hello { .. } => unreachable!("handled above"),
        Request::Open => {
            std::fs::write(LAYOUT_PATH, input).map_err(io)?;
            view.load(LAYOUT_PATH)?;
            Reply::Opened { dbu: view.dbu()?, cells: view.cells()?, layers: view.layers()? }
        }
        Request::Export { format } => {
            state.payload = Payload::Owned(view.export(format)?);
            Reply::Bytes
        }
        Request::Resize { width, height } => {
            view.resize(width, height)?;
            Reply::Ok
        }
        Request::Frame => {
            let frame = view.frame()?;
            state.payload = Payload::Borrowed(frame.rgba.as_ptr(), frame.rgba.len());
            Reply::Frame { width: frame.width, height: frame.height }
        }
        Request::Mouse { event, x, y, buttons } => {
            view.mouse(event, x, y, buttons)?;
            Reply::Ok
        }
        Request::Wheel { delta, horizontal, x, y, buttons } => {
            view.wheel(delta, horizontal, x, y, buttons)?;
            Reply::Ok
        }
        Request::Key { key, buttons } => {
            view.key(key, buttons)?;
            Reply::Ok
        }
        Request::ZoomFit => {
            view.zoom_fit()?;
            Reply::Ok
        }
        Request::SwitchMode { mode } => {
            view.switch_mode(&mode)?;
            Reply::Ok
        }
        Request::SetCurrentLayer { layer } => Reply::CurrentLayer { found: view.set_current_layer(layer.layer, layer.datatype)? },
        Request::Undo => {
            view.undo()?;
            Reply::Ok
        }
        Request::Redo => {
            view.redo()?;
            Reply::Ok
        }
        Request::Command { command } => {
            view.command(command)?;
            Reply::Ok
        }
        Request::Cells => Reply::Cells { cells: view.cells()? },
        Request::Layers => Reply::Layers { layers: view.layers()? },
        Request::Shapes { cell, layer } => Reply::Shapes { shapes: view.shapes(&cell, layer)? },
        Request::Instances { cell } => Reply::Instances { instances: view.instances(&cell)? },
        Request::Selection => Reply::Selection { selection: view.selection()? },
        Request::Edit { description, edits } => {
            view.edit(&description, &edits)?;
            Reply::Ok
        }
        Request::LayerPanel => Reply::LayerPanel { entries: view.layer_panel()? },
        Request::SetLayerVisible { id, visible } => {
            view.set_layer_visible(id, visible)?;
            Reply::Ok
        }
        Request::LoadLayerProperties => {
            std::fs::write(LYP_PATH, input).map_err(io)?;
            view.load_layer_properties(LYP_PATH)?;
            Reply::Ok
        }
        Request::SaveLayerProperties => {
            view.save_layer_properties(LYP_PATH)?;
            state.payload = Payload::Owned(std::fs::read(LYP_PATH).map_err(io)?);
            Reply::Bytes
        }
    })
}

/// What changed since the previous call, however it changed: through this
/// request, or through KLayout's own tools reacting to input.
fn events(session: &mut Session, opened: bool) -> Result<Vec<Event>, Error> {
    let now = session.view.state()?;
    let seen = std::mem::replace(&mut session.seen, now.clone());
    let mut events = Vec::new();
    if opened || now.revision != seen.revision || now.history != seen.history {
        events.push(Event::LayoutChanged { history: now.history });
    }
    if now.selection != seen.selection {
        events.push(Event::SelectionChanged);
    }
    if opened || now.layer_panel != seen.layer_panel {
        events.push(Event::LayerPanelChanged);
    }
    if now.mode != seen.mode {
        events.push(Event::ModeChanged { mode: now.mode });
    }
    Ok(events)
}

fn call(state: &mut State, request: &[u8], input: &[u8]) -> Response {
    state.payload = Payload::None;
    let reply = match serde_json::from_slice::<Request>(request) {
        Err(e) => Err(Error(format!("bad request: {e}"))),
        Ok(request) => {
            let opened = matches!(request, Request::Open);
            handle(state, request, input).map(|reply| (reply, opened))
        }
    };
    let (reply, opened) = reply.unwrap_or_else(|e| (Reply::Error { message: e.0 }, false));
    let events = match state.session.as_mut() {
        None => Vec::new(),
        Some(session) => events(session, opened).unwrap_or_default(),
    };
    Response { reply, events }
}

/// Handles one request; see the module documentation.
///
/// # Safety
/// `request` must point to `request_len` readable bytes and `payload` to
/// `payload_len` (or be null with length 0).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn klw_call(request: *const u8, request_len: usize, payload: *const u8, payload_len: usize) -> *const u8 {
    // SAFETY: guaranteed by the caller.
    let request = unsafe { bytes(request, request_len) };
    let input = unsafe { bytes(payload, payload_len) };
    STATE.with_borrow_mut(|state| {
        let response = call(state, request, input);
        state.response = serde_json::to_vec(&response).expect("responses always serialize");
        state.response.as_ptr()
    })
}

unsafe fn bytes<'a>(p: *const u8, len: usize) -> &'a [u8] {
    if len == 0 { &[] } else { unsafe { std::slice::from_raw_parts(p, len) } }
}

#[unsafe(no_mangle)]
pub extern "C" fn klw_response_len() -> usize {
    STATE.with_borrow(|s| s.response.len())
}

#[unsafe(no_mangle)]
pub extern "C" fn klw_payload() -> *const u8 {
    STATE.with_borrow(|s| match &s.payload {
        Payload::None => std::ptr::null(),
        Payload::Owned(v) => v.as_ptr(),
        Payload::Borrowed(p, _) => *p,
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn klw_payload_len() -> usize {
    STATE.with_borrow(|s| match &s.payload {
        Payload::None => 0,
        Payload::Owned(v) => v.len(),
        Payload::Borrowed(_, n) => *n,
    })
}

fn main() {}
