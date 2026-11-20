"""SHRAPNEL logo options.
A  BLAST    family style (spiky metal, black + red outline) with the word blown apart along fracture lines
B  STENCIL  military stencil on armour plate, punched with shrapnel holes that glow red-hot
C  SPLIT    heavy condensed letters sliced into horizontal bands (the spectrum) that are thrown sideways,
            the right end breaking into flying fragments
usage: python make_shrapnel.py A|B|C out.svg"""
import math, random, sys
sys.path.insert(0, '.')
from fontTools.ttLib import TTFont
from shapely.geometry import Polygon, MultiPolygon, box, Point, LineString
from shapely.ops import unary_union, split
from shapely import affinity
from helpers import glyph_shape, sharpen, blade

MODE = sys.argv[1]; OUT = sys.argv[2]
random.seed({'A': 11, 'B': 5, 'C': 9}[MODE])
FONT = {'A': 'racing-sans-one-latin-400-normal.ttf', 'B': 'black-ops-one-latin-400-normal.ttf', 'C': 'saira-condensed-latin-900-normal.ttf'}[MODE]
TEXT = {'A': 'ShrapneL', 'B': 'SHRAPNEL', 'C': 'SHRAPNEL'}[MODE]
font = TTFont(FONT); upm = font['head'].unitsPerEm
caps = font['OS/2'].sCapHeight or int(upm * 0.7)

BIG = {0: 1.3, 7: 1.3} if MODE == 'A' else {}
x = 0.0; letters = []
for i, ch in enumerate(TEXT):
    shp, adv = glyph_shape(font, ch)
    s = BIG.get(i, 1.0)
    shp = affinity.scale(shp, s, s, origin=(0, 0))
    if i in BIG: shp = affinity.translate(shp, 0, -caps * 0.12)
    shp = affinity.translate(shp, x, 0)
    letters.append(shp)
    x += adv * s * ({'A': 0.93, 'B': 0.98, 'C': 0.97}[MODE])
word = unary_union(letters)
if MODE == 'A': word = affinity.skew(word, xs=-12, origin=(0, 0))
if MODE == 'C': word = affinity.skew(word, xs=-8, origin=(0, 0))
word = word.simplify(upm * 0.003)
minx, miny, maxx, maxy = word.bounds
h = maxy - miny; w = maxx - minx

def pieces(g):
    return list(g.geoms) if isinstance(g, MultiPolygon) else ([g] if not g.is_empty else [])

fragments = []      # small flying shards
hot = []            # red-hot areas (hole rims, cut faces)

if MODE == 'A':
    word = sharpen(word, upm * 0.15)
    extra = [
        blade((minx + h * 0.10, miny + h * 0.12), (minx + h * 0.30, miny + h * 0.02), (minx - h * 0.30, miny - h * 0.12)),
        blade((maxx - h * 0.10, miny + h * 0.28), (maxx - h * 0.02, miny + h * 0.04), (maxx + h * 0.30, miny - h * 0.10)),
    ]
    word = unary_union([word] + extra)
    # blow it apart: three fracture lines, the pieces pushed away from the blast point (right of centre)
    cuts = [((minx + w * 0.30, maxy + h), (minx + w * 0.40, miny - h)),
            ((minx + w * 0.58, maxy + h), (minx + w * 0.52, miny - h)),
            ((minx + w * 0.78, maxy + h), (minx + w * 0.86, miny - h))]
    parts = [word]
    for a, b in cuts:
        nxt = []
        for p in parts:
            for q in pieces(p):
                try: nxt += pieces(unary_union(split(q, LineString([a, b]))))
                except Exception: nxt.append(q)
        parts = nxt
    blast = (minx + w * 0.68, miny + h * 0.45)
    moved = []
    for p in parts:
        c = p.centroid; dx, dy = c.x - blast[0], c.y - blast[1]; d = math.hypot(dx, dy) or 1
        k = upm * 0.075 * (0.6 + random.random())
        moved.append(affinity.rotate(affinity.translate(p, dx / d * k, dy / d * k * 0.6), random.uniform(-4, 4), origin='centroid'))
    word = unary_union(moved)
    for i in range(22):        # shards flying out to the right and up
        a = random.uniform(-1.1, 1.0); r = random.uniform(h * 0.25, h * 1.0)
        cx = maxx - w * 0.12 + math.cos(a) * r; cy = miny + h * 0.5 + math.sin(a) * r * 0.9
        sz = random.uniform(0.06, 0.16) * h
        tri = Polygon([(cx, cy), (cx + sz * random.uniform(.6, 1.6), cy + sz * random.uniform(-.4, .6)), (cx + sz * random.uniform(-.2, .5), cy - sz * random.uniform(.5, 1.2))])
        fragments.append(affinity.rotate(tri, random.uniform(0, 360), origin='centroid'))

