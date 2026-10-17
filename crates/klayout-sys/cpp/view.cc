// The C ABI for KLayout's Qt-free layout view: the same lay::LayoutView the
// `klayout.lay` Python module uses headless, in synchronous mode (there are
// no threads), driven by synthetic input and read back as RGBA frames.
//
// See guard.h for error handling.

#include "codec.h"
#include "guard.h"

#include "dbClipboard.h"
#include "dbLayout.h"
#include "dbManager.h"
#include "dbStatic.h"
#include "dbSaveLayoutOptions.h"
#include "dbWriter.h"
#include "tlStream.h"
#include "layLayoutCanvas.h"
#include "layLayoutView.h"
#include "layLayerProperties.h"
#include "edtService.h"
#include "tlObject.h"
#include "tlPixelBuffer.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using kl::guarded;

namespace
{
struct View : public tl::Object
{
  // Declared before the view: the view refers to it and must die first.
  db::Manager manager;
  std::unique_ptr<lay::LayoutView> view;
  std::vector<uint8_t> rgba;
  unsigned int width = 0, height = 0;
  std::string text;
  // The result of the last call that returns encoded data; see kl_view_out.
  std::vector<uint8_t> out;
  // Bumped whenever KLayout reports a selection or layer panel change.
  uint64_t selection_serial = 0;
  uint64_t layer_panel_serial = 0;

  void on_selection_changed () { ++selection_serial; }
  void on_layer_list_changed (int) { ++layer_panel_serial; }

  db::Layout &layout ()
  {
    if (view->cellviews () == 0) {
      throw tl::Exception ("no layout loaded");
    }
    return view->cellview (0)->layout ();
  }

  db::Cell &cell (const char *name)
  {
    db::Layout &l = layout ();
    std::pair<bool, db::cell_index_type> ci = l.cell_by_name (name);
    if (! ci.first) {
      throw tl::Exception (std::string ("no cell named ") + name);
    }
    return l.cell (ci.second);
  }

  // The layer index for GDS layer/datatype, or -1 if the layout has none.
  int find_layer (const db::LayerProperties &lp)
  {
    db::Layout &l = layout ();
    for (auto i = l.begin_layers (); i != l.end_layers (); ++i) {
      if ((*i).second->layer == lp.layer && (*i).second->datatype == lp.datatype) {
        return int ((*i).first);
      }
    }
    return -1;
  }

  kl::Writer writer ()
  {
    out.clear ();
    return kl::Writer (out);
  }
};

db::Shapes &shapes_for_edit (View *v, const std::string &cell, const db::LayerProperties &lp, bool create, bool &created)
{
  db::Cell &c = v->cell (cell.c_str ());
  int li = v->find_layer (lp);
  if (li < 0) {
    if (! create) {
      throw tl::Exception ("no layer " + lp.to_string ());
    }
    li = int (v->layout ().insert_layer (lp));
    created = true;
  }
  return c.shapes ((unsigned int) li);
}

db::Instance find_instance (View *v, db::Cell &cell, const db::CellInstArray &wanted)
{
  for (db::Cell::const_iterator i = cell.begin (); ! i.at_end (); ++i) {
    if (i->cell_inst () == wanted) {
      return *i;
    }
  }
  throw tl::Exception (std::string ("no such instance in ") + v->layout ().cell_name (cell.cell_index ()));
}
}

