# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) Super Melee 64 contributors
"""Author the native Smash Royale title as beveled geometry (no raster overlay).

Run with fonttools, shapely, numpy and mapbox-earcut installed. The generated
mesh is committed; players/builders need none of those packages or font files.
Only new glyph outlines are emitted, never the disc's logo assets.

Font files are not included. Defaults refer to locally installed macOS fonts;
use --big-font and --three-d-font with fonts you are licensed to use. The
original generator's font-derived geometry and colors are retained; review the
font's terms before redistributing regenerated lettering.
"""
from pathlib import Path
import argparse
import numpy as np
import mapbox_earcut as earcut
from fontTools.ttLib import TTFont
from fontTools.pens.basePen import BasePen
from shapely.geometry import Polygon
from shapely.ops import unary_union

ROOT=Path(__file__).resolve().parents[3]

class Outline(BasePen):
    def __init__(self,glyphs):super().__init__(glyphs);self.rings=[];self.current=[]
    def _moveTo(self,p):self.current=[p]
    def _lineTo(self,p):self.current.append(p)
    def _curveToOne(self,a,b,c):
        p=self._getCurrentPoint()
        for i in range(1,13):
            t=i/12;s=1-t
            self.current.append(tuple(s*s*s*p[k]+3*s*s*t*a[k]+3*s*t*t*b[k]+t*t*t*c[k] for k in (0,1)))
    def _qCurveToOne(self,a,b):
        p=self._getCurrentPoint()
        for i in range(1,13):
            t=i/12;s=1-t
            self.current.append(tuple(s*s*p[k]+2*s*t*a[k]+t*t*b[k] for k in (0,1)))
    def _closePath(self):
        if len(self.current)>2:self.rings.append(Polygon(self.current))
        self.current=[]
    def _endPath(self):self._closePath()


def lettering(font_path,text,width,height,x,y):
    font=TTFont(font_path);glyphs=font.getGlyphSet();cmap=font.getBestCmap();cursor=0;shapes=[]
    for letter in text:
        pen=Outline(glyphs);name=cmap[ord(letter)];glyphs[name].draw(pen)
        shape=Polygon()
        # Font contour winding differs by source. Even/odd nesting preserves
        # all counters without depending on winding or filling the B / D holes.
        for p in pen.rings:shape=shape.symmetric_difference(p)
        from shapely.affinity import translate
        shapes.append(translate(shape,xoff=cursor));cursor+=font['hmtx'][name][0]
    shape=unary_union(shapes);l,b,r,t=shape.bounds
    from shapely.affinity import affine_transform
    return affine_transform(shape,[width/(r-l),0,0,height/(t-b),x-l*width/(r-l),y-b*height/(t-b)])


def polygons(shape):
    if shape.is_empty:return []
    return [shape] if shape.geom_type=='Polygon' else list(shape.geoms)


def cap(shape,z,colour,vertices):
    area=0
    for polygon in polygons(shape):
        rings=[np.array(polygon.exterior.coords[:-1])]+[np.array(r.coords[:-1]) for r in polygon.interiors]
        points=np.concatenate(rings);ends=np.cumsum([len(r) for r in rings],dtype=np.uint32)
        indices=earcut.triangulate_float64(points,ends)
        for i in range(0,len(indices),3):
            tri=points[indices[i:i+3]]
            a,b=tri[1]-tri[0],tri[2]-tri[0]
            area+=abs(a[0]*b[1]-a[1]*b[0])/2
            for px,py in tri:vertices.append((px,py,z,*colour))
    if abs(area-shape.area)>max(.00001,shape.area*.00001):raise ValueError('Invalid title cap triangulation')


def mesh(shape,red):
    vertices=[]
    # A dark extruded back, silver rim and inset face. Offsets are in the
    # existing title model's units; the camera and title parent animate them.
    from shapely.affinity import translate
    outside=shape.buffer(.12,join_style='mitre')
    rear=translate(outside,xoff=.16,yoff=-.18)
    cap(rear,-.38,(16,13,22),vertices)
    for polygon in polygons(outside):
        for ring in [polygon.exterior,*polygon.interiors]:
            for a,b in zip(ring.coords, list(ring.coords)[1:]):
                shade=72 if b[0]>a[0] else 32
                colour=(shade,shade,shade+8)
                front=[(*a,-.12),(*b,-.12)]
                back=[(a[0]+.16,a[1]-.18,-.38),(b[0]+.16,b[1]-.18,-.38)]
                for point in (front[0],back[0],back[1],front[0],back[1],front[1]):vertices.append((*point,*colour))
    cap(outside,-.12,(85,90,100),vertices)
    cap(shape.buffer(.08,join_style='mitre'),.0,(245,246,250),vertices)
    face=shape.buffer(-.025,join_style='mitre')
    cap(shape,.12,(145,152,166),vertices)
    cap(face,.18,(58,5,17) if red else (242,249,255),vertices)
    return vertices


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--big-font',type=Path,default=Path('/System/Library/Fonts/Supplemental/Times New Roman Bold.ttf'))
    p.add_argument('--three-d-font',type=Path,default=Path('/System/Library/Fonts/Supplemental/Arial Black.ttf'))
    p.add_argument('--output',type=Path,default=ROOT/'work/generated-title/m3d_title_mesh.inc',
                   help='Generated C mesh path (default: work/generated-title/m3d_title_mesh.inc)')
    args=p.parse_args()
    smash=lettering(args.big_font,'SMASH',42,9,-21,1)
    royale=lettering(args.three_d_font,'ROYALE',35,6,-17.5,-6)
    vertices=mesh(smash,True)+mesh(royale,False)
    unique=list(dict.fromkeys(tuple(round(float(v),5) for v in row[:3])+row[3:] for row in vertices))
    lookup={row:i for i,row in enumerate(unique)}
    indices=[lookup[tuple(round(float(v),5) for v in row[:3])+row[3:]] for row in vertices]
    if len(unique)>65535:raise ValueError('Title mesh index limit')
    lines=['/* Generated by tools/melee3d/title_lettering.py. SMASH ROYALE. */',
           'static const struct {float x,y,z;unsigned char r,g,b;} title_letters[] = {']
    lines += ['{%sf,%sf,%sf,%d,%d,%d},'%tuple([f'{v:.5f}' for v in row[:3]]+list(row[3:])) for row in unique]
    lines+=['};','static const unsigned short title_letter_indices[] = {']
    lines += [','.join(map(str,indices[i:i+24]))+',' for i in range(0,len(indices),24)]
    lines+=['};','']
    target=args.output.expanduser()
    target.parent.mkdir(parents=True,exist_ok=True)
    target.write_text('\n'.join(lines))
    print(target, len(vertices)//3, 'triangles; all face areas verified')

if __name__=='__main__':main()
