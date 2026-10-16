//! Phase 0: KLayout's database, compiled to WebAssembly, reads a real GDS
//! and an OASIS file. Run under Node:
//!
//!   cargo build --example smoke --target wasm32-unknown-emscripten --release
//!   node target/wasm32-unknown-emscripten/release/examples/smoke.js

use klayout_sys::Layout;

const GDS: &[u8] = include_bytes!("../../../vendor/klayout/samples/lvs/ringo.gds");
const OAS: &[u8] = include_bytes!("../../../vendor/klayout/testdata/drc/drcFullTest_1.oas");

fn main() {
    for (name, data) in [("ringo.gds", GDS), ("drcFullTest_1.oas", OAS)] {
        let mut layout = Layout::new().expect("new layout");
        layout.read(data).unwrap_or_else(|e| panic!("{name}: {e}"));
        println!(
            "{name}: {} bytes, dbu {}, {} cells ({} top), {} layers, {} shapes",
            data.len(),
            layout.dbu(),
            layout.cell_count(),
            layout.top_cell_count(),
            layout.layer_count(),
            layout.shape_count(),
        );
    }

    let mut bad = Layout::new().unwrap();
    match bad.read(b"definitely not a layout") {
        Err(e) => println!("garbage input rejected: {e}"),
        Ok(()) => panic!("garbage input was accepted"),
    }
}
