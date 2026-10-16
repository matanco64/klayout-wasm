// The C ABI for KLayout's layout database. See guard.h for error handling.

#include "guard.h"

#include "dbLayout.h"
#include "dbReader.h"
#include "tlStream.h"

#include <cstddef>
#include <cstdint>

std::string &kl::error_slot ()
{
  static std::string slot;
  return slot;
}

using kl::guarded;

extern "C" {

const char *kl_last_error ()
{
  return kl::error_slot ().c_str ();
}

db::Layout *kl_layout_new ()
{
  db::Layout *layout = nullptr;
  guarded ([&] { layout = new db::Layout (); });
  return layout;
}

void kl_layout_free (db::Layout *layout)
{
  delete layout;
}

// Reads GDS2 or OASIS (detected from the content) from memory.
int kl_layout_read (db::Layout *layout, const uint8_t *data, size_t len)
{
  return guarded ([&] {
    tl::InputMemoryStream memory (reinterpret_cast<const char *> (data), len);
    tl::InputStream stream (memory);
    db::Reader reader (stream);
    reader.read (*layout);
  });
}

double kl_layout_dbu (const db::Layout *layout)
{
  return layout->dbu ();
}

size_t kl_layout_cell_count (const db::Layout *layout)
{
  return layout->cells ();
}

size_t kl_layout_top_cell_count (const db::Layout *layout)
{
  size_t n = 0;
  for (auto c = layout->begin_top_down (); c != layout->end_top_cells (); ++c) {
    ++n;
  }
  return n;
}

size_t kl_layout_layer_count (const db::Layout *layout)
{
  size_t n = 0;
  for (auto l = layout->begin_layers (); l != layout->end_layers (); ++l) {
    ++n;
  }
  return n;
}

// Shapes summed over every cell and layer, unflattened.
size_t kl_layout_shape_count (const db::Layout *layout)
{
  size_t n = 0;
  for (auto c = layout->begin (); c != layout->end (); ++c) {
    for (auto l = layout->begin_layers (); l != layout->end_layers (); ++l) {
      n += c->shapes ((*l).first).size ();
    }
  }
  return n;
}

}
