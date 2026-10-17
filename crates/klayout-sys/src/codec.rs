//! The Rust half of the byte encoding in `cpp/codec.h`, which documents
//! the layout of each value. Both sides run in the same WebAssembly
//! module, so numbers are little-endian and nothing is versioned.

use crate::Error;
use crate::model::*;

pub(crate) struct Reader<'a>(&'a [u8]);

fn malformed() -> Error {
    Error("malformed data from KLayout".into())
}

impl<'a> Reader<'a> {
    pub fn new(data: &'a [u8]) -> Self {
        Self(data)
    }

    fn take<const N: usize>(&mut self) -> Result<[u8; N], Error> {
        let (head, rest) = self.0.split_first_chunk::<N>().ok_or_else(malformed)?;
        self.0 = rest;
        Ok(*head)
    }

    pub fn u8(&mut self) -> Result<u8, Error> {
        Ok(self.take::<1>()?[0])
    }

    pub fn bool(&mut self) -> Result<bool, Error> {
        Ok(self.u8()? != 0)
    }

    pub fn i32(&mut self) -> Result<i32, Error> {
        self.take().map(i32::from_le_bytes)
    }

    pub fn u32(&mut self) -> Result<u32, Error> {
        self.take().map(u32::from_le_bytes)
    }

    pub fn u64(&mut self) -> Result<u64, Error> {
        self.take().map(u64::from_le_bytes)
    }

    pub fn f64(&mut self) -> Result<f64, Error> {
        self.take().map(f64::from_le_bytes)
    }

    pub fn str(&mut self) -> Result<String, Error> {
        let n = self.u32()? as usize;
        if self.0.len() < n {
            return Err(malformed());
        }
        let (s, rest) = self.0.split_at(n);
        self.0 = rest;
        String::from_utf8(s.to_vec()).map_err(|_| malformed())
    }

    pub fn list<T>(&mut self, mut item: impl FnMut(&mut Self) -> Result<T, Error>) -> Result<Vec<T>, Error> {
        let n = self.u32()?;
        (0..n).map(|_| item(self)).collect()
    }

    pub fn point(&mut self) -> Result<Point, Error> {
        Ok(Point { x: self.i32()?, y: self.i32()? })
    }

    pub fn vector(&mut self) -> Result<Vector, Error> {
        let Point { x, y } = self.point()?;
        Ok(Vector { x, y })
    }

    pub fn rect(&mut self) -> Result<Rect, Error> {
        let (p1, p2) = (self.point()?, self.point()?);
        Ok(Rect { left: p1.x, bottom: p1.y, right: p2.x, top: p2.y })
    }

    pub fn edge(&mut self) -> Result<Edge, Error> {
        Ok(Edge { from: self.point()?, to: self.point()? })
    }

    pub fn points(&mut self) -> Result<Vec<Point>, Error> {
        self.list(Self::point)
    }

    pub fn layer(&mut self) -> Result<LayerSpec, Error> {
        Ok(LayerSpec { layer: self.i32()?, datatype: self.i32()? })
    }

    pub fn shape(&mut self) -> Result<Shape, Error> {
        Ok(match self.u8()? {
            0 => Shape::Box(self.rect()?),
            1 => Shape::Polygon(Polygon { hull: self.points()?, holes: self.list(Self::points)? }),
            2 => {
                let (width, begin_extension, end_extension, round) = (self.i32()?, self.i32()?, self.i32()?, self.bool()?);
                Shape::Path(Path { points: self.points()?, width, begin_extension, end_extension, round })
            }
            3 => Shape::Text(Text {
                string: self.str()?,
                orientation: Orientation::from_code(self.u8()?).ok_or_else(malformed)?,
                position: self.point()?,
                size: self.i32()?,
                font: u32::try_from(self.i32()?).ok(),
                halign: match self.i32()? {
                    0 => Some(HAlign::Left),
                    1 => Some(HAlign::Center),
                    2 => Some(HAlign::Right),
                    _ => None,
                },
                valign: match self.i32()? {
                    0 => Some(VAlign::Top),
                    1 => Some(VAlign::Center),
                    2 => Some(VAlign::Bottom),
                    _ => None,
                },
            }),
            4 => Shape::Edge(self.edge()?),
            5 => Shape::EdgePair { first: self.edge()?, second: self.edge()? },
            6 => Shape::Point(self.point()?),
            _ => return Err(malformed()),
        })
    }

    pub fn instance(&mut self) -> Result<Instance, Error> {
        let cell = self.str()?;
        let transform = Transform {
            displacement: self.vector()?,
            angle: self.f64()?,
            mirror: self.bool()?,
            magnification: self.f64()?,
        };
        let array = if self.bool()? {
            Some(Array { a: self.vector()?, b: self.vector()?, na: self.u64()?, nb: self.u64()? })
        } else {
            None
        };
        Ok(Instance { cell, transform, array })
    }

