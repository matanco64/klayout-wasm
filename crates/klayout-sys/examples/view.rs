//! Phase 1, headless: KLayout's own view renders a layout, its own box tool
//! draws a shape from synthetic clicks, and undo removes it again. Frames are
//! written as PNG. Needs host filesystem access under Node:
//!
//!   cargo rustc -p klayout-sys --example view --target wasm32-unknown-emscripten \
//!     --release -- -C link-arg=-sNODERAWFS=1
//!   node target/wasm32-unknown-emscripten/release/examples/view.js <out-dir>

use klayout_sys::{Buttons, Mouse, View};
use std::fs::File;
use std::io::BufWriter;

const GDS: &[u8] = include_bytes!("../../../vendor/klayout/samples/lvs/ringo.gds");
const W: u32 = 800;
const H: u32 = 600;

fn save(view: &mut View, path: &str) {
    let frame = view.frame().expect("frame");
    let mut png = png::Encoder::new(BufWriter::new(File::create(path).unwrap()), frame.width, frame.height);
    png.set_color(png::ColorType::Rgba);
    png.write_header().unwrap().write_image_data(frame.rgba).unwrap();
    println!("wrote {path} ({}x{})", frame.width, frame.height);
}

fn click(view: &mut View, x: f64, y: f64) {
    view.mouse(Mouse::Move, x, y, Buttons::NONE).unwrap();
    view.mouse(Mouse::Press, x, y, Buttons::LEFT).unwrap();
    view.mouse(Mouse::Release, x, y, Buttons::LEFT).unwrap();
}

fn main() {
    let out = std::env::args().nth(1).unwrap_or_else(|| ".".into());
    let input = format!("{out}/ringo.gds");
    std::fs::write(&input, GDS).unwrap();

    let mut view = View::new(true).expect("view");
    view.resize(W, H).unwrap();
    view.load(&input).expect("load");
    println!("modes: {:?}", view.mode_names());
    let before = view.shape_count();
    save(&mut view, &format!("{out}/0-loaded.png"));

    let layer = (1..=100).find(|&l| view.set_current_layer(l, 0).unwrap()).expect("a drawable layer");
    println!("drawing on layer {layer}/0");

    view.switch_mode("box").unwrap();
    println!("mode: {}", view.mode_name());
    view.mouse(Mouse::Enter, 0.0, 0.0, Buttons::NONE).unwrap();
    click(&mut view, 300.0, 250.0);
    for step in 1..=10 {
        let t = f64::from(step) / 10.0;
        view.mouse(Mouse::Move, 300.0 + 200.0 * t, 250.0 + 100.0 * t, Buttons::NONE).unwrap();
    }
    click(&mut view, 500.0, 350.0);
    let after = view.shape_count();
    println!("shapes: {before} before, {after} after drawing");
    save(&mut view, &format!("{out}/1-box.png"));
    view.save(&format!("{out}/1-box.gds")).expect("save");

    view.undo().unwrap();
    println!("shapes after undo: {}", view.shape_count());
    view.redo().unwrap();
    println!("shapes after redo: {}", view.shape_count());

    assert_eq!(after, before + 1, "the box tool should have created exactly one shape");
}
