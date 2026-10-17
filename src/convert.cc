// Conversions between JavaScript values and KLayout's types. The shapes of
// the JavaScript values are the TypeScript types in bindings.cc.

#include "klayout_wasm.h"

#include "dbEdgePair.h"
#include "dbHersheyFont.h"
#include "dbPath.h"
#include "dbPolygon.h"
#include "dbText.h"

#include <cmath>
#include <cstdio>
#include <limits>

namespace kl
{

namespace
{

const char *const orientations[] = { "R0", "R90", "R180", "R270", "M0", "M45", "M90", "M135" };
const char *const haligns[] = { "left", "center", "right" };
const char *const valigns[] = { "top", "center", "bottom" };

// Index of `name` in `names`, or an exception naming what was expected.
template <size_t N>
int pick (const char *const (&names)[N], const val &v, const char *what)
{
  std::string name = to_string (v, what);
  for (size_t i = 0; i < N; ++i) {
    if (name == names[i]) {
      return int (i);
    }
  }
  throw tl::Exception (std::string (what) + ": unknown value \"" + name + "\"");
}

val from_point (const db::Point &p)
{
  val o = val::object ();
  o.set ("x", p.x ());
  o.set ("y", p.y ());
  return o;
}

db::Point to_point (const val &v)
{
  return db::Point (to_i32 (field (v, "x"), "x"), to_i32 (field (v, "y"), "y"));
}

db::Vector to_vector (const val &v)
{
  return to_point (v) - db::Point ();
}

val from_edge (const db::Edge &e)
{
  val o = val::object ();
  o.set ("from", from_point (e.p1 ()));
  o.set ("to", from_point (e.p2 ()));
  return o;
}

db::Edge to_edge (const val &v)
{
  return db::Edge (to_point (field (v, "from")), to_point (field (v, "to")));
}

template <class Iter>
val from_points (Iter from, Iter to)
{
  val a = val::array ();
  for (Iter i = from; i != to; ++i) {
    a.call<void> ("push", from_point (*i));
  }
  return a;
}

std::vector<db::Point> to_points (const val &v, const char *what)
{
  if (! v.isArray ()) {
    throw tl::Exception (std::string (what) + ": expected an array of points");
  }
  std::vector<db::Point> pts;
  unsigned int n = v["length"].as<unsigned int> ();
  pts.reserve (n);
  for (unsigned int i = 0; i < n; ++i) {
    pts.push_back (to_point (v[i]));
  }
  return pts;
}

bool is_object (const val &v)
{
  return ! v.isNull () && v.typeOf ().as<std::string> () == "object";
}

// An optional field: absent, undefined and null all mean "not given".
bool given (const val &object, const char *name)
{
  if (! has (object, name)) {
    return false;
  }
  val v = object[name];
  return ! v.isUndefined () && ! v.isNull ();
}

}

val field (const val &object, const char *name)
{
  if (! is_object (object)) {
    throw tl::Exception (std::string ("expected an object with \"") + name + "\"");
  }
  val v = object[name];
  if (v.isUndefined ()) {
    throw tl::Exception (std::string ("missing field \"") + name + "\"");
  }
  return v;
}

bool has (const val &object, const char *name)
{
  return is_object (object) && ! object[name].isUndefined ();
}

int32_t to_i32 (const val &v, const char *what)
{
  double d = to_f64 (v, what);
  if (d != std::floor (d) || d < std::numeric_limits<int32_t>::min () || d > std::numeric_limits<int32_t>::max ()) {
    throw tl::Exception (std::string (what) + ": expected a 32-bit integer");
  }
  return int32_t (d);
}

double to_f64 (const val &v, const char *what)
{
  if (! v.isNumber ()) {
    throw tl::Exception (std::string (what) + ": expected a number");
  }
  double d = v.as<double> ();
  if (! std::isfinite (d)) {
    throw tl::Exception (std::string (what) + ": expected a finite number");
  }
  return d;
}

bool to_bool (const val &v, const char *what)
{
  if (! v.isTrue () && ! v.isFalse ()) {
    throw tl::Exception (std::string (what) + ": expected true or false");
  }
  return v.isTrue ();
}

std::string to_string (const val &v, const char *what)
{
  if (! v.isString ()) {
    throw tl::Exception (std::string (what) + ": expected a string");
  }
  return v.as<std::string> ();
}

std::vector<uint8_t> to_bytes (const val &v)
{
  if (! v.instanceof (val::global ("Uint8Array"))) {
    throw tl::Exception ("expected a Uint8Array");
  }
  return emscripten::convertJSArrayToNumberVector<uint8_t> (v);
}

val from_bytes (const uint8_t *data, size_t size)
{
  // A copy: the caller keeps it beyond the next call.
  return val::global ("Uint8Array").new_ (emscripten::typed_memory_view (size, data));
}

val from_layer (const db::LayerProperties &lp)
{
  val o = val::object ();
  o.set ("layer", lp.layer);
  o.set ("datatype", lp.datatype);
  return o;
}

db::LayerProperties to_layer (const val &v)
{
  return db::LayerProperties (to_i32 (field (v, "layer"), "layer"), to_i32 (field (v, "datatype"), "datatype"));
}

val from_box (const db::Box &b)
{
  val o = val::object ();
  o.set ("left", b.left ());
  o.set ("bottom", b.bottom ());
  o.set ("right", b.right ());
  o.set ("top", b.top ());
  return o;
}

val from_shape (const db::Shape &s)
{
  val o = val::object ();
  if (s.is_box ()) {
    o = from_box (s.box ());
    o.set ("kind", "box");
  } else if (s.is_polygon () || s.is_simple_polygon ()) {
    db::Polygon p;
    s.polygon (p);
    o.set ("kind", "polygon");
    o.set ("hull", from_points (p.begin_hull (), p.end_hull ()));
    val holes = val::array ();
    for (unsigned int h = 0; h < p.holes (); ++h) {
      holes.call<void> ("push", from_points (p.begin_hole (h), p.end_hole (h)));
    }
    o.set ("holes", holes);
  } else if (s.is_path ()) {
    db::Path p;
    s.path (p);
    o.set ("kind", "path");
    o.set ("points", from_points (p.begin (), p.end ()));
    o.set ("width", p.width ());
    o.set ("beginExtension", p.bgn_ext ());
    o.set ("endExtension", p.end_ext ());
    o.set ("round", p.round ());
  } else if (s.is_text ()) {
    db::Text t;
    s.text (t);
    o.set ("kind", "text");
    o.set ("string", std::string (t.string ()));
    o.set ("position", from_point (db::Point () + t.trans ().disp ()));
    o.set ("orientation", orientations[t.trans ().fp_trans ().rot ()]);
    o.set ("size", t.size ());
    o.set ("font", t.font () >= 0 ? val (int (t.font ())) : val::null ());
    o.set ("halign", t.halign () >= 0 && t.halign () <= 2 ? val (haligns[t.halign ()]) : val::null ());
    o.set ("valign", t.valign () >= 0 && t.valign () <= 2 ? val (valigns[t.valign ()]) : val::null ());
  } else if (s.is_edge ()) {
    o = from_edge (s.edge ());
    o.set ("kind", "edge");
  } else if (s.is_edge_pair ()) {
    o.set ("kind", "edgePair");
    o.set ("first", from_edge (s.edge_pair ().first ()));
    o.set ("second", from_edge (s.edge_pair ().second ()));
  } else if (s.is_point ()) {
    o = from_point (s.point ());
    o.set ("kind", "point");
  } else {
    return val::undefined ();
  }
  return o;
}

db::Shape insert_shape (const val &v, db::Shapes &target)
{
  std::string kind = to_string (field (v, "kind"), "kind");
  if (kind == "box") {
    return target.insert (db::Box (to_i32 (field (v, "left"), "left"), to_i32 (field (v, "bottom"), "bottom"),
                                   to_i32 (field (v, "right"), "right"), to_i32 (field (v, "top"), "top")));
  } else if (kind == "polygon") {
    db::Polygon p;
    std::vector<db::Point> hull = to_points (field (v, "hull"), "hull");
    p.assign_hull (hull.begin (), hull.end ());
    if (given (v, "holes")) {
      val holes = v["holes"];
      if (! holes.isArray ()) {
        throw tl::Exception ("holes: expected an array");
      }
      unsigned int n = holes["length"].as<unsigned int> ();
      for (unsigned int h = 0; h < n; ++h) {
        std::vector<db::Point> hole = to_points (holes[h], "hole");
        p.insert_hole (hole.begin (), hole.end ());
      }
    }
    return target.insert (p);
  } else if (kind == "path") {
    std::vector<db::Point> pts = to_points (field (v, "points"), "points");
    db::Coord bgn_ext = given (v, "beginExtension") ? to_i32 (v["beginExtension"], "beginExtension") : 0;
    db::Coord end_ext = given (v, "endExtension") ? to_i32 (v["endExtension"], "endExtension") : 0;
    bool round = given (v, "round") && to_bool (v["round"], "round");
    return target.insert (db::Path (pts.begin (), pts.end (), to_i32 (field (v, "width"), "width"), bgn_ext, end_ext, round));
  } else if (kind == "text") {
    int orientation = given (v, "orientation") ? pick (orientations, v["orientation"], "orientation") : 0;
    db::Trans t (orientation, to_point (field (v, "position")) - db::Point ());
    db::Coord size = given (v, "size") ? to_i32 (v["size"], "size") : 0;
    int font = given (v, "font") ? to_i32 (v["font"], "font") : -1;
    int halign = given (v, "halign") ? pick (haligns, v["halign"], "halign") : -1;
    int valign = given (v, "valign") ? pick (valigns, v["valign"], "valign") : -1;
    return target.insert (db::Text (to_string (field (v, "string"), "string"), t, size, db::Font (font),
                                    db::HAlign (halign), db::VAlign (valign)));
  } else if (kind == "edge") {
    return target.insert (to_edge (v));
  } else if (kind == "edgePair") {
    return target.insert (db::EdgePair (to_edge (field (v, "first")), to_edge (field (v, "second"))));
  } else if (kind == "point") {
    return target.insert (to_point (v));
  }
  throw tl::Exception ("kind: unknown shape kind \"" + kind + "\"");
}

val from_instance (const db::Layout &layout, const db::CellInstArray &inst)
{
  val o = val::object ();
  o.set ("cell", std::string (layout.cell_name (inst.object ().cell_index ())));
  db::ICplxTrans t = inst.complex_trans ();
  val transform = val::object ();
  val displacement = val::object ();
  displacement.set ("x", t.disp ().x ());
  displacement.set ("y", t.disp ().y ());
  transform.set ("displacement", displacement);
  transform.set ("angle", t.angle ());
  transform.set ("mirror", t.is_mirror ());
  transform.set ("magnification", t.mag ());
  o.set ("transform", transform);
  db::Vector a, b;
  unsigned long na = 1, nb = 1;
  if (inst.is_regular_array (a, b, na, nb)) {
    val array = val::object ();
    array.set ("a", from_point (db::Point () + a));
    array.set ("b", from_point (db::Point () + b));
    array.set ("na", double (na));
    array.set ("nb", double (nb));
    o.set ("array", array);
  } else {
    o.set ("array", val::null ());
  }
  return o;
}

db::CellInstArray to_instance (const val &v, const db::Layout &layout)
{
  std::string child = to_string (field (v, "cell"), "cell");
  std::pair<bool, db::cell_index_type> ci = layout.cell_by_name (child.c_str ());
  if (! ci.first) {
    throw tl::Exception ("no cell named " + child);
  }
  db::Vector disp;
  double angle = 0.0, mag = 1.0;
  bool mirror = false;
  if (given (v, "transform")) {
    val t = v["transform"];
    disp = to_vector (field (t, "displacement"));
    angle = given (t, "angle") ? to_f64 (t["angle"], "angle") : 0.0;
    mirror = given (t, "mirror") && to_bool (t["mirror"], "mirror");
    mag = given (t, "magnification") ? to_f64 (t["magnification"], "magnification") : 1.0;
  }
  db::ICplxTrans t (mag, angle, mirror, disp);
  db::CellInst cell (ci.second);
  if (given (v, "array")) {
    val array = v["array"];
    db::Vector a = to_vector (field (array, "a"));
    db::Vector b = to_vector (field (array, "b"));
    int32_t na = to_i32 (field (array, "na"), "na");
    int32_t nb = to_i32 (field (array, "nb"), "nb");
    if (na < 1 || nb < 1) {
      throw tl::Exception ("na, nb: expected at least 1");
    }
    if (t.is_complex ()) {
      return db::CellInstArray (cell, t, a, b, (unsigned long) na, (unsigned long) nb);
    }
    return db::CellInstArray (cell, db::Trans (t), a, b, (unsigned long) na, (unsigned long) nb);
  }
  if (t.is_complex ()) {
    return db::CellInstArray (cell, t);
  }
  return db::CellInstArray (cell, db::Trans (t));
}

unsigned int to_buttons (const val &v)
{
  // lay::ShiftButton, ControlButton, AltButton, LeftButton, MidButton, RightButton.
  static const char *const names[] = { "shift", "control", "alt", "left", "middle", "right" };
  if (! is_object (v)) {
    throw tl::Exception ("buttons: expected an object");
  }
  unsigned int bits = 0;
  for (unsigned int i = 0; i < 6; ++i) {
    if (given (v, names[i]) && to_bool (v[names[i]], names[i])) {
      bits |= 1u << i;
    }
  }
  return bits;
}

unsigned int to_qt_key (const val &v)
{
  static const char *const names[] = { "escape", "tab", "backspace", "enter", "delete", "left", "up", "right", "down" };
  static const unsigned int codes[] = { 0x01000000, 0x01000001, 0x01000003, 0x01000004, 0x01000007,
                                        0x01000012, 0x01000013, 0x01000014, 0x01000015 };
  if (v.isString ()) {
    return codes[pick (names, v, "key")];
  }
  std::string c = to_string (field (v, "char"), "char");
  if (c.size () != 1) {
    throw tl::Exception ("char: expected one ASCII character");
  }
  // Qt codes letters by their upper case.
  unsigned char ch = (unsigned char) c[0];
  return (ch >= 'a' && ch <= 'z') ? ch - 'a' + 'A' : ch;
}

std::string color (tl::color_t argb)
{
  char s[8];
  std::snprintf (s, sizeof (s), "#%06x", unsigned (argb & 0xffffff));
  return s;
}

}
