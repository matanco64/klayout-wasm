// Comparing two layout files without a view: diff reports the objects that
// differ, through KLayout's db::LayoutDiff; xor_layouts reports where the
// drawn geometry differs, through db::Region.

#include "klayout_wasm.h"

#include "dbLayoutDiff.h"
#include "dbRecursiveShapeIterator.h"
#include "dbRegion.h"

#include <cmath>
#include <map>
#include <utility>

namespace kl
{

namespace
{

bool flag (const val &options, const char *name)
{
  return given (options, name) && to_bool (options[name], name);
}

// Builds the LayoutDiff value from db::LayoutDiff's callbacks. With
// f_verbose, the shape lists arrive already reduced to what only one side has.
typedef std::pair<int, int> LayerKey;

// Layer/datatype -> the layer's index in the first and second layout, -1 if absent.
typedef std::map<LayerKey, std::pair<int, int>> LayerMap;

void collect_layers (const db::Layout &layout, bool second, LayerMap &layers)
{
  for (auto l = layout.begin_layers (); l != layout.end_layers (); ++l) {
    const db::LayerProperties &lp = *(*l).second;
    if (lp.layer >= 0 && lp.datatype >= 0) {
      auto entry = layers.emplace (LayerKey (lp.layer, lp.datatype), std::make_pair (-1, -1)).first;
      (second ? entry->second.second : entry->second.first) = int ((*l).first);
    }
  }
}

LayerMap layers_of (const db::Layout &a, const db::Layout &b)
{
  LayerMap layers;
  collect_layers (a, false, layers);
  collect_layers (b, true, layers);
  return layers;
}

class Collector : public db::DifferenceReceiver
{
public:
  val result (bool identical, const val &layers_only_a, const val &layers_only_b)
  {
    val o = val::object ();
    o.set ("identical", identical);
    o.set ("dbu", m_dbu);
    o.set ("layersOnlyInA", layers_only_a);
    o.set ("layersOnlyInB", layers_only_b);
    o.set ("cellsOnlyInA", m_cells_a);
    o.set ("cellsOnlyInB", m_cells_b);
    o.set ("cells", m_cells);
    return o;
  }

  void dbu_differs (double dbu_a, double dbu_b) override
  {
    m_dbu = val::object ();
    m_dbu.set ("a", dbu_a);
    m_dbu.set ("b", dbu_b);
  }

  void cell_in_a_only (const std::string &name, db::cell_index_type) override { m_cells_a.call<void> ("push", name); }
  void cell_in_b_only (const std::string &name, db::cell_index_type) override { m_cells_b.call<void> ("push", name); }

  void begin_cell (const std::string &name, db::cell_index_type, db::cell_index_type) override
  {
    m_cell = val::object ();
    m_cell.set ("cell", name);
    m_insts_a = val::array ();
    m_insts_b = val::array ();
    m_layers = val::array ();
    m_cell_differs = false;
  }

  void instances_in_a_only (const std::vector<db::CellInstArrayWithProperties> &insts, const db::Layout &a) override
  {
    add_instances (insts, a, m_insts_a);
  }

  void instances_in_b_only (const std::vector<db::CellInstArrayWithProperties> &insts, const db::Layout &b) override
  {
    add_instances (insts, b, m_insts_b);
  }

  void begin_layer (const db::LayerProperties &layer, unsigned int, bool, unsigned int, bool) override
  {
    m_layer = layer;
    m_only_a = val::array ();
    m_only_b = val::array ();
    m_layer_differs = false;
  }

  void detailed_diff (const std::vector<std::pair<db::Polygon, db::properties_id_type>> &a, const std::vector<std::pair<db::Polygon, db::properties_id_type>> &b) override { add (a, b); }
  void detailed_diff (const std::vector<std::pair<db::Path, db::properties_id_type>> &a, const std::vector<std::pair<db::Path, db::properties_id_type>> &b) override { add (a, b); }
  void detailed_diff (const std::vector<std::pair<db::Box, db::properties_id_type>> &a, const std::vector<std::pair<db::Box, db::properties_id_type>> &b) override { add (a, b); }
  void detailed_diff (const std::vector<std::pair<db::Edge, db::properties_id_type>> &a, const std::vector<std::pair<db::Edge, db::properties_id_type>> &b) override { add (a, b); }
  void detailed_diff (const std::vector<std::pair<db::EdgePair, db::properties_id_type>> &a, const std::vector<std::pair<db::EdgePair, db::properties_id_type>> &b) override { add (a, b); }
  void detailed_diff (const std::vector<std::pair<db::Text, db::properties_id_type>> &a, const std::vector<std::pair<db::Text, db::properties_id_type>> &b) override { add (a, b); }

  void end_layer () override
  {
    if (m_layer_differs) {
      val o = val::object ();
      o.set ("layer", from_layer (m_layer));
      o.set ("onlyInA", m_only_a);
      o.set ("onlyInB", m_only_b);
      m_layers.call<void> ("push", o);
      m_cell_differs = true;
    }
  }

  void end_cell () override
  {
    if (m_cell_differs) {
      m_cell.set ("instancesOnlyInA", m_insts_a);
      m_cell.set ("instancesOnlyInB", m_insts_b);
      m_cell.set ("layers", m_layers);
      m_cells.call<void> ("push", m_cell);
    }
  }

private:
  void add_instances (const std::vector<db::CellInstArrayWithProperties> &insts, const db::Layout &layout, val &target)
  {
    for (const db::CellInstArrayWithProperties &inst : insts) {
      target.call<void> ("push", from_instance (layout, inst));
      m_cell_differs = true;
    }
  }

