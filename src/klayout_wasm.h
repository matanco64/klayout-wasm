// klayout-wasm: KLayout's layout view and editors, exposed to JavaScript
// with Embind. view.cc holds the View class, convert.cc the conversions
// between JavaScript values and KLayout's types, bindings.cc the Embind
// registrations and the TypeScript types they emit.
//
// JavaScript values are checked as they are read: a missing or mistyped
// field throws a JavaScript Error naming it. The TypeScript types in
// bindings.cc describe the same shapes for callers.

#pragma once

#include "dbInstances.h"
#include "dbLayout.h"
#include "dbShape.h"
#include "dbShapes.h"
#include "dbManager.h"
#include "layLayoutView.h"
#include "layMarker.h"
#include "tlException.h"
#include "tlObject.h"

#include <emscripten/val.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace kl
{

using emscripten::val;

// JavaScript values with a TypeScript type of the same name (bindings.cc).
EMSCRIPTEN_DECLARE_VAL_TYPE (Bytes)
EMSCRIPTEN_DECLARE_VAL_TYPE (Buttons)
EMSCRIPTEN_DECLARE_VAL_TYPE (MouseAction)
EMSCRIPTEN_DECLARE_VAL_TYPE (Key)
EMSCRIPTEN_DECLARE_VAL_TYPE (Frame)
EMSCRIPTEN_DECLARE_VAL_TYPE (LayerSpec)
EMSCRIPTEN_DECLARE_VAL_TYPE (Command)
EMSCRIPTEN_DECLARE_VAL_TYPE (Format)
EMSCRIPTEN_DECLARE_VAL_TYPE (CellList)
EMSCRIPTEN_DECLARE_VAL_TYPE (LayerList)
EMSCRIPTEN_DECLARE_VAL_TYPE (ShapeList)
EMSCRIPTEN_DECLARE_VAL_TYPE (InstanceList)
EMSCRIPTEN_DECLARE_VAL_TYPE (EditList)
EMSCRIPTEN_DECLARE_VAL_TYPE (SelectionList)
EMSCRIPTEN_DECLARE_VAL_TYPE (LayerEntryList)
EMSCRIPTEN_DECLARE_VAL_TYPE (History)
EMSCRIPTEN_DECLARE_VAL_TYPE (ChangeList)
EMSCRIPTEN_DECLARE_VAL_TYPE (StringList)
EMSCRIPTEN_DECLARE_VAL_TYPE (LayoutSummary)
EMSCRIPTEN_DECLARE_VAL_TYPE (Point)
EMSCRIPTEN_DECLARE_VAL_TYPE (Rect)
EMSCRIPTEN_DECLARE_VAL_TYPE (ViewConfig)
EMSCRIPTEN_DECLARE_VAL_TYPE (OptionalString)
EMSCRIPTEN_DECLARE_VAL_TYPE (MarkerList)

// Runs f, turning a C++ exception into a JavaScript Error. Every function
// JavaScript can call goes through here.
template <class F>
auto guarded (F &&f) -> decltype (f ())
{
  std::string message;
  try {
    return f ();
  } catch (tl::Exception &ex) {
    message = ex.msg ();
  } catch (std::exception &ex) {
    message = ex.what ();
  } catch (...) {
    message = "unknown C++ exception";
  }
  // Thrown outside the catch handler, so the C++ exception is released.
  val::global ("Error").new_ (message).throw_ ();
}

// Reading fields of JavaScript objects, checked.
val field (const val &object, const char *name);
bool has (const val &object, const char *name);
int32_t to_i32 (const val &v, const char *what);
double to_f64 (const val &v, const char *what);
bool to_bool (const val &v, const char *what);
std::string to_string (const val &v, const char *what);
std::vector<uint8_t> to_bytes (const val &v);
val from_bytes (const uint8_t *data, size_t size);

// KLayout values to JavaScript and back.
val from_layer (const db::LayerProperties &lp);
db::LayerProperties to_layer (const val &v);
val from_box (const db::Box &b);
db::Box to_box (const val &v);
// Undefined for user objects, which have no portable form.
val from_shape (const db::Shape &s);
db::Shape insert_shape (const val &v, db::Shapes &target);
val from_instance (const db::Layout &layout, const db::CellInstArray &inst);
db::CellInstArray to_instance (const val &v, const db::Layout &layout);
unsigned int to_buttons (const val &v);
unsigned int to_qt_key (const val &v);
std::string color (tl::color_t argb);

// KLayout's Qt-free layout view in synchronous mode (no threads), driven
// by synthetic input and read back as RGBA frames.
class View : public tl::Object
{
public:
  explicit View (bool editable);
  ~View ();

  void open (const Bytes &bytes);
  Bytes export_layout (const Format &format);

  void resize (unsigned int width, unsigned int height);
  Frame frame ();
  void mouse (const MouseAction &action, double x, double y, const Buttons &buttons);
  void wheel (int delta, bool horizontal, double x, double y, const Buttons &buttons);
  void key (const Key &key, const Buttons &buttons);
  void zoom_fit ();
  void set_device_pixel_ratio (double ratio);
  void configure (const ViewConfig &config);
  Rect viewport ();
  void zoom_box (const Rect &box);
  Point point_at (double x, double y);
  void show_cell (const std::string &name);
  OptionalString current_cell ();

  StringList modes ();
  std::string mode ();
  void switch_mode (const std::string &name);
  bool set_current_layer (const LayerSpec &layer);

  void undo ();
  void redo ();
  History history ();
  void command (const Command &command);

  double dbu ();
  CellList cells ();
  LayerList layers ();
  ShapeList shapes (const std::string &cell, const LayerSpec &layer);
  InstanceList instances (const std::string &cell);
  SelectionList selection ();
  void edit (const std::string &description, const EditList &edits);

  LayerEntryList layer_panel ();
  void set_layer_visible (unsigned int id, bool visible);
  void load_layer_properties (const Bytes &lyp);
  Bytes save_layer_properties ();

  ChangeList changes ();

  // Markers: outlines drawn over the layout (KLayout's lay::DMarker, what its
  // marker browser shows), not part of the layout and not undoable.
  unsigned int set_markers (const MarkerList &markers);
  void clear_markers ();
  unsigned int marker_count ();

private:
  struct State
  {
    size_t undo_id = 0, redo_id = 0;
    std::string undo, redo;
    bool can_undo = false, can_redo = false;
    uint64_t selection = 0, layer_panel = 0;
    std::string mode;
  };

  db::Layout &layout ();
  db::Cell &cell (const std::string &name);
  int find_layer (const db::LayerProperties &lp);
  db::Shapes &shapes_for_edit (const std::string &cell, const db::LayerProperties &lp, bool create, bool &created);
  State state ();
  void on_selection_changed () { ++m_selection_serial; }
  void on_layer_list_changed (int) { ++m_layer_panel_serial; }

  // Declared before the view: the view refers to it and must die first.
  db::Manager m_manager;
  std::unique_ptr<lay::LayoutView> m_view;
  std::vector<uint8_t> m_rgba;
  uint64_t m_selection_serial = 0, m_layer_panel_serial = 0;
  // The CSS size last given to resize.
  unsigned int m_width = 0, m_height = 0;
  State m_seen;
  bool m_opened = false;
  // Declared after the view, so they are destroyed before it.
  std::vector<std::unique_ptr<lay::DMarker>> m_markers;
};

// Reads GDS2 or OASIS into a standalone layout and summarizes it.
LayoutSummary inspect (const Bytes &bytes);

}
