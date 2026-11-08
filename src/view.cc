// The View class: KLayout's Qt-free lay::LayoutView, the same one KLayout's
// `klayout.lay` Python module uses headless, in synchronous mode (there are
// no threads), driven by synthetic input and read back as RGBA frames.

#include "klayout_wasm.h"

#include "dbClipboard.h"
#include "dbReader.h"
#include "dbSaveLayoutOptions.h"
#include "dbStatic.h"
#include "dbWriter.h"
#include "edtService.h"
#include "layLayerProperties.h"
#include "layLayoutCanvas.h"
#include "tlColor.h"
#include "tlPixelBuffer.h"
#include "tlStream.h"

#include <cmath>
#include <fstream>
#include <iterator>

namespace lay
{
// The canvas's device pixel ratio, patched in by tools/klayout_sources.py.
extern double wasm_device_pixel_ratio;
}

namespace kl
{

// Where files given as bytes go in Emscripten's in-memory filesystem.
static const char *const layout_path = "/layout.bin";
static const char *const lyp_path = "/layers.lyp";

static void write_file (const char *path, const std::vector<uint8_t> &data)
{
  std::ofstream f (path, std::ios::binary | std::ios::trunc);
  f.write (reinterpret_cast<const char *> (data.data ()), std::streamsize (data.size ()));
  if (! f) {
    throw tl::Exception (std::string ("cannot write ") + path);
  }
}

static std::vector<uint8_t> read_file (const char *path)
{
  std::ifstream f (path, std::ios::binary);
  if (! f) {
    throw tl::Exception (std::string ("cannot read ") + path);
  }
  return std::vector<uint8_t> (std::istreambuf_iterator<char> (f), std::istreambuf_iterator<char> ());
}

View::View (bool editable)
{
  guarded ([&] {
    // Layouts KLayout creates internally (the clipboard's, for one) take
    // their mode from this global, which desktop KLayout sets at startup.
    db::set_default_editable_mode (editable);
    m_view.reset (new lay::LayoutView (&m_manager, editable, nullptr));
    m_view->set_synchronous (true);
    m_view->selection_changed_event.add (this, &View::on_selection_changed);
    m_view->layer_list_changed_event.add (this, &View::on_layer_list_changed);
    m_seen = state ();
  });
}

View::~View ()
{
  // The view goes before the manager it records undo steps in.
  m_view.reset ();
}

db::Layout &View::layout ()
{
  if (m_view->cellviews () == 0) {
    throw tl::Exception ("no layout loaded");
  }
  return m_view->cellview (0)->layout ();
}

db::Cell &View::cell (const std::string &name)
{
  db::Layout &l = layout ();
  std::pair<bool, db::cell_index_type> ci = l.cell_by_name (name.c_str ());
  if (! ci.first) {
    throw tl::Exception ("no cell named " + name);
  }
  return l.cell (ci.second);
}

// The layer index for GDS layer/datatype, or -1 if the layout has none.
int View::find_layer (const db::LayerProperties &lp)
{
  db::Layout &l = layout ();
  for (auto i = l.begin_layers (); i != l.end_layers (); ++i) {
    if ((*i).second->layer == lp.layer && (*i).second->datatype == lp.datatype) {
      return int ((*i).first);
    }
  }
  return -1;
}

db::Shapes &View::shapes_for_edit (const std::string &name, const db::LayerProperties &lp, bool create, bool &created)
{
  db::Cell &c = cell (name);
  int li = find_layer (lp);
  if (li < 0) {
    if (! create) {
      throw tl::Exception ("no layer " + lp.to_string ());
    }
    li = int (layout ().insert_layer (lp));
    created = true;
  }
  return c.shapes ((unsigned int) li);
}

void View::open (const Bytes &bytes)
{
  guarded ([&] {
    write_file (layout_path, to_bytes (bytes));
    // Markers are in the old layout's coordinates.
    m_markers.clear ();
    m_view->load_layout (layout_path, false);
    m_view->max_hier ();
    m_view->zoom_fit ();
    m_opened = true;
  });
}

Bytes View::export_layout (const Format &format)
{
  return guarded ([&] {
    db::SaveLayoutOptions options;
    std::string name = to_string (format, "format");
    if (name == "gds2") {
      options.set_format ("GDS2");
    } else if (name == "oasis") {
      options.set_format ("OASIS");
    } else {
      throw tl::Exception ("format: unknown value \"" + name + "\"");
    }
    tl::OutputMemoryStream memory;
    {
      tl::OutputStream stream (memory);
      db::Writer writer (options);
      writer.write (layout (), stream);
    }
    return Bytes (from_bytes (reinterpret_cast<const uint8_t *> (memory.data ()), memory.size ()));
  });
}

void View::resize (unsigned int width, unsigned int height)
{
  guarded ([&] {
    m_view->resize (width, height);
    m_width = width;
    m_height = height;
  });
}

Frame View::frame ()
{
  return guarded ([&] {
    // timer() runs the view's timer before the deferred methods, so a redraw
    // that an edit schedules is only drawn on the following pass. In
    // synchronous mode a pass with nothing to do is nearly free.
    for (int pass = 0; pass < 3; ++pass) {
      m_view->timer ();
    }
    tl::PixelBuffer pixels = m_view->canvas ()->screenshot ();
    unsigned int width = pixels.width (), height = pixels.height ();
    m_rgba.resize (size_t (width) * height * 4);
    uint8_t *out = m_rgba.data ();
    for (unsigned int row = 0; row < height; ++row) {
      const tl::color_t *line = pixels.scan_line (row);
      for (unsigned int col = 0; col < width; ++col) {
        tl::color_t argb = line[col];
        *out++ = uint8_t (argb >> 16);
        *out++ = uint8_t (argb >> 8);
        *out++ = uint8_t (argb);
        *out++ = 0xff;
      }
    }
    val frame = val::object ();
    frame.set ("width", width);
    frame.set ("height", height);
    // A view into module memory, valid until the next call: no copy per frame.
    frame.set ("rgba", val (emscripten::typed_memory_view (m_rgba.size (), m_rgba.data ())));
    return Frame (frame);
  });
}

void View::mouse (const MouseAction &action, double x, double y, const Buttons &buttons)
{
  guarded ([&] {
    lay::LayoutCanvas *c = m_view->canvas ();
    db::DPoint p (x, y);
    unsigned int b = to_buttons (buttons);
    std::string kind = to_string (action, "action");
    if (kind == "move") {
      c->send_mouse_move_event (p, b);
    } else if (kind == "press") {
      c->send_mouse_press_event (p, b);
    } else if (kind == "release") {
      c->send_mouse_release_event (p, b);
    } else if (kind == "doubleClick") {
      c->send_mouse_double_clicked_event (p, b);
    } else if (kind == "enter") {
      c->send_enter_event ();
    } else if (kind == "leave") {
      c->send_leave_event ();
    } else {
      throw tl::Exception ("action: unknown value \"" + kind + "\"");
    }
  });
}

void View::wheel (int delta, bool horizontal, double x, double y, const Buttons &buttons)
{
  guarded ([&] { m_view->canvas ()->send_wheel_event (delta, horizontal, db::DPoint (x, y), to_buttons (buttons)); });
}

void View::key (const Key &key, const Buttons &buttons)
{
  guarded ([&] { m_view->canvas ()->send_key_press_event (to_qt_key (key), to_buttons (buttons)); });
}

void View::zoom_fit ()
{
  guarded ([&] { m_view->zoom_fit (); });
}

void View::set_device_pixel_ratio (double ratio)
{
  guarded ([&] {
    if (! std::isfinite (ratio) || ratio < 0.25 || ratio > 8.0) {
      throw tl::Exception ("ratio: expected a number from 0.25 to 8");
    }
    lay::wasm_device_pixel_ratio = ratio;
    // The canvas sizes its pixel buffer and line widths from the ratio when
    // it is resized; resize to the same CSS size to apply it.
    m_view->resize (m_width, m_height);
  });
}

// Each field is optional; the ones given are set through KLayout's own
// configuration keys, as its setup dialogs do.
void View::configure (const ViewConfig &config)
{
  guarded ([&] {
    static const char *const angles[] = { "any", "diagonal", "ortho" };
    static const char *const keys[] = { "grid", "editGrid", "snapToObjects", "gridVisible", "background", "connectAngle" };
    if (config.isNull () || config.typeOf ().as<std::string> () != "object") {
      throw tl::Exception ("config: expected an object");
    }
    // A misspelt key would otherwise be ignored silently.
    val names = val::global ("Object").call<val> ("keys", config);
    for (unsigned int i = 0; i < names["length"].as<unsigned int> (); ++i) {
      std::string k = names[i].as<std::string> ();
      bool known = false;
      for (const char *n : keys) {
        known = known || k == n;
      }
      if (! known) {
        throw tl::Exception ("config: unknown field \"" + k + "\"");
      }
    }
    auto micron = [] (const val &v, const char *what) {
      double d = to_f64 (v, what);
      if (! (d > 0.0)) {
        throw tl::Exception (std::string (what) + ": expected a positive number of microns");
      }
      return tl::to_string (d);
    };
    // Read and check everything before setting anything.
    std::vector<std::pair<std::string, std::string>> settings;
    if (has (config, "grid")) {
      settings.emplace_back ("grid-micron", micron (config["grid"], "grid"));
    }
    if (has (config, "editGrid")) {
      val g = config["editGrid"];
      // null: snap to the display grid.
      settings.emplace_back ("edit-grid", g.isNull () ? std::string () : micron (g, "editGrid"));
    }
    if (has (config, "snapToObjects")) {
      settings.emplace_back ("edit-snap-to-objects", to_bool (config["snapToObjects"], "snapToObjects") ? "true" : "false");
    }
    if (has (config, "gridVisible")) {
      settings.emplace_back ("grid-visible", to_bool (config["gridVisible"], "gridVisible") ? "true" : "false");
    }
    if (has (config, "background")) {
      std::string c = to_string (config["background"], "background");
      if (c.size () != 7 || c[0] != '#' || c.find_first_not_of ("0123456789abcdefABCDEF", 1) != std::string::npos) {
        throw tl::Exception ("background: expected \"#rrggbb\"");
      }
      settings.emplace_back ("background-color", c);
    }
    if (has (config, "connectAngle")) {
      std::string a = to_string (config["connectAngle"], "connectAngle");
      bool known = false;
      for (const char *n : angles) {
        known = known || a == n;
      }
      if (! known) {
        throw tl::Exception ("connectAngle: unknown value \"" + a + "\"");
      }
      settings.emplace_back ("edit-connect-angle-mode", a);
    }
    for (const auto &s : settings) {
      m_view->config_set (s.first, s.second);
    }
    m_view->config_end ();
  });
}

Rect View::viewport ()
{
  return guarded ([&] {
    db::DBox b = m_view->box ();
    return Rect (from_box (db::Box (b.transformed (db::VCplxTrans (1.0 / layout ().dbu ())))));
  });
}

void View::zoom_box (const Rect &box)
{
  guarded ([&] {
    db::Box b = to_box (box);
    if (b.empty () || b.width () == 0 || b.height () == 0) {
      throw tl::Exception ("box: expected a box with a width and a height");
    }
    m_view->zoom_box (db::DBox (b.transformed (db::CplxTrans (layout ().dbu ()))));
  });
}

// The layout point under CSS pixel (x, y), in DBU.
Point View::point_at (double x, double y)
{
  return guarded ([&] {
    db::DPoint um = m_view->canvas ()->pixel_to_um (db::DPoint (x, y));
    db::Point p = db::VCplxTrans (1.0 / layout ().dbu ()) * um;
    val o = val::object ();
    o.set ("x", p.x ());
    o.set ("y", p.y ());
    return Point (o);
  });
}

void View::show_cell (const std::string &name)
{
  guarded ([&] {
    db::Cell &c = cell (name);
    m_view->select_cell_fit (c.cell_index (), 0);
  });
}

OptionalString View::current_cell ()
{
  return guarded ([&] {
    if (m_view->cellviews () == 0 || ! m_view->cellview (0).is_valid ()) {
      return OptionalString (val::null ());
    }
    const lay::CellView &cv = m_view->cellview (0);
    return OptionalString (val (std::string (cv->layout ().cell_name (cv.cell_index ()))));
  });
}

StringList View::modes ()
{
  return guarded ([&] {
    val a = val::array ();
    for (const auto &name : m_view->mode_names ()) {
      a.call<void> ("push", name);
    }
    return StringList (a);
  });
}

std::string View::mode ()
{
  return guarded ([&] { return m_view->mode_name (); });
}

void View::switch_mode (const std::string &name)
{
  guarded ([&] { m_view->switch_mode (name); });
}

bool View::set_current_layer (const LayerSpec &layer)
{
  return guarded ([&] { return m_view->set_current_layer (0, to_layer (layer)); });
}

void View::undo ()
{
  guarded ([&] { m_manager.undo (); });
}

void View::redo ()
{
  guarded ([&] { m_manager.redo (); });
}

History View::history ()
{
  return guarded ([&] {
    std::pair<bool, std::string> undo = m_manager.available_undo ();
    std::pair<bool, std::string> redo = m_manager.available_redo ();
    val h = val::object ();
    h.set ("undo", undo.first ? val (undo.second) : val::null ());
    h.set ("redo", redo.first ? val (redo.second) : val::null ());
    return History (h);
  });
}

// Desktop KLayout's Edit menu, with the same follow-up calls as
// layLayoutViewFunctions.cc. Each makes its own undo step.
void View::command (const Command &command)
{
  guarded ([&] {
    lay::LayoutView &view = *m_view;
    std::string name = to_string (command, "command");
    if (name == "delete") {
      view.del ();
      view.cancel ();
      view.clear_selection ();
    } else if (name == "cut") {
      view.cut ();
      view.cancel ();
      view.clear_selection ();
    } else if (name == "copy") {
      view.copy ();
      view.clear_selection ();
    } else if (name == "paste" || name == "pasteMove") {
      if (! db::Clipboard::instance ().empty ()) {
        view.cancel_edits ();
        if (name == "paste") {
          view.paste ();
        } else {
          view.paste_interactive ();
        }
      }
    } else if (name == "selectAll") {
      view.select_all ();
    } else if (name == "unselectAll") {
      view.select (db::DBox (), lay::Editable::Reset);
    } else if (name == "cancel") {
      view.cancel_esc ();
    } else {
      throw tl::Exception ("command: unknown value \"" + name + "\"");
    }
  });
}

double View::dbu ()
{
  return guarded ([&] { return layout ().dbu (); });
}

CellList View::cells ()
{
  return guarded ([&] {
    db::Layout &l = layout ();
    val a = val::array ();
    for (auto c = l.begin (); c != l.end (); ++c) {
      val o = val::object ();
      o.set ("name", std::string (l.cell_name (c->cell_index ())));
      o.set ("top", c->is_top ());
      o.set ("bbox", c->bbox ().empty () ? val::null () : from_box (c->bbox ()));
      a.call<void> ("push", o);
    }
    return CellList (a);
  });
}

LayerList View::layers ()
{
  return guarded ([&] {
    db::Layout &l = layout ();
    val a = val::array ();
    for (auto i = l.begin_layers (); i != l.end_layers (); ++i) {
      val o = from_layer (*(*i).second);
      const std::string &name = (*i).second->name;
      o.set ("name", name.empty () ? val::null () : val (name));
      a.call<void> ("push", o);
    }
    return LayerList (a);
  });
}

ShapeList View::shapes (const std::string &name, const LayerSpec &layer)
{
  return guarded ([&] {
    db::Cell &c = cell (name);
    int li = find_layer (to_layer (layer));
    val a = val::array ();
    if (li >= 0) {
      for (auto s = c.shapes ((unsigned int) li).begin (db::ShapeIterator::All); ! s.at_end (); ++s) {
        val shape = from_shape (*s);
        if (! shape.isUndefined ()) {
          a.call<void> ("push", shape);
        }
      }
    }
    return ShapeList (a);
  });
}

InstanceList View::instances (const std::string &name)
{
  return guarded ([&] {
    db::Cell &c = cell (name);
    val a = val::array ();
    for (db::Cell::const_iterator i = c.begin (); ! i.at_end (); ++i) {
      a.call<void> ("push", from_instance (layout (), i->cell_inst ()));
    }
    return InstanceList (a);
  });
}

SelectionList View::selection ()
{
  return guarded ([&] {
    db::Layout &l = layout ();
    val a = val::array ();
    for (const lay::ObjectInstPath &p : edt::object_selection (m_view.get ())) {
      if (p.cv_index () != 0) {
        continue;
      }
      val o = val::object ();
      if (p.is_cell_inst ()) {
        const db::Instance &inst = p.back ().inst_ptr;
        o.set ("kind", "instance");
        o.set ("cell", std::string (l.cell_name (inst.instances ()->cell ()->cell_index ())));
        o.set ("instance", from_instance (l, inst.cell_inst ()));
      } else {
        val shape = from_shape (p.shape ());
        if (shape.isUndefined ()) {
          continue;
        }
        o.set ("kind", "shape");
        o.set ("cell", std::string (l.cell_name (p.cell_index_tot ())));
        o.set ("layer", from_layer (l.get_properties (p.layer ())));
        o.set ("shape", shape);
      }
      a.call<void> ("push", o);
    }
    return SelectionList (a);
  });
}

static db::Instance find_instance (db::Cell &cell, const db::CellInstArray &wanted, const db::Layout &layout)
{
  for (db::Cell::const_iterator i = cell.begin (); ! i.at_end (); ++i) {
    if (i->cell_inst () == wanted) {
      return *i;
    }
  }
  throw tl::Exception (std::string ("no such instance in ") + layout.cell_name (cell.cell_index ()));
}

// One undo step: all edits apply, or on error none.
void View::edit (const std::string &description, const EditList &edits)
{
  guarded ([&] {
    if (! edits.isArray ()) {
      throw tl::Exception ("edits: expected an array");
    }
    db::Layout &l = layout ();
    bool created_layer = false;
    unsigned int n = edits["length"].as<unsigned int> ();
    // The selection refers to shapes by position in their containers; an
    // edit may erase or move them, so it is dropped first, as KLayout's own
    // editing functions do.
    m_view->cancel_edits ();
    m_view->clear_selection ();
    m_manager.transaction (description);
    try {
      for (unsigned int i = 0; i < n; ++i) {
        val e = edits[i];
        std::string op = to_string (field (e, "op"), "op");
        if (op == "insertShape") {
          insert_shape (field (e, "shape"), shapes_for_edit (to_string (field (e, "cell"), "cell"), to_layer (field (e, "layer")), true, created_layer));
        } else if (op == "eraseShape") {
          std::string name = to_string (field (e, "cell"), "cell");
          db::LayerProperties lp = to_layer (field (e, "layer"));
          db::Shapes &target = shapes_for_edit (name, lp, false, created_layer);
          db::Shapes probe (true);
          db::Shape found = target.find (insert_shape (field (e, "shape"), probe));
          if (found.is_null ()) {
            throw tl::Exception ("no such shape in " + name + " on " + lp.to_string ());
          }
          target.erase_shape (found);
        } else if (op == "insertInstance") {
          cell (to_string (field (e, "cell"), "cell")).insert (to_instance (field (e, "instance"), l));
        } else if (op == "eraseInstance") {
          db::Cell &c = cell (to_string (field (e, "cell"), "cell"));
          c.erase (find_instance (c, to_instance (field (e, "instance"), l), l));
        } else if (op == "createCell") {
          std::string name = to_string (field (e, "name"), "name");
          if (l.cell_by_name (name.c_str ()).first) {
            throw tl::Exception ("a cell named " + name + " exists already");
          }
          l.add_cell (name.c_str ());
        } else if (op == "deleteCell") {
          l.delete_cell (cell (to_string (field (e, "name"), "name")).cell_index ());
        } else if (op == "renameCell") {
          db::cell_index_type ci = cell (to_string (field (e, "name"), "name")).cell_index ();
          std::string name = to_string (field (e, "newName"), "newName");
          if (l.cell_by_name (name.c_str ()).first) {
            throw tl::Exception ("a cell named " + name + " exists already");
          }
          l.rename_cell (ci, name.c_str ());
        } else {
          throw tl::Exception ("op: unknown value \"" + op + "\"");
        }
      }
      // Inside the transaction: the view clears the undo history on layer
      // panel changes made outside one.
      if (created_layer) {
        m_view->add_missing_layers ();
      }
    } catch (...) {
      m_manager.cancel ();
      throw;
    }
    m_manager.commit ();
  });
}

// Ids are KLayout's iterator positions, valid until the panel changes.
LayerEntryList View::layer_panel ()
{
  return guarded ([&] {
    val a = val::array ();
    for (lay::LayerPropertiesConstIterator i = m_view->begin_layers (); ! i.at_end (); ++i) {
      val o = val::object ();
      o.set ("id", unsigned (i.uint ()));
      lay::LayerPropertiesConstIterator parent = i.parent ();
      o.set ("parent", parent.is_null () ? val::null () : val (unsigned (parent.uint ())));
      o.set ("group", i->has_children ());
      o.set ("label", i->display_string (m_view.get (), true));
      o.set ("source", i->source_string (true));
      int li = i->layer_index ();
      int cvi = i->cellview_index ();
      bool has_layer = li >= 0 && cvi >= 0 && cvi < int (m_view->cellviews ()) &&
                       m_view->cellview (cvi)->layout ().is_valid_layer ((unsigned int) li);
      // The layer the entry draws: the layout's, or else the one its source
      // names, so a technology's layers can be drawn on before the layout has
      // them. Null for wildcard and group entries.
      const lay::ParsedLayerSource &src = i->source (true);
      if (has_layer) {
        o.set ("layer", from_layer (m_view->cellview (cvi)->layout ().get_properties ((unsigned int) li)));
      } else if (! i->has_children () && src.layer () >= 0 && src.datatype () >= 0) {
        o.set ("layer", from_layer (db::LayerProperties (src.layer (), src.datatype ())));
      } else {
        o.set ("layer", val::null ());
      }
      o.set ("present", has_layer);
      o.set ("visible", i->visible (false));
      o.set ("fill", color (i->eff_fill_color (true)));
      o.set ("frame", color (i->eff_frame_color (true)));
      a.call<void> ("push", o);
    }
    return LayerEntryList (a);
  });
}

void View::set_layer_visible (unsigned int id, bool visible)
{
  guarded ([&] {
    lay::LayerPropertiesConstIterator i (m_view->get_properties (), size_t (id));
    if (i.is_null () || i.at_end ()) {
      throw tl::Exception ("no layer panel entry with that id");
    }
    lay::LayerProperties props = *i;
    props.set_visible (visible);
    // In a transaction, as KLayout's layer panel does; outside one the view
    // would clear the undo history.
    db::Transaction transaction (&m_manager, visible ? "Show layer" : "Hide layer");
    m_view->set_properties (i, props);
  });
}

void View::load_layer_properties (const Bytes &lyp)
{
  guarded ([&] {
    write_file (lyp_path, to_bytes (lyp));
    // KLayout makes this its own undo step.
    m_view->load_layer_props (lyp_path);
  });
}

Bytes View::save_layer_properties ()
{
  return guarded ([&] {
    m_view->save_layer_props (lyp_path);
    std::vector<uint8_t> data = read_file (lyp_path);
    return Bytes (from_bytes (data.data (), data.size ()));
  });
}

View::State View::state ()
{
  State s;
  s.undo_id = m_manager.transaction_id_for_undo ();
  s.redo_id = m_manager.transaction_id_for_redo ();
  std::pair<bool, std::string> undo = m_manager.available_undo ();
  std::pair<bool, std::string> redo = m_manager.available_redo ();
  s.can_undo = undo.first;
  s.undo = undo.second;
  s.can_redo = redo.first;
  s.redo = redo.second;
  s.selection = m_selection_serial;
  s.layer_panel = m_layer_panel_serial;
  s.mode = m_view->mode_name ();
  return s;
}

// What changed since the previous call, however it changed: through this
// API, or through KLayout's own tools reacting to input.
ChangeList View::changes ()
{
  return guarded ([&] {
    State now = state ();
    val a = val::array ();
    bool layout_changed = now.undo_id != m_seen.undo_id || now.redo_id != m_seen.redo_id ||
                          now.can_undo != m_seen.can_undo || now.undo != m_seen.undo ||
                          now.can_redo != m_seen.can_redo || now.redo != m_seen.redo;
    if (m_opened || layout_changed) {
      val e = val::object ();
      e.set ("type", "layoutChanged");
      e.set ("history", history ());
      a.call<void> ("push", e);
    }
    if (now.selection != m_seen.selection) {
      val e = val::object ();
      e.set ("type", "selectionChanged");
      a.call<void> ("push", e);
    }
    if (m_opened || now.layer_panel != m_seen.layer_panel) {
      val e = val::object ();
      e.set ("type", "layerPanelChanged");
      a.call<void> ("push", e);
    }
    if (now.mode != m_seen.mode) {
      val e = val::object ();
      e.set ("type", "modeChanged");
      e.set ("mode", now.mode);
      a.call<void> ("push", e);
    }
    m_seen = now;
    m_opened = false;
    return ChangeList (a);
  });
}

// -- Markers ------------------------------------------------------------------

static tl::Color marker_color (const val &m, const char *name)
{
  std::string c = to_string (m[name], name);
  if (c.size () != 7 || c[0] != '#' || c.find_first_not_of ("0123456789abcdefABCDEF", 1) != std::string::npos) {
    throw tl::Exception (std::string (name) + ": expected \"#rrggbb\"");
  }
  return tl::Color (c);
}

unsigned int View::set_markers (const MarkerList &markers)
{
  return guarded ([&] {
    if (! markers.isArray ()) {
      throw tl::Exception ("markers: expected an array");
    }
    double dbu = layout ().dbu ();
    db::CplxTrans to_um (dbu);
    unsigned int n = markers["length"].as<unsigned int> ();

    // Build all of them first: a bad entry replaces nothing.
    std::vector<std::unique_ptr<lay::DMarker>> built;
    built.reserve (n);
    db::Shapes scratch (false);
    for (unsigned int i = 0; i < n; ++i) {
      val m = markers[i];
      std::string at = "markers[" + std::to_string (i) + "].";
      try {
        if (m.isNull () || m.typeOf ().as<std::string> () != "object") {
          throw tl::Exception ("expected an object");
        }
        db::Shape s = insert_shape (field (m, "shape"), scratch);
        std::unique_ptr<lay::DMarker> marker (new lay::DMarker (m_view.get ()));
        if (s.is_box ()) {
          marker->set (db::DBox (s.box ().transformed (to_um)));
        } else if (s.is_polygon ()) {
          db::Polygon p;
          s.polygon (p);
          marker->set (p.transformed (to_um));
        } else if (s.is_path ()) {
          db::Path p;
          s.path (p);
          marker->set (p.transformed (to_um));
        } else if (s.is_text ()) {
          db::Text t;
          s.text (t);
          marker->set (t.transformed (to_um));
        } else if (s.is_edge ()) {
          marker->set (s.edge ().transformed (to_um));
        } else if (s.is_edge_pair ()) {
          marker->set (s.edge_pair ().transformed (to_um));
        } else if (s.is_point ()) {
          db::DPoint p = to_um * s.point ();
          marker->set (db::DBox (p, p));
          marker->set_vertex_size (5);
        } else {
          throw tl::Exception ("shape: unsupported kind");
        }
        marker->set_color (has (m, "color") ? marker_color (m, "color") : tl::Color (0xffff0000));
        if (has (m, "frameColor")) {
          marker->set_frame_color (marker_color (m, "frameColor"));
        }
        if (has (m, "lineWidth")) {
          marker->set_line_width (to_i32 (m["lineWidth"], "lineWidth"));
        }
        if (has (m, "vertexSize")) {
          marker->set_vertex_size (to_i32 (m["vertexSize"], "vertexSize"));
        }
        if (has (m, "ditherPattern")) {
          marker->set_dither_pattern (to_i32 (m["ditherPattern"], "ditherPattern"));
        }
        if (has (m, "halo")) {
          marker->set_halo (to_bool (m["halo"], "halo") ? 1 : 0);
        }
        built.push_back (std::move (marker));
      } catch (tl::Exception &ex) {
        throw tl::Exception (at + ex.msg ());
      }
      scratch.clear ();
    }
    m_markers = std::move (built);
    return (unsigned int) m_markers.size ();
  });
}

void View::clear_markers ()
{
  guarded ([&] { m_markers.clear (); });
}

unsigned int View::marker_count ()
{
  return (unsigned int) m_markers.size ();
}

LayoutSummary inspect (const Bytes &bytes)
{
  return guarded ([&] {
    std::vector<uint8_t> data = to_bytes (bytes);
    db::Layout layout (false);
    tl::InputMemoryStream memory (reinterpret_cast<const char *> (data.data ()), data.size ());
    tl::InputStream stream (memory);
    db::Reader reader (stream);
    reader.read (layout);
    size_t top = 0, layers = 0, shapes = 0;
    for (auto c = layout.begin_top_down (); c != layout.end_top_cells (); ++c) {
      ++top;
    }
    for (auto l = layout.begin_layers (); l != layout.end_layers (); ++l) {
      ++layers;
    }
    size_t cells = 0;
    for (auto c = layout.begin (); c != layout.end (); ++c, ++cells) {
      for (auto l = layout.begin_layers (); l != layout.end_layers (); ++l) {
        shapes += c->shapes ((*l).first).size ();
      }
    }
    val o = val::object ();
    o.set ("format", reader.format ());
    o.set ("dbu", layout.dbu ());
    o.set ("cells", unsigned (cells));
    o.set ("topCells", unsigned (top));
    o.set ("layers", unsigned (layers));
    o.set ("shapes", unsigned (shapes));
    return LayoutSummary (o);
  });
}

}