  template <class S>
  void add (const std::vector<std::pair<S, db::properties_id_type>> &a, const std::vector<std::pair<S, db::properties_id_type>> &b)
  {
    for (const auto &s : a) {
      m_only_a.call<void> ("push", from_shape (s.first));
    }
    for (const auto &s : b) {
      m_only_b.call<void> ("push", from_shape (s.first));
    }
    m_layer_differs = m_layer_differs || ! a.empty () || ! b.empty ();
  }

  val m_dbu = val::null ();
  val m_cells_a = val::array (), m_cells_b = val::array ();
  val m_cells = val::array ();
  val m_cell = val::null (), m_insts_a = val::null (), m_insts_b = val::null (), m_layers = val::null ();
  val m_only_a = val::null (), m_only_b = val::null ();
  db::LayerProperties m_layer;
  bool m_cell_differs = false, m_layer_differs = false;
};

// The cell `options[key]` names, or the layout's only top cell.
const db::Cell &cell_to_compare (const db::Layout &layout, const val &options, const char *key)
{
  if (given (options, key)) {
    std::string name = to_string (options[key], key);
    std::pair<bool, db::cell_index_type> ci = layout.cell_by_name (name.c_str ());
    if (! ci.first) {
      throw tl::Exception (std::string (key) + ": no cell named " + name);
    }
    return layout.cell (ci.second);
  }
  size_t tops = 0;
  db::cell_index_type top = 0;
  for (auto c = layout.begin_top_down (); c != layout.end_top_cells (); ++c, ++tops) {
    top = *c;
  }
  if (tops != 1) {
    throw tl::Exception (std::string (key) + ": the layout has " + std::to_string (tops) + " top cells; name one");
  }
  return layout.cell (top);
}

}

LayoutDiff diff (const Bytes &bytes_a, const Bytes &bytes_b, const DiffOptions &options)
{
  return guarded ([&] {
    // A layer only one layout has is compared like any other, so its shapes
    // are reported; the layer itself is listed below.
    unsigned int flags = db::layout_diff::f_verbose | db::layout_diff::f_no_layer_names |
                         db::layout_diff::f_dont_summarize_missing_layers;
    if (! flag (options, "properties")) {
      flags |= db::layout_diff::f_no_properties;
    }
    if (flag (options, "boxesAsPolygons")) {
      flags |= db::layout_diff::f_boxes_as_polygons;
    }
    if (flag (options, "pathsAsPolygons")) {
      flags |= db::layout_diff::f_paths_as_polygons;
    }
    if (flag (options, "expandArrays")) {
      flags |= db::layout_diff::f_flatten_array_insts;
    }
    if (flag (options, "ignoreTextDetails")) {
      // Font and alignment; orientation and size.
      flags |= db::layout_diff::f_no_text_details | db::layout_diff::f_no_text_orientation;
    }
    if (flag (options, "ignoreDuplicates")) {
      flags |= db::layout_diff::f_ignore_duplicates;
    }
    int32_t tolerance = given (options, "tolerance") ? to_i32 (options["tolerance"], "tolerance") : 0;
    if (tolerance < 0) {
      throw tl::Exception ("tolerance: expected 0 or more");
    }

    db::Layout a (false), b (false);
    read_layout (bytes_a, a);
    read_layout (bytes_b, b);
    Collector collector;
    bool identical = db::compare_layouts (a, b, flags, tolerance, collector);
    val only_a = val::array (), only_b = val::array ();
    for (const auto &l : layers_of (a, b)) {
      if (l.second.first < 0 || l.second.second < 0) {
        (l.second.second < 0 ? only_a : only_b).call<void> ("push", from_layer (db::LayerProperties (l.first.first, l.first.second)));
      }
    }
    return LayoutDiff (collector.result (identical, only_a, only_b));
  });
}

XorResult xor_layouts (const Bytes &bytes_a, const Bytes &bytes_b, const XorOptions &options)
{
  return guarded ([&] {
    db::Layout a (false), b (false);
    read_layout (bytes_a, a);
    read_layout (bytes_b, b);
    if (std::fabs (a.dbu () - b.dbu ()) > 1e-12) {
      throw tl::Exception ("the database units differ (" + std::to_string (a.dbu ()) + " and " + std::to_string (b.dbu ()) +
                           " micron); geometry cannot be compared");
    }
    const db::Cell &cell_a = cell_to_compare (a, options, "cellA");
    const db::Cell &cell_b = cell_to_compare (b, options, "cellB");

    val result = val::array ();
    for (const auto &l : layers_of (a, b)) {
      db::Region ra, rb;
      if (l.second.first >= 0) {
        ra = db::Region (db::RecursiveShapeIterator (a, cell_a, (unsigned int) l.second.first));
      }
      if (l.second.second >= 0) {
        rb = db::Region (db::RecursiveShapeIterator (b, cell_b, (unsigned int) l.second.second));
      }
      db::Region differing = ra ^ rb;
      val polygons = val::array ();
      bool any = false;
      for (db::Region::const_iterator p = differing.begin_merged (); ! p.at_end (); ++p) {
        polygons.call<void> ("push", from_shape (*p));
        any = true;
      }
      if (any) {
        val o = val::object ();
        o.set ("layer", from_layer (db::LayerProperties (l.first.first, l.first.second)));
        o.set ("polygons", polygons);
        result.call<void> ("push", o);
      }
    }
    return XorResult (result);
  });
}

}