if MODE == 'B':
    # shrapnel holes punched through, torn edges; small fragments knocked out
    holes = []
    for i in range(30):
        if len(holes) >= 9: break
        cx = minx + random.uniform(0.04, 0.96) * w; cy = miny + random.uniform(0.12, 0.88) * h
        if not word.contains(Point(cx, cy)): continue
        r = random.uniform(0.06, 0.11) * h
        pts = [(cx + math.cos(t) * r * random.uniform(0.6, 1.25), cy + math.sin(t) * r * random.uniform(0.6, 1.25)) for t in [j / 11 * 2 * math.pi for j in range(11)]]
        holes.append(Polygon(pts).buffer(0))
    hole_u = unary_union(holes)
    hot = [hole_u.buffer(upm * 0.03).intersection(word).difference(hole_u)]
    word = word.difference(hole_u)
    for i in range(10):
        cx = maxx + random.uniform(0.02, 0.28) * h; cy = miny + random.uniform(0.0, 1.0) * h
        sz = random.uniform(0.03, 0.07) * h
        fragments.append(affinity.rotate(Polygon([(cx, cy), (cx + sz, cy + sz * .4), (cx + sz * .3, cy - sz)]), random.uniform(0, 360), origin='centroid'))

if MODE == 'C':
    # horizontal bands (the spectrum) shoved sideways; the right end breaks into fragments
    n = 7
    bands = []
    for i in range(n):
        y0 = miny + h * i / n; y1 = miny + h * (i + 1) / n
        band = word.intersection(box(minx - 10, y0 + upm * 0.004, maxx + 10, y1 - upm * 0.004))
        shift = (math.sin(i * 2.1 + 0.7)) * h * 0.11
        bands.append(affinity.translate(band, shift, 0))
    word = unary_union([q for b in bands for q in pieces(b) if q.area > upm * upm * 0.0015])
    cutx = minx + w * 0.80
    right = word.intersection(box(cutx, miny - h, maxx + h, maxy + h))
    word = word.difference(box(cutx, miny - h, maxx + h, maxy + h))
    # break the right end into chunks that fly off
    grid = []
    for i in range(6):
        for j in range(4):
            gx0 = cutx + (maxx - cutx) * i / 6; gy0 = miny + h * j / 4
            cell = Polygon([(gx0 + random.uniform(-8, 8) * upm / 1000, gy0), (gx0 + (maxx - cutx) / 6, gy0 + random.uniform(-10, 10) * upm / 1000),
                            (gx0 + (maxx - cutx) / 6, gy0 + h / 4), (gx0, gy0 + h / 4 + random.uniform(-10, 10) * upm / 1000)])
            grid.append(cell)
    for cell in grid:
        for q in pieces(right.intersection(cell)):
            if q.area < upm * upm * 0.0002: continue
            c = q.centroid
            t = (c.x - cutx) / (maxx - cutx)
            k = h * (0.05 + 0.55 * t ** 1.6) * random.uniform(0.7, 1.2)
            ang = random.uniform(-0.6, 0.6)
            fragments.append(affinity.rotate(affinity.translate(q, k * math.cos(ang), k * math.sin(ang)), random.uniform(-25, 25) * t, origin='centroid'))
    hot = [q for q in pieces(word.intersection(box(cutx - upm * 0.035, miny - h, cutx, maxy + h))) if q.area > upm * upm * 0.0006]

frag_u = unary_union(fragments) if fragments else Polygon()
allshape = unary_union([word, frag_u])
outline_in = {'A': 0.045, 'B': 0.035, 'C': 0.035}[MODE]
inner = allshape.buffer(upm * outline_in, join_style=2, mitre_limit=6)
outer = allshape.buffer(upm * (outline_in + 0.035), join_style=2, mitre_limit=6)
if MODE == 'A': outer = sharpen(outer.simplify(upm * 0.004), upm * 0.1, 120)

bx0, by0, bx1, by1 = outer.bounds
pad = upm * 0.06
W, H = bx1 - bx0 + 2 * pad, by1 - by0 + 2 * pad