    pub fn finish(self) -> Result<(), Error> {
        if self.0.is_empty() { Ok(()) } else { Err(malformed()) }
    }
}

#[derive(Default)]
pub(crate) struct Writer(pub Vec<u8>);

impl Writer {
    fn u8(&mut self, v: u8) {
        self.0.push(v);
    }

    fn bool(&mut self, v: bool) {
        self.u8(u8::from(v));
    }

    fn i32(&mut self, v: i32) {
        self.0.extend_from_slice(&v.to_le_bytes());
    }

    fn u32(&mut self, v: u32) {
        self.0.extend_from_slice(&v.to_le_bytes());
    }

    fn u64(&mut self, v: u64) {
        self.0.extend_from_slice(&v.to_le_bytes());
    }

    fn f64(&mut self, v: f64) {
        self.0.extend_from_slice(&v.to_le_bytes());
    }

    fn len(&mut self, n: usize) -> Result<(), Error> {
        self.u32(u32::try_from(n).map_err(|_| Error("list too long".into()))?);
        Ok(())
    }

    fn str(&mut self, s: &str) -> Result<(), Error> {
        self.len(s.len())?;
        self.0.extend_from_slice(s.as_bytes());
        Ok(())
    }

    fn point(&mut self, p: Point) {
        self.i32(p.x);
        self.i32(p.y);
    }

    fn vector(&mut self, v: Vector) {
        self.point(Point { x: v.x, y: v.y });
    }

    fn edge(&mut self, e: Edge) {
        self.point(e.from);
        self.point(e.to);
    }

    fn points(&mut self, pts: &[Point]) -> Result<(), Error> {
        self.len(pts.len())?;
        pts.iter().for_each(|&p| self.point(p));
        Ok(())
    }

    fn layer(&mut self, l: LayerSpec) {
        self.i32(l.layer);
        self.i32(l.datatype);
    }

    fn shape(&mut self, shape: &Shape) -> Result<(), Error> {
        match shape {
            Shape::Box(r) => {
                self.u8(0);
                self.point(Point { x: r.left, y: r.bottom });
                self.point(Point { x: r.right, y: r.top });
            }
            Shape::Polygon(p) => {
                self.u8(1);
                self.points(&p.hull)?;
                self.len(p.holes.len())?;
                for hole in &p.holes {
                    self.points(hole)?;
                }
            }
            Shape::Path(p) => {
                self.u8(2);
                self.i32(p.width);
                self.i32(p.begin_extension);
                self.i32(p.end_extension);
                self.bool(p.round);
                self.points(&p.points)?;
            }
            Shape::Text(t) => {
                self.u8(3);
                self.str(&t.string)?;
                self.u8(t.orientation as u8);
                self.point(t.position);
                self.i32(t.size);
                self.i32(t.font.and_then(|f| i32::try_from(f).ok()).unwrap_or(-1));
                self.i32(t.halign.map_or(-1, |a| a as i32));
                self.i32(t.valign.map_or(-1, |a| a as i32));
            }
            Shape::Edge(e) => {
                self.u8(4);
                self.edge(*e);
            }
            Shape::EdgePair { first, second } => {
                self.u8(5);
                self.edge(*first);
                self.edge(*second);
            }
            Shape::Point(p) => {
                self.u8(6);
                self.point(*p);
            }
        }
        Ok(())
    }

    fn instance(&mut self, inst: &Instance) -> Result<(), Error> {
        self.str(&inst.cell)?;
        let t = inst.transform;
        self.vector(t.displacement);
        self.f64(t.angle);
        self.bool(t.mirror);
        self.f64(t.magnification);
        self.bool(inst.array.is_some());
        if let Some(a) = inst.array {
            self.vector(a.a);
            self.vector(a.b);
            self.u64(a.na);
            self.u64(a.nb);
        }
        Ok(())
    }

    pub fn edit(&mut self, edit: &Edit) -> Result<(), Error> {
        match edit {
            Edit::InsertShape { cell, layer, shape } | Edit::EraseShape { cell, layer, shape } => {
                self.u8(if matches!(edit, Edit::InsertShape { .. }) { 0 } else { 1 });
                self.str(cell)?;
                self.layer(*layer);
                self.shape(shape)
            }
            Edit::InsertInstance { cell, instance } | Edit::EraseInstance { cell, instance } => {
                self.u8(if matches!(edit, Edit::InsertInstance { .. }) { 2 } else { 3 });
                self.str(cell)?;
                self.instance(instance)
            }
            Edit::CreateCell { name } => {
                self.u8(4);
                self.str(name)
            }
            Edit::DeleteCell { name } => {
                self.u8(5);
                self.str(name)
            }
            Edit::RenameCell { name, new_name } => {
                self.u8(6);
                self.str(name)?;
                self.str(new_name)
            }
        }
    }
}
