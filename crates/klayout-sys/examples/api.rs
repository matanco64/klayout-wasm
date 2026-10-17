//! Phase 2, headless: the typed API end to end against ringo.gds. Every
//! check is an assertion; the run fails on the first mismatch.
//!
//!   cargo build -p klayout-sys --example api --target wasm32-unknown-emscripten --release
//!   node target/wasm32-unknown-emscripten/release/examples/api.js

use klayout_sys::*;

const GDS: &[u8] = include_bytes!("../../../vendor/klayout/samples/lvs/ringo.gds");

fn total_shapes(view: &mut View) -> usize {
    let cells = view.cells().unwrap();
    let layers = view.layers().unwrap();
    let mut n = 0;
    for cell in &cells {
        for layer in &layers {
            n += view.shapes(&cell.name, layer.spec).unwrap().len();
        }
    }
    n
}

fn main() {
    std::fs::write("/ringo.gds", GDS).unwrap();
    let mut view = View::new(true).unwrap();
    view.resize(800, 600).unwrap();
    view.load("/ringo.gds").unwrap();

    // Reading.
    let dbu = view.dbu().unwrap();
    let cells = view.cells().unwrap();
    let layers = view.layers().unwrap();
    let top = cells.iter().find(|c| c.top).expect("a top cell").name.clone();
    println!("dbu {dbu}, {} cells, {} layers, top {top}", cells.len(), layers.len());
    assert_eq!((dbu, cells.len(), layers.len()), (0.001, 9, 12));
    let decoded = total_shapes(&mut view);
    println!("decoded {decoded} shapes over every cell and layer (view counts {})", view.shape_count());
    assert_eq!(decoded, view.shape_count());
    let instances = view.instances(&top).unwrap();
    println!("{top} places {} instances, first {:?}", instances.len(), instances[0]);

    // One transaction writing every shape kind, a new cell and a new layer.
    let state0 = view.state().unwrap();
    let layer = LayerSpec { layer: 200, datatype: 0 };
    let cell = "API_TEST".to_string();
    let shapes = vec![
        Shape::Box(Rect { left: 0, bottom: 0, right: 1000, top: 500 }),
        Shape::Polygon(Polygon {
            hull: vec![Point { x: 0, y: 0 }, Point { x: 0, y: 3000 }, Point { x: 3000, y: 3000 }, Point { x: 3000, y: 0 }],
            // KLayout's normal form: holes counterclockwise.
            holes: vec![vec![Point { x: 1000, y: 1000 }, Point { x: 2000, y: 1000 }, Point { x: 2000, y: 2000 }, Point { x: 1000, y: 2000 }]],
        }),
        Shape::Path(Path {
            points: vec![Point { x: 0, y: 5000 }, Point { x: 4000, y: 5000 }, Point { x: 4000, y: 9000 }],
            width: 200,
            begin_extension: 100,
            end_extension: 0,
            round: false,
        }),
        Shape::Text(Text {
            string: "VDD".into(),
            position: Point { x: 500, y: 250 },
            orientation: Orientation::M90,
            size: 300,
            font: None,
            halign: Some(HAlign::Center),
            valign: Some(VAlign::Bottom),
        }),
    ];
    let mut edits = vec![Edit::CreateCell { name: cell.clone() }];
    edits.extend(shapes.iter().map(|s| Edit::InsertShape { cell: cell.clone(), layer, shape: s.clone() }));
    let rotated = Instance {
        cell: cell.clone(),
        transform: Transform { displacement: Vector { x: 50_000, y: 0 }, angle: 30.0, mirror: true, magnification: 2.0 },
        array: None,
    };
    let arrayed = Instance {
        cell: cell.clone(),
        transform: Transform { displacement: Vector { x: 0, y: -20_000 }, ..Transform::default() },
        array: Some(Array { a: Vector { x: 5000, y: 0 }, b: Vector { x: 0, y: 12_000 }, na: 4, nb: 2 }),
    };
    edits.push(Edit::InsertInstance { cell: top.clone(), instance: rotated.clone() });
    edits.push(Edit::InsertInstance { cell: top.clone(), instance: arrayed.clone() });
    view.edit("API test", &edits).unwrap();

    let back = view.shapes(&cell, layer).unwrap();
    assert_eq!(back.len(), shapes.len());
    for s in &shapes {
        assert!(back.contains(s), "shape did not round-trip: {s:?}\nread back: {back:?}");
    }
    println!("all {} shape kinds round-trip exactly", shapes.len());
    assert!(view.layers().unwrap().iter().any(|l| l.spec == layer), "layer 200/0 was created");
    let placed = view.instances(&top).unwrap();
    let found = placed.iter().find(|i| i.cell == cell && i.array.is_none()).expect("rotated instance");
    assert!((found.transform.angle - 30.0).abs() < 1e-9 && (found.transform.magnification - 2.0).abs() < 1e-12);
    assert!(found.transform.mirror && found.transform.displacement == rotated.transform.displacement);
    assert!(placed.contains(&arrayed));
    println!("instances round-trip: {found:?}");

    let state1 = view.state().unwrap();
    assert_ne!(state0.revision, state1.revision);
    assert_eq!(state1.history.undo.as_deref(), Some("API test"));
    println!("history after edit: {:?}", state1.history);

    // All or nothing: the second edit fails, so the first is rolled back.
    let missing = Shape::Box(Rect { left: 1, bottom: 1, right: 2, top: 2 });
    let err = view
        .edit("bad", &[
            Edit::InsertShape { cell: cell.clone(), layer, shape: missing.clone() },
            Edit::EraseShape { cell: cell.clone(), layer, shape: missing.clone() },
            Edit::EraseShape { cell: cell.clone(), layer, shape: missing.clone() },
        ])
        .unwrap_err();
    println!("failing edit rejected: {err}");
    assert_eq!(view.shapes(&cell, layer).unwrap().len(), shapes.len());
    assert_eq!(view.state().unwrap().history, state1.history, "a failed edit leaves no undo step");

    // Erase by value, rename, undo, redo.
    view.edit("erase", &[Edit::EraseShape { cell: cell.clone(), layer, shape: shapes[0].clone() }]).unwrap();
    assert_eq!(view.shapes(&cell, layer).unwrap().len(), shapes.len() - 1);
    view.edit("rename", &[Edit::RenameCell { name: cell.clone(), new_name: "RENAMED".into() }]).unwrap();
    assert!(view.cells().unwrap().iter().any(|c| c.name == "RENAMED"));
    view.undo().unwrap();
    view.undo().unwrap();
    assert_eq!(view.shapes(&cell, layer).unwrap().len(), shapes.len());
    view.undo().unwrap();
    assert!(!view.cells().unwrap().iter().any(|c| c.name == cell), "undo removes the whole API test edit");
    view.redo().unwrap();
    assert_eq!(view.shapes(&cell, layer).unwrap().len(), shapes.len());
    println!("erase, rename, undo x3, redo: ok");

    // Export and read back with a separate layout.
    for format in [Format::Gds2, Format::Oasis] {
        let bytes = view.export(format).unwrap();
        let mut layout = Layout::new().unwrap();
        layout.read(&bytes).unwrap();
        println!("{format:?}: {} bytes, {} cells, {} shapes", bytes.len(), layout.cell_count(), layout.shape_count());
        assert_eq!(layout.cell_count(), view.cells().unwrap().len());
        // KLayout's OASIS writer compacts shapes into repetitions, so shape
        // counts read back differently; GDS is written one shape per record.
        if format == Format::Gds2 {
            assert_eq!(layout.shape_count(), view.shape_count());
        }
    }

    // Edit menu commands. The view shows every hierarchy level, so select
    // all reaches into child cells, once per instance path, as on desktop.
    let before = view.shape_count();
    view.command(Command::SelectAll).unwrap();
    let selected = view.selection().unwrap();
    let selected_shapes = selected.iter().filter(|s| matches!(s, Selected::Shape { .. })).count();
    println!("select all: {} objects, {selected_shapes} shapes", selected.len());
    assert!(!selected.is_empty());
    let serial = view.state().unwrap().selection;
    view.command(Command::Copy).unwrap();
    assert!(view.selection().unwrap().is_empty(), "copy clears the selection, as in KLayout");
    assert_ne!(view.state().unwrap().selection, serial, "selection changes are noticed");
    // Paste puts one flattened copy per selected object into the current cell.
    view.command(Command::Paste).unwrap();
    println!("copy + paste: {before} -> {} shapes", view.shape_count());
    assert_eq!(view.shape_count(), before + selected_shapes);
    view.undo().unwrap();
    assert_eq!(view.shape_count(), before);
    // Every layer is visible, so select all + delete removes every shape.
    view.command(Command::SelectAll).unwrap();
    view.command(Command::Delete).unwrap();
    println!("select all + delete: {before} -> {} shapes", view.shape_count());
    assert_eq!(view.shape_count(), 0);
    view.undo().unwrap();
    assert_eq!(view.shape_count(), before);
    view.command(Command::Cancel).unwrap();
    assert_eq!(view.state().unwrap().mode, "select");

    // Layer panel and .lyp.
    let panel = view.layer_panel().unwrap();
    println!("layer panel: {} entries, first {:?}", panel.len(), panel[0]);
    assert!(panel.iter().any(|e| e.layer == Some(layer)), "the new layer shows in the panel");
    let entry = panel.iter().find(|e| e.layer.is_some()).unwrap().clone();
    view.set_layer_visible(entry.id, false).unwrap();
    assert!(!view.layer_panel().unwrap().iter().find(|e| e.id == entry.id).unwrap().visible);
    view.save_layer_properties("/layers.lyp").unwrap();
    view.set_layer_visible(entry.id, true).unwrap();
    view.load_layer_properties("/layers.lyp").unwrap();
    let reloaded = view.layer_panel().unwrap();
    assert!(!reloaded.iter().find(|e| e.source == entry.source).unwrap().visible, ".lyp keeps the hidden layer");
    println!(".lyp: {} bytes, round-trips visibility", std::fs::metadata("/layers.lyp").unwrap().len());

    view.frame().unwrap();
    println!("ok");
}