def to_path(g):
    d = []
    for p in pieces(g) if not isinstance(g, Polygon) else [g]:
        if p.is_empty: continue
        for r in [p.exterior] + list(p.interiors):
            pts = [(cx - bx0 + pad, by1 - cy + pad) for cx, cy in r.coords]
            d.append("M" + " L".join(f"{a:.1f} {b:.1f}" for a, b in pts) + " Z")
    return " ".join(d)

hot_u = unary_union(hot) if hot else Polygon()
metal = {'A': ('#ffffff', '#e9eaed', '#b9bcc2', '#f4f5f7'), 'B': ('#c9ccd1', '#9fa3aa', '#6d7179', '#a9adb4'), 'C': ('#ffffff', '#e4e5e8', '#a9adb4', '#eef0f2')}[MODE]
svg = f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W:.0f} {H:.0f}" width="{W:.0f}" height="{H:.0f}">
<defs>
  <linearGradient id="metal" x1="0" y1="0" x2="0" y2="1">
    <stop offset="0" stop-color="{metal[0]}"/><stop offset=".45" stop-color="{metal[1]}"/><stop offset=".62" stop-color="{metal[2]}"/><stop offset="1" stop-color="{metal[3]}"/>
  </linearGradient>
  <radialGradient id="heat" cx=".5" cy=".5" r=".6"><stop offset="0" stop-color="#ffd27a"/><stop offset=".4" stop-color="#ff7138"/><stop offset="1" stop-color="#d0141c"/></radialGradient>
  <filter id="grunge" x="0" y="0" width="100%" height="100%">
    <feTurbulence type="fractalNoise" baseFrequency="0.012 0.05" numOctaves="4" seed="4" result="n"/>
    <feColorMatrix in="n" type="matrix" values="0 0 0 0 0  0 0 0 0 0  0 0 0 0 0  0 0 0 -4.2 1.75" result="spots"/>
    <feTurbulence type="turbulence" baseFrequency="0.9 0.02" numOctaves="2" seed="7" result="s"/>
    <feColorMatrix in="s" type="matrix" values="0 0 0 0 0  0 0 0 0 0  0 0 0 0 0  0 0 0 -5 2.2" result="scratch"/>
    <feComposite in="spots" in2="scratch" operator="arithmetic" k1="0" k2=".45" k3=".3" k4="0" result="dirt"/>
    <feComposite in="dirt" in2="SourceAlpha" operator="in"/>
  </filter>
  <filter id="bevel" x="-5%" y="-5%" width="110%" height="110%">
    <feGaussianBlur in="SourceAlpha" stdDeviation="{upm * 0.01:.1f}" result="b"/>
    <feOffset in="b" dx="0" dy="{-upm * 0.01:.1f}" result="o"/>
    <feComposite in="SourceAlpha" in2="o" operator="out" result="edge"/>
    <feFlood flood-color="#3a3d44" flood-opacity=".75"/><feComposite in2="edge" operator="in"/>
  </filter>
  <filter id="glow" x="-10%" y="-30%" width="120%" height="160%"><feGaussianBlur stdDeviation="{upm * 0.02:.1f}"/></filter>
  <filter id="hotglow" x="-20%" y="-20%" width="140%" height="140%"><feGaussianBlur stdDeviation="{upm * 0.012:.1f}"/></filter>
</defs>
<path d="{to_path(outer)}" fill="#ff1f2a" opacity=".35" filter="url(#glow)"/>
<path d="{to_path(outer)}" fill="#ff1f2a"/>
<path d="{to_path(outer.buffer(-upm * 0.012))}" fill="#c8101a"/>
<path d="{to_path(inner)}" fill="#0a0a0c"/>
<path d="{to_path(allshape)}" fill="url(#metal)"/>
<path d="{to_path(allshape)}" fill="#2a2c31" filter="url(#grunge)" opacity="{'.75' if MODE == 'B' else '.55'}"/>
<path d="{to_path(allshape)}" fill="#000" filter="url(#bevel)"/>
<path d="{to_path(hot_u)}" fill="#ff5a28" filter="url(#hotglow)" opacity=".9"/>
<path d="{to_path(hot_u)}" fill="url(#heat)"/>
<path d="{to_path(allshape)}" fill="none" stroke="#ffffff" stroke-opacity=".45" stroke-width="{upm * 0.005:.1f}"/>
</svg>'''
open(OUT, 'w').write(svg)
print(OUT, f'{W:.0f}x{H:.0f}')
