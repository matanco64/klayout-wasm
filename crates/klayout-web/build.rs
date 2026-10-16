//! Emscripten link settings for the browser module. They apply to this
//! crate's binary only, so the -sys crate stays usable from Node examples.

const EXPORTS: &[&str] = &[
    "klw_new", "klw_load", "klw_resize", "klw_zoom_fit", "klw_modes", "klw_mode",
    "klw_switch_mode", "klw_mouse", "klw_wheel", "klw_key", "klw_frame",
    "klw_frame_width", "klw_frame_height", "klw_set_current_layer", "klw_undo",
    "klw_redo", "klw_shape_count", "klw_error", "malloc", "free",
];

fn main() {
    let exports: Vec<String> = EXPORTS.iter().map(|e| format!("_{e}")).collect();
    for arg in [
        format!("-sEXPORTED_FUNCTIONS={}", exports.join(",")),
        "-sEXPORTED_RUNTIME_METHODS=HEAPU8,UTF8ToString,stringToNewUTF8".into(),
        "-sMODULARIZE=1".into(),
        "-sEXPORT_ES6=1".into(),
        "-sEXPORT_NAME=createKlayout".into(),
        "-sENVIRONMENT=web,worker".into(),
        "-sINVOKE_RUN=0".into(),
    ] {
        println!("cargo:rustc-link-arg-bins={arg}");
    }
    println!("cargo:rerun-if-changed=build.rs");
}
