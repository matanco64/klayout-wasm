//! Emscripten link settings for the browser module. They apply to this
//! crate's binary only, so the -sys crate stays usable from Node examples.

const EXPORTS: &[&str] = &["klw_call", "klw_response_len", "klw_payload", "klw_payload_len", "malloc", "free"];

fn main() {
    let exports: Vec<String> = EXPORTS.iter().map(|e| format!("_{e}")).collect();
    for arg in [
        format!("-sEXPORTED_FUNCTIONS={}", exports.join(",")),
        "-sEXPORTED_RUNTIME_METHODS=HEAPU8".into(),
        "-sMODULARIZE=1".into(),
        "-sEXPORT_ES6=1".into(),
        "-sEXPORT_NAME=createKlayout".into(),
        // node: the protocol test runs the same module headless.
        "-sENVIRONMENT=web,worker,node".into(),
        "-sINVOKE_RUN=0".into(),
    ] {
        println!("cargo:rustc-link-arg-bins={arg}");
    }
    println!("cargo:rerun-if-changed=build.rs");
}
