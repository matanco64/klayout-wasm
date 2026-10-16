// The C ABI for KLayout's Qt-free layout view: the same lay::LayoutView the
// `klayout.lay` Python module uses headless, in synchronous mode (there are
// no threads), driven by synthetic input and read back as RGBA frames.
//
// See guard.h for error handling.

#include "guard.h"

#include "dbLayout.h"
#include "dbManager.h"
#include "dbSaveLayoutOptions.h"
#include "dbWriter.h"
#include "tlStream.h"
#include "layLayoutCanvas.h"
#include "layLayoutView.h"
#include "tlPixelBuffer.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using kl::guarded;

namespace
{
struct View
{
  // Declared before the view: the view refers to it and must die first.
  db::Manager manager;
  std::unique_ptr<lay::LayoutView> view;
  std::vector<uint8_t> rgba;
  unsigned int width = 0, height = 0;
  std::string text;
};
}

extern "C" {

View *kl_view_new (int editable)
{
  View *v = nullptr;
  guarded ([&] {
    std::unique_ptr<View> owned (new View ());
    owned->view.reset (new lay::LayoutView (&owned->manager, editable != 0, nullptr));
    owned->view->set_synchronous (true);
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

}
