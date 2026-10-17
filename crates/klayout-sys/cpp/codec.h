// The binary encoding shared with src/codec.rs. Structured values (shapes,
// instances, cells, layers, edits) cross the C ABI as little-endian byte
// strings: i32/u32/u64/f64 as their bytes, bool as one byte, strings as a
// u32 length and UTF-8 bytes, lists as a u32 count and their items. The
// layout of each value is documented once, in src/codec.rs.

#pragma once

#include "dbBox.h"
#include "dbEdge.h"
#include "dbEdgePair.h"
#include "dbHersheyFont.h"
#include "dbInstances.h"
#include "dbLayout.h"
#include "dbPath.h"
#include "dbPolygon.h"
#include "dbShape.h"
#include "dbShapes.h"
#include "dbText.h"
#include "tlException.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace kl
{

class Writer
{
public:
  explicit Writer (std::vector<uint8_t> &out) : m_out (out) { }

  void u8 (uint8_t v) { m_out.push_back (v); }
  void boolean (bool v) { u8 (v ? 1 : 0); }
  void i32 (int32_t v) { raw (&v, sizeof (v)); }
  void u32 (uint32_t v) { raw (&v, sizeof (v)); }
  void u64 (uint64_t v) { raw (&v, sizeof (v)); }
  void f64 (double v) { raw (&v, sizeof (v)); }

  // A list whose length is only known after writing it: reserve() writes a
  // placeholder count, set_count() fills it in.
  size_t reserve () { size_t at = m_out.size (); u32 (0); return at; }
  void set_count (size_t at, uint32_t n) { std::memcpy (m_out.data () + at, &n, sizeof (n)); }

  void str (const std::string &s)
  {
    u32 (uint32_t (s.size ()));
    raw (s.data (), s.size ());
  }

  void point (const db::Point &p)
  {
    i32 (p.x ());
    i32 (p.y ());
  }

  void box (const db::Box &b)
  {
    point (b.p1 ());
    point (b.p2 ());
  }

  void edge (const db::Edge &e)
  {
    point (e.p1 ());
    point (e.p2 ());
  }

  template <class Iter>
  void points (Iter from, Iter to, size_t n)
  {
    u32 (uint32_t (n));
    for (Iter i = from; i != to; ++i) {
      point (*i);
    }
  }

  void layer (const db::LayerProperties &lp)
  {
    i32 (lp.layer);
    i32 (lp.datatype);
  }

  // Shape: u8 kind, then
  //   0 box: box | 1 polygon: hull points, u32 holes, hole points... |
  //   2 path: i32 width, i32 begin ext, i32 end ext, bool round, points |
  //   3 text: str, u8 orientation, point, i32 size, i32 font, i32 halign, i32 valign |
  //   4 edge: edge | 5 edge pair: edge, edge | 6 point: point
  // Returns false (and writes nothing) for user objects, which have no
  // portable form.
  bool shape (const db::Shape &s)
  {
    if (s.is_box ()) {
      u8 (0);
      box (s.box ());
    } else if (s.is_polygon () || s.is_simple_polygon ()) {
      db::Polygon p;
      s.polygon (p);
      u8 (1);
      points (p.begin_hull (), p.end_hull (), p.hull ().size ());
      u32 (uint32_t (p.holes ()));
      for (unsigned int h = 0; h < p.holes (); ++h) {
        points (p.begin_hole (h), p.end_hole (h), p.hole (h).size ());
      }
    } else if (s.is_path ()) {
      db::Path p;
      s.path (p);
      u8 (2);
      i32 (p.width ());
      i32 (p.bgn_ext ());
      i32 (p.end_ext ());
      boolean (p.round ());
      points (p.begin (), p.end (), p.points ());
    } else if (s.is_text ()) {
      db::Text t;
      s.text (t);
      u8 (3);
      str (t.string ());
      u8 (uint8_t (t.trans ().fp_trans ().rot ()));
      point (db::Point () + t.trans ().disp ());
      i32 (t.size ());
      i32 (int32_t (t.font ()));
      i32 (int32_t (t.halign ()));
      i32 (int32_t (t.valign ()));
    } else if (s.is_edge ()) {
      u8 (4);
      edge (s.edge ());
    } else if (s.is_edge_pair ()) {
      u8 (5);
      edge (s.edge_pair ().first ());
      edge (s.edge_pair ().second ());
    } else if (s.is_point ()) {
      u8 (6);
      point (s.point ());
    } else {
      return false;
    }
    return true;
  }

  // Instance: str child cell, point displacement, f64 angle (degrees),
  // bool mirror, f64 magnification, bool array, then for an array:
  // point a, point b, u64 na, u64 nb.
  void instance (const db::Layout &layout, const db::CellInstArray &inst)
  {
    str (layout.cell_name (inst.object ().cell_index ()));
    db::ICplxTrans t = inst.complex_trans ();
    point (db::Point () + t.disp ());
    f64 (t.angle ());
    boolean (t.is_mirror ());
    f64 (t.mag ());
    db::Vector a, b;
    unsigned long na = 1, nb = 1;
    bool array = inst.is_regular_array (a, b, na, nb);
    boolean (array);
    if (array) {
      point (db::Point () + a);
      point (db::Point () + b);
      u64 (na);
      u64 (nb);
    }
  }

private:
  void raw (const void *p, size_t n)
  {
    const uint8_t *b = static_cast<const uint8_t *> (p);
    m_out.insert (m_out.end (), b, b + n);
  }

  std::vector<uint8_t> &m_out;
};

class Reader
{
public:
  Reader (const uint8_t *data, size_t len) : m_p (data), m_end (data + len) { }

  bool at_end () const { return m_p == m_end; }

  uint8_t u8 () { uint8_t v; raw (&v, sizeof (v)); return v; }
  bool boolean () { return u8 () != 0; }
  int32_t i32 () { int32_t v; raw (&v, sizeof (v)); return v; }
  uint32_t u32 () { uint32_t v; raw (&v, sizeof (v)); return v; }
  uint64_t u64 () { uint64_t v; raw (&v, sizeof (v)); return v; }
  double f64 () { double v; raw (&v, sizeof (v)); return v; }

  std::string str ()
  {
    uint32_t n = u32 ();
    need (n);
    std::string s (reinterpret_cast<const char *> (m_p), n);
    m_p += n;
    return s;
  }

  db::Point point ()
  {
    int32_t x = i32 ();
    return db::Point (x, i32 ());
  }

  db::Box box ()
  {
    db::Point p1 = point ();
    return db::Box (p1, point ());
  }

  db::Edge edge ()
  {
    db::Point p1 = point ();
    return db::Edge (p1, point ());
  }

  std::vector<db::Point> points ()
  {
    uint32_t n = u32 ();
    std::vector<db::Point> pts;
    pts.reserve (n);
    for (uint32_t i = 0; i < n; ++i) {
      pts.push_back (point ());
    }
    return pts;
  }

  db::LayerProperties layer ()
  {
    int32_t l = i32 ();
    return db::LayerProperties (l, i32 ());
  }

  // Reads a shape (see Writer::shape) and inserts it into `target`.
  db::Shape insert_shape (db::Shapes &target)
  {
    switch (u8 ()) {
      case 0:
        return target.insert (box ());
      case 1: {
        db::Polygon p;
        std::vector<db::Point> hull = points ();
        p.assign_hull (hull.begin (), hull.end ());
        uint32_t holes = u32 ();
        for (uint32_t h = 0; h < holes; ++h) {
          std::vector<db::Point> hole = points ();
          p.insert_hole (hole.begin (), hole.end ());
        }
        return target.insert (p);
      }
      case 2: {
        db::Coord width = i32 ();
        db::Coord bgn_ext = i32 ();
        db::Coord end_ext = i32 ();
        bool round = boolean ();
        std::vector<db::Point> pts = points ();
        return target.insert (db::Path (pts.begin (), pts.end (), width, bgn_ext, end_ext, round));
      }
      case 3: {
        std::string s = str ();
        uint8_t orientation = u8 ();
        if (orientation > 7) {
          throw tl::Exception ("text orientation out of range");
        }
        db::Point at = point ();
        db::Coord size = i32 ();
        int32_t font = i32 ();
        int32_t halign = i32 ();
        int32_t valign = i32 ();
        db::Trans t (int (orientation), at - db::Point ());
        return target.insert (db::Text (s, t, size, db::Font (font), db::HAlign (halign), db::VAlign (valign)));
      }
      case 4:
        return target.insert (edge ());
      case 5: {
        db::Edge first = edge ();
        return target.insert (db::EdgePair (first, edge ()));
      }
      case 6:
        return target.insert (point ());
      default:
        throw tl::Exception ("unknown shape kind");
    }
  }

  // Reads an instance (see Writer::instance), resolving the child cell by
  // name in `layout`.
  db::CellInstArray instance (const db::Layout &layout)
  {
    std::string child = str ();
    std::pair<bool, db::cell_index_type> ci = layout.cell_by_name (child.c_str ());
    if (! ci.first) {
      throw tl::Exception ("no cell named " + child);
    }
    db::Vector disp = point () - db::Point ();
    double angle = f64 ();
    bool mirror = boolean ();
    double mag = f64 ();
    db::ICplxTrans t (mag, angle, mirror, disp);
    db::CellInst cell (ci.second);
    if (boolean ()) {
      db::Vector a = point () - db::Point ();
      db::Vector b = point () - db::Point ();
      unsigned long na = (unsigned long) u64 ();
      unsigned long nb = (unsigned long) u64 ();
      if (t.is_complex ()) {
        return db::CellInstArray (cell, t, a, b, na, nb);
      }
      return db::CellInstArray (cell, db::Trans (t), a, b, na, nb);
    }
    if (t.is_complex ()) {
      return db::CellInstArray (cell, t);
    }
    return db::CellInstArray (cell, db::Trans (t));
  }

private:
  void need (size_t n)
  {
    if (size_t (m_end - m_p) < n) {
      throw tl::Exception ("truncated message from Rust");
    }
  }

  void raw (void *v, size_t n)
  {
    need (n);
    std::memcpy (v, m_p, n);
    m_p += n;
  }

  const uint8_t *m_p, *m_end;
};

}