extern "C" {

View *kl_view_new (int editable)
{
  View *v = nullptr;
  guarded ([&] {
    // Layouts KLayout creates internally (the clipboard's, for one) take
    // their mode from this global, which desktop KLayout sets at startup.
    db::set_default_editable_mode (editable != 0);
    std::unique_ptr<View> owned (new View ());
    owned->view.reset (new lay::LayoutView (&owned->manager, editable != 0, nullptr));
    owned->view->set_synchronous (true);
    owned->view->selection_changed_event.add (owned.get (), &View::on_selection_changed);
    owned->view->layer_list_changed_event.add (owned.get (), &View::on_layer_list_changed);
    v = owned.release ();
  });
  return v;
}

void kl_view_free (View *v)
{
  delete v;
}

// Loads a layout from a path in the Emscripten filesystem, replacing any
// layout shown, and fits it.
int kl_view_load (View *v, const char *path)
{
  return guarded ([&] {
    v->view->load_layout (std::string (path), false);
    v->view->max_hier ();
    v->view->zoom_fit ();
  });
}

int kl_view_resize (View *v, unsigned int width, unsigned int height)
{
  return guarded ([&] { v->view->resize (width, height); });
}

int kl_view_zoom_fit (View *v)
{
  return guarded ([&] { v->view->zoom_fit (); });
}

// Newline-separated; valid until the next call that returns text.
const char *kl_view_mode_names (View *v)
{
  v->text.clear ();
  for (const auto &name : v->view->mode_names ()) {
    v->text += name;
    v->text += '\n';
  }
  return v->text.c_str ();
}

const char *kl_view_mode_name (View *v)
{
  v->text = v->view->mode_name ();
  return v->text.c_str ();
}

int kl_view_switch_mode (View *v, const char *name)
{
  return guarded ([&] { v->view->switch_mode (std::string (name)); });
}

// kind: 0 move, 1 press, 2 release, 3 double click, 4 enter, 5 leave.
// (x, y) in canvas pixels; buttons as lay::ShiftButton | ... | LeftButton.
int kl_view_mouse (View *v, int kind, double x, double y, unsigned int buttons)
{
  return guarded ([&] {
    lay::LayoutCanvas *c = v->view->canvas ();
    db::DPoint p (x, y);
    switch (kind) {
      case 0: c->send_mouse_move_event (p, buttons); break;
      case 1: c->send_mouse_press_event (p, buttons); break;
      case 2: c->send_mouse_release_event (p, buttons); break;
      case 3: c->send_mouse_double_clicked_event (p, buttons); break;
      case 4: c->send_enter_event (); break;
      case 5: c->send_leave_event (); break;
      default: throw tl::Exception ("unknown mouse event kind");
    }
  });
}

int kl_view_wheel (View *v, int delta, int horizontal, double x, double y, unsigned int buttons)
{
  return guarded ([&] {
    v->view->canvas ()->send_wheel_event (delta, horizontal != 0, db::DPoint (x, y), buttons);
  });
}

int kl_view_key (View *v, unsigned int key, unsigned int buttons)
{
  return guarded ([&] { v->view->canvas ()->send_key_press_event (key, buttons); });
}

// Pumps the view and returns the current frame as tightly packed RGBA,
// including rubber bands and markers. Valid until the next call.
const uint8_t *kl_view_frame (View *v, unsigned int *width, unsigned int *height)
{
  const uint8_t *result = nullptr;
  guarded ([&] {
    // timer() runs the view's timer before the deferred methods, so a redraw
    // that an edit schedules is only drawn on the following pass. In
    // synchronous mode a pass with nothing to do is nearly free.
    for (int pass = 0; pass < 3; ++pass) {
      v->view->timer ();
    }
    tl::PixelBuffer pixels = v->view->canvas ()->screenshot ();
    v->width = pixels.width ();
    v->height = pixels.height ();
    v->rgba.resize (size_t (v->width) * v->height * 4);
    uint8_t *out = v->rgba.data ();
    for (unsigned int row = 0; row < v->height; ++row) {
      const tl::color_t *line = pixels.scan_line (row);
      for (unsigned int col = 0; col < v->width; ++col) {
        tl::color_t argb = line[col];
        *out++ = uint8_t (argb >> 16);
        *out++ = uint8_t (argb >> 8);
        *out++ = uint8_t (argb);
        *out++ = 0xff;
      }
    }
    result = v->rgba.data ();
  });
  *width = v->width;
  *height = v->height;
  return result;
}

// Makes GDS layer/datatype the layer that drawing tools create shapes on.
// Returns 1 if the view shows that layer, 0 if not, -1 on error.
int kl_view_set_current_layer (View *v, int layer, int datatype)
{
  int found = 0;
  if (guarded ([&] { found = v->view->set_current_layer (0, db::LayerProperties (layer, datatype)) ? 1 : 0; }) != 0) {
    return -1;
  }
  return found;
}

// Writes the shown layout; the format follows the file extension.
int kl_view_save (View *v, const char *path)
{
  return guarded ([&] {
    db::SaveLayoutOptions options;
    if (! options.set_format_from_filename (path).first) {
      throw tl::Exception ("cannot tell the layout format from the file name");
    }
    tl::OutputStream stream (path);
    db::Writer writer (options);
    writer.write (v->view->cellview (0)->layout (), stream);
  });
}

// Shapes in the shown layout, summed over every cell and layer.
size_t kl_view_shape_count (View *v)
{
  if (v->view->cellviews () == 0) {
    return 0;
  }
  const db::Layout &layout = v->view->cellview (0)->layout ();
  size_t n = 0;
  for (auto c = layout.begin (); c != layout.end (); ++c) {
    for (auto l = layout.begin_layers (); l != layout.end_layers (); ++l) {
      n += c->shapes ((*l).first).size ();
    }
  }
  return n;
}

int kl_view_undo (View *v)
{
  return guarded ([&] { v->manager.undo (); });
}

int kl_view_redo (View *v)
{
  return guarded ([&] { v->manager.redo (); });
}


const uint8_t *kl_view_out (View *v, size_t *len)
{
  *len = v->out.size ();
  return v->out.data ();
}

double kl_view_dbu (View *v)
{
  double dbu = 0.0;
  guarded ([&] { dbu = v->layout ().dbu (); });
  return dbu;
}

// format: 0 GDS2, 1 OASIS. The stream goes to kl_view_out.
int kl_view_export (View *v, int format)
{
  return guarded ([&] {
    db::SaveLayoutOptions options;
    switch (format) {
      case 0: options.set_format ("GDS2"); break;
      case 1: options.set_format ("OASIS"); break;
      default: throw tl::Exception ("unknown layout format");
    }
    tl::OutputMemoryStream memory;
    {
      tl::OutputStream stream (memory);
      db::Writer writer (options);
      writer.write (v->layout (), stream);
    }
    v->out.assign (memory.data (), memory.data () + memory.size ());
  });
}

// Cells: list of (str name, bool top, bool empty, box bbox).
int kl_view_cells (View *v)
{
  return guarded ([&] {
    db::Layout &l = v->layout ();
    kl::Writer w = v->writer ();
    // Counted while iterating: l.cells () includes the slots of deleted cells.
    size_t n = w.reserve ();
    uint32_t count = 0;
    for (auto c = l.begin (); c != l.end (); ++c, ++count) {
      w.str (l.cell_name (c->cell_index ()));
      w.boolean (c->is_top ());
      w.boolean (c->bbox ().empty ());
      w.box (c->bbox ().empty () ? db::Box (0, 0, 0, 0) : c->bbox ());
    }
    w.set_count (n, count);
  });
}

// Layers: list of (layer, str name).
int kl_view_layers (View *v)
{
  return guarded ([&] {
    db::Layout &l = v->layout ();
    kl::Writer w = v->writer ();
    size_t n = w.reserve ();
    uint32_t count = 0;
    for (auto i = l.begin_layers (); i != l.end_layers (); ++i, ++count) {
      w.layer (*(*i).second);
      w.str ((*i).second->name);
    }
    w.set_count (n, count);
  });
}

// Shapes of one cell on one layer: list of shape. An absent layer is empty.
int kl_view_shapes (View *v, const char *cell, int layer, int datatype)
{
  return guarded ([&] {
    db::Cell &c = v->cell (cell);
    int li = v->find_layer (db::LayerProperties (layer, datatype));
    kl::Writer w = v->writer ();
    size_t n = w.reserve ();
    uint32_t count = 0;
    if (li >= 0) {
      for (auto s = c.shapes ((unsigned int) li).begin (db::ShapeIterator::All); ! s.at_end (); ++s) {
        if (w.shape (*s)) {
          ++count;
        }
      }
    }
    w.set_count (n, count);
  });
}

// Instances placed in one cell: list of instance.
int kl_view_instances (View *v, const char *cell)
{
  return guarded ([&] {
    db::Cell &c = v->cell (cell);
    db::Layout &l = v->layout ();
    kl::Writer w = v->writer ();
    w.u32 (uint32_t (c.cell_instances ()));
    for (db::Cell::const_iterator i = c.begin (); ! i.at_end (); ++i) {
      w.instance (l, i->cell_inst ());
    }
  });
}

// Applies a sequence of edits as one undoable transaction: all of them, or
// on error none. Each edit is a u8 kind followed by
//   0 insert shape: str cell, layer, shape      1 erase shape: same
//   2 insert instance: str cell, instance        3 erase instance: same
//   4 create cell: str name   5 delete cell: str name   6 rename cell: str name, str new name
// A shape inserted on a layer the layout lacks creates that layer.
int kl_view_edit (View *v, const char *description, const uint8_t *data, size_t len)
{
  return guarded ([&] {
    db::Layout &l = v->layout ();
    kl::Reader r (data, len);
    bool created_layer = false;
    v->manager.transaction (description);
    try {
      while (! r.at_end ()) {
        switch (r.u8 ()) {
          case 0: {
            std::string cell = r.str ();
            db::LayerProperties lp = r.layer ();
            r.insert_shape (shapes_for_edit (v, cell, lp, true, created_layer));
            break;
          }
          case 1: {
            std::string cell = r.str ();
            db::LayerProperties lp = r.layer ();
            db::Shapes &target = shapes_for_edit (v, cell, lp, false, created_layer);
            db::Shapes probe (true);
            db::Shape found = target.find (r.insert_shape (probe));
            if (found.is_null ()) {
              throw tl::Exception ("no such shape in " + cell + " on " + lp.to_string ());
            }
            target.erase_shape (found);
            break;
          }
          case 2: {
            db::Cell &c = v->cell (r.str ().c_str ());
            c.insert (r.instance (l));
            break;
          }
          case 3: {
            db::Cell &c = v->cell (r.str ().c_str ());
            c.erase (find_instance (v, c, r.instance (l)));
            break;
          }
          case 4: {
            std::string name = r.str ();
            if (l.cell_by_name (name.c_str ()).first) {
              throw tl::Exception ("a cell named " + name + " exists already");
            }
            l.add_cell (name.c_str ());
            break;
          }
          case 5:
            l.delete_cell (v->cell (r.str ().c_str ()).cell_index ());
            break;
          case 6: {
            db::cell_index_type ci = v->cell (r.str ().c_str ()).cell_index ();
            std::string name = r.str ();
            if (l.cell_by_name (name.c_str ()).first) {
              throw tl::Exception ("a cell named " + name + " exists already");
            }
            l.rename_cell (ci, name.c_str ());
            break;
          }
          default:
            throw tl::Exception ("unknown edit kind");
        }
      }
      // Inside the transaction: the view clears the undo history on
      // layer panel changes made outside one.
      if (created_layer) {
        v->view->add_missing_layers ();
      }
    } catch (...) {
      v->manager.cancel ();
      throw;
    }
    v->manager.commit ();
  });
}

// The commands desktop KLayout puts in its Edit menu, with the same
// follow-up calls as layLayoutViewFunctions.cc:
// 0 delete, 1 cut, 2 copy, 3 paste, 4 paste and move, 5 select all,
// 6 unselect all, 7 cancel (Escape: abort edits, clear selection, select mode).
int kl_view_command (View *v, int command)
{
  return guarded ([&] {
    lay::LayoutView &view = *v->view;
    switch (command) {
      case 0:
        view.del ();
        view.cancel ();
        view.clear_selection ();
        break;
      case 1:
        view.cut ();
        view.cancel ();
        view.clear_selection ();
        break;
      case 2:
        view.copy ();
        view.clear_selection ();
        break;
      case 3:
      case 4:
        if (! db::Clipboard::instance ().empty ()) {
          view.cancel_edits ();
          if (command == 3) {
            view.paste ();
          } else {
            view.paste_interactive ();
          }
        }
        break;
      case 5:
        view.select_all ();
        break;
      case 6:
        view.select (db::DBox (), lay::Editable::Reset);
        break;
      case 7:
        view.cancel_esc ();
        break;
      default:
        throw tl::Exception ("unknown command");
    }
  });
}

// Selected objects: list of u8 kind, then
//   0 shape: str cell, layer, shape     1 instance: str parent cell, instance
int kl_view_selection (View *v)
{
  return guarded ([&] {
    db::Layout &l = v->layout ();
    kl::Writer w = v->writer ();
    size_t n = w.reserve ();
    uint32_t count = 0;
    for (const lay::ObjectInstPath &p : edt::object_selection (v->view.get ())) {
      if (p.cv_index () != 0) {
        continue;
      }
      if (p.is_cell_inst ()) {
        const db::Instance &inst = p.back ().inst_ptr;
        w.u8 (1);
        w.str (l.cell_name (inst.instances ()->cell ()->cell_index ()));
        w.instance (l, inst.cell_inst ());
        ++count;
      } else {
        size_t at = v->out.size ();
        w.u8 (0);
        w.str (l.cell_name (p.cell_index_tot ()));
        w.layer (l.get_properties (p.layer ()));
        if (w.shape (p.shape ())) {
          ++count;
        } else {
          v->out.resize (at);
        }
      }
    }
    w.set_count (n, count);
  });
}

// The layer panel, depth first: list of (u64 id, bool has parent, u64
// parent id, bool group, str label, str source, bool has layer, layer,
// bool visible, u32 fill ARGB, u32 frame ARGB). Ids are KLayout's iterator
// positions, valid until the panel changes.
int kl_view_layer_panel (View *v)
{
  return guarded ([&] {
    kl::Writer w = v->writer ();
    size_t n = w.reserve ();
    uint32_t count = 0;
    for (lay::LayerPropertiesConstIterator i = v->view->begin_layers (); ! i.at_end (); ++i, ++count) {
      w.u64 (i.uint ());
      lay::LayerPropertiesConstIterator parent = i.parent ();
      w.boolean (! parent.is_null ());
      w.u64 (parent.is_null () ? 0 : parent.uint ());
      w.boolean (i->has_children ());
      w.str (i->display_string (v->view.get (), true));
      w.str (i->source_string (true));
      int li = i->layer_index ();
      int cvi = i->cellview_index ();
      bool has_layer = li >= 0 && cvi >= 0 && cvi < int (v->view->cellviews ()) && v->view->cellview (cvi)->layout ().is_valid_layer ((unsigned int) li);
      w.boolean (has_layer);
      w.layer (has_layer ? v->view->cellview (cvi)->layout ().get_properties ((unsigned int) li) : db::LayerProperties (0, 0));
      w.boolean (i->visible (false));
      w.u32 (i->eff_fill_color (true));
      w.u32 (i->eff_frame_color (true));
    }
    w.set_count (n, count);
  });
}

int kl_view_set_layer_visible (View *v, uint64_t id, int visible)
{
  return guarded ([&] {
    lay::LayerPropertiesConstIterator i (v->view->get_properties (), size_t (id));
    if (i.is_null () || i.at_end ()) {
      throw tl::Exception ("no layer panel entry with that id");
    }
    lay::LayerProperties props = *i;
    props.set_visible (visible != 0);
    // In a transaction, as KLayout's layer panel does; outside one the view
    // would clear the undo history.
    db::Transaction transaction (&v->manager, visible ? "Show layer" : "Hide layer");
    v->view->set_properties (i, props);
  });
}

int kl_view_load_layer_properties (View *v, const char *path)
{
  // KLayout makes this its own undo step.
  return guarded ([&] { v->view->load_layer_props (std::string (path)); });
}

int kl_view_save_layer_properties (View *v, const char *path)
{
  return guarded ([&] { v->view->save_layer_props (std::string (path)); });
}

// What the caller needs to notice changes: u64 undo id, u64 redo id,
// bool can undo, str undo label, bool can redo, str redo label,
// u64 selection serial, u64 layer panel serial, str mode.
int kl_view_state (View *v)
{
  return guarded ([&] {
    kl::Writer w = v->writer ();
    w.u64 (v->manager.transaction_id_for_undo ());
    w.u64 (v->manager.transaction_id_for_redo ());
    std::pair<bool, std::string> undo = v->manager.available_undo ();
    std::pair<bool, std::string> redo = v->manager.available_redo ();
    w.boolean (undo.first);
    w.str (undo.second);
    w.boolean (redo.first);
    w.str (redo.second);
    w.u64 (v->selection_serial);
    w.u64 (v->layer_panel_serial);
    w.str (v->view->mode_name ());
  });
}

}
