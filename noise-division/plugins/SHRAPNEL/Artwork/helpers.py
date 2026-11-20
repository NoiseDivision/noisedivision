import math, random
from fontTools.pens.basePen import BasePen
from shapely.geometry import Polygon, MultiPolygon
from shapely.ops import unary_union
class PolyPen(BasePen):
    """collects glyph contours as flattened point lists"""
    def __init__(self, gs):
        super().__init__(gs); self.contours = []; self.cur = []
    def _moveTo(self, p): self.cur = [p]
    def _lineTo(self, p): self.cur.append(p)
    def _curveToOne(self, p1, p2, p3):
        p0 = self.cur[-1]
        for i in range(1, 9):
            t = i / 8; u = 1 - t
            self.cur.append((u**3*p0[0] + 3*u*u*t*p1[0] + 3*u*t*t*p2[0] + t**3*p3[0],
                             u**3*p0[1] + 3*u*u*t*p1[1] + 3*u*t*t*p2[1] + t**3*p3[1]))
    def _qCurveToOne(self, p1, p2):
        p0 = self.cur[-1]
        for i in range(1, 7):
            t = i / 6; u = 1 - t
            self.cur.append((u*u*p0[0] + 2*u*t*p1[0] + t*t*p2[0], u*u*p0[1] + 2*u*t*p1[1] + t*t*p2[1]))
    def _closePath(self):
        if len(self.cur) > 2: self.contours.append(self.cur)
        self.cur = []
    _endPath = _closePath


def glyph_shape(font, ch):
    gs = font.getGlyphSet(); name = font.getBestCmap()[ord(ch)]
    pen = PolyPen(gs); gs[name].draw(pen)
    # even-odd style: biggest rings are outer shapes, rings inside them are holes
    polys = [Polygon(c).buffer(0) for c in pen.contours]
    polys.sort(key=lambda p: -p.area)
    shape = None
    for p in polys:
        if shape is None: shape = p
        elif shape.contains(p.representative_point()): shape = shape.difference(p)
        else: shape = shape.union(p)
    return shape, gs[name].width


def sharpen(poly, max_len, angle_limit=115):
    """pull convex corners outward into spikes"""
    def ring(coords, outward=True):
        pts = list(coords)[:-1]; n = len(pts); res = []
        for i in range(n):
            a, b, c = pts[i - 1], pts[i], pts[(i + 1) % n]
            v1 = (a[0] - b[0], a[1] - b[1]); v2 = (c[0] - b[0], c[1] - b[1])
            l1, l2 = math.hypot(*v1), math.hypot(*v2)
            if l1 < 1e-6 or l2 < 1e-6: res.append(b); continue
            cosang = max(-1, min(1, (v1[0]*v2[0] + v1[1]*v2[1]) / (l1 * l2)))
            ang = math.degrees(math.acos(cosang))
            cross = v1[0]*v2[1] - v1[1]*v2[0]
            convex = cross < 0 if outward else cross > 0
            if convex and ang < angle_limit:
                bis = (-(v1[0]/l1 + v2[0]/l2), -(v1[1]/l1 + v2[1]/l2)); bl = math.hypot(*bis) or 1
                k = max_len * (1 - ang / angle_limit) ** 0.8 * random.uniform(0.55, 1.0)
                res.append((b[0] + bis[0]/bl*k, b[1] + bis[1]/bl*k))
            else:
                res.append(b)
        return res
    if poly.is_empty: return poly
    geoms = poly.geoms if isinstance(poly, MultiPolygon) else [poly]
    out = []
    for g in geoms:
        g = Polygon(g.exterior.coords[::-1] if g.exterior.is_ccw else g.exterior.coords, [r.coords for r in g.interiors])
        ext = ring(g.exterior.coords, True)
        out.append(Polygon(ext, [list(r.coords)[:-1] for r in g.interiors]).buffer(0))
    return unary_union(out)


def blade(base_a, base_b, tip):
    return Polygon([base_a, tip, base_b])


