#!/usr/bin/env python3
"""Private Castle Grounds scene for the existing native Melee terrain adapter.

Extracts the owner's verified US ROM. This is a static traversal/combat proof,
not the running Mario 64 adventure simulation. Never downloads game assets.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[3]
DEFAULT_MELEE = ROOT/'work/deps/smash-royale'
SHA1 = '9bef1128717f958171a4afac3ed78ee2bb4e86ce'
DISPLAY_LISTS = [(0x07006D70, 1), (0x070095F0, 1), (0x0700A860, 4),
                 (0x0700B1D0, 6), (0x0700BA20, 4), (0x0700C430, 2), (0x0700C210, 1)]


def normal(points):
    a, b, c = points
    u, v = [b[i]-a[i] for i in range(3)], [c[i]-a[i] for i in range(3)]
    n = [u[1]*v[2]-u[2]*v[1], u[2]*v[0]-u[0]*v[2], u[0]*v[1]-u[1]*v[0]]
    size = math.sqrt(sum(x*x for x in n))
    return tuple(x/size for x in n) if size > 1e-8 else None


def compile_scene(rom_path, target, melee_root=DEFAULT_MELEE, scale=.1):
    sys.path.insert(0, str(melee_root/'tools/melee3d'))
    from extract_n64_arenas import mio0, sm64_collision
    from n64_display_mesh import display_mesh
    from n64_materials import decode_texture, combine
    from compile_arena_visuals import gx_rgb5a3, sky_panorama
    from sunshine_compile import ledges
    from sunshine_navigation import build as navigation, encode
    from sunshine_occlusion import groups

    if not math.isfinite(scale) or not .025 <= scale <= .25:
        raise ValueError('Scene scale must be between .025 and .25')
    rom = rom_path.read_bytes()
    if hashlib.sha1(rom).hexdigest() != SHA1:
        raise ValueError('Requires the verified original US SM64 ROM')
    # Pinned US-ROM segment offsets from sm64ex assets.json.
    banks = {7: mio0(rom, 4500416)[0], 9: mio0(rom, 3534096)[0],
             10: mio0(rom, 2803376)[0]}
    signature = struct.pack('>HHhhh', 0x40, 0x1EA, -4671, -511, -2545)
    offset = banks[7].find(signature)
    if offset < 0 or banks[7].find(signature, offset+1) >= 0:
        raise ValueError('Castle Grounds collision signature is not unique')
    collision = sm64_collision(banks[7], offset, 'castle_grounds')
    colliders = []
    for tri in collision['triangles']:
        points = [tuple(x*scale for x in collision['vertices'][i]) for i in tri['indices']]
        n = normal(points)
        if n is not None:
            surface = tri['source_surface']
            colliders.append((points, n, surface, 2 if surface == 0xA else 0))

    # Source COL_WATER_BOX records. Water is an immediate bottom KO, matching
    # the agreed initial rule, with the loader preserving covered dry tunnels.
    water_boxes = [(-7129, -7222, 8253, -58, -81), (1024, -58, 8230, 8137, -81)]
    for x0, z0, x1, z1, y in water_boxes:
        p = [(x0*scale,y*scale,z0*scale), (x0*scale,y*scale,z1*scale),
             (x1*scale,y*scale,z1*scale), (x1*scale,y*scale,z0*scale)]
        for ids in ((0,1,2), (0,2,3)):
            colliders.append(([p[i] for i in ids], (0.,1.,0.), 0x100, 1))

    textures, keys, geometry, draws = [], {}, [], []
    def texture(pixels, w, h, ws=1, wt=1, layer=1, flags=0):
        data = gx_rgb5a3(pixels, w, h)
        # GX enum values: LEQUAL=3, SRCALPHA=4, INVSRCALPHA=5, ALWAYS=7.
        transparent = layer == 6
        state = (0,1,3,int(not transparent),int(transparent),4,5,0,
                 4 if layer == 4 else 7,0,0,7,0,0)
        key = (w,h,ws,wt,flags,state,data)
        if key not in keys:
            keys[key] = len(textures)
            textures.append((w,h,5,ws,wt,flags,len(data),state,data))
        return keys[key]

    def resolve(address):
        segment, pos = address >> 24, address & 0xffffff
        if segment not in banks or pos >= len(banks[segment]):
            raise ValueError(f'Unresolved texture {address:08x}')
        return banks[segment][pos:]

    def decode(desc):
        if desc['format'] == 3 and desc['size'] == 2:
            data = resolve(desc['image']['address'])[:desc['width']*desc['height']*2]
            if len(data) != desc['width']*desc['height']*2:
                raise ValueError('Truncated IA16 texture')
            return [(a,a,a,b) for a,b in zip(data[::2],data[1::2])]
        return decode_texture(desc,resolve)

    # Ocean sky panorama, drawn first with depth disabled by the native loader.
    sky = texture(sky_panorama(banks[10]),256,256,1,0,flags=2)
    first = len(geometry)
    for row in range(16):
        for col in range(32):
            quad = []
            for dx,dy in ((0,0),(1,0),(1,1),(0,1)):
                u,v = (col+dx)/32,(row+dy)/16
                yaw,pitch = u*math.tau,(v-.5)*math.pi
                point = (5000*math.cos(pitch)*math.sin(yaw),
                         5000*math.sin(pitch),5000*math.cos(pitch)*math.cos(yaw))
                quad.append((point,(255,255,255,255),(u,v)))
            geometry.extend([[quad[i] for i in ids] for ids in ((0,1,2),(0,2,3))])
    draws.append((sky,first,len(geometry)-first))
    source_stats = []
    for address,layer in DISPLAY_LISTS:
        mesh = display_mesh(banks,address,f'{address:08x}',capture_materials=True)
        if mesh['unresolved_material_lists']:
            raise ValueError('Incomplete display-list graph')
        material_ids = []
        for desc in mesh['materials']:
            if desc is None:
                material_ids.append(texture([(255,255,255,255)]*16,4,4,layer=layer))
                continue
            wrap = lambda flag: 0 if flag&2 else 2 if flag&1 else 1
            material_ids.append(texture(decode(desc),desc['width'],desc['height'],
                                        wrap(desc['wrap_s']),wrap(desc['wrap_t']),layer))
        grouped = {}
        for tri in mesh['triangles']:
            desc = mesh['materials'][tri['material']]
            converted = []
            for i in tri['indices']:
                attr = mesh['vertex_attributes'][i]
                color = [round(255*x) for x in combine(tri['render_state'],attr['lit_rgba'])]
                uv = [0.,0.]
                if desc:
                    for axis,k in enumerate(('s','t')):
                        shift = desc['shift_'+k]
                        multiplier = 1/(1<<shift) if shift<=10 else 1<<(16-shift)
                        uv[axis] = (attr['st_fixed'][axis]/32*desc['scale'][axis]/65536*multiplier-desc['ul_'+k]/4)/desc[('width','height')[axis]]
                converted.append((tuple(x*scale for x in mesh['vertices'][i]),tuple(color),tuple(uv)))
            grouped.setdefault(material_ids[tri['material']],[]).append(converted)
        for material, triangles in grouped.items():
            draws.append((material,len(geometry),len(triangles)))
            geometry.extend(triangles)
        source_stats.append(dict(address=f'{address:08x}',triangles=len(mesh['triangles'])))

    # Visible water planes are static; no projectile/water effects are added.
    water = texture([(95,170,215,150)]*16,4,4,layer=6)
    first = len(geometry)
    for points,n,surface,flags in colliders:
        if flags&1:
            geometry.append([(p,(255,255,255,255),(p[0]/20,p[2]/20)) for p in points])
    draws.append((water,first,len(geometry)-first))

    target.mkdir(parents=True,exist_ok=True)
    raw = bytearray(struct.pack('<4s4I',b'DLV1',2,len(textures),len(draws),len(geometry)))
    for w,h,fmt,ws,wt,flags,size,state,data in textures:
        raw += struct.pack('<7I14I',w,h,fmt,ws,wt,flags,size,*state)+data
    raw += b''.join(struct.pack('<3I',*d) for d in draws)
    for tri in geometry:
        for p,c,uv in tri: raw += struct.pack('<3f4B2f',*p,*c,*uv)
    (target/'delfino.dlv').write_bytes(raw)
    assigned = groups(geometry)
    (target/'delfino.dlg').write_bytes(struct.pack('<4s3I',b'DLG1',1,len(geometry),max(assigned)+1)+struct.pack('<%dI'%len(assigned),*assigned))
    raw = struct.pack('<4s2I',b'DLC1',1,len(colliders))
    raw += b''.join(struct.pack('<12f2I',*(v for p in ps for v in p),*n,s,f) for ps,n,s,f in colliders)
    (target/'delfino.dlc').write_bytes(raw)
    boundary = ledges(colliders)
    spawn = (-1328*scale,260*scale,4664*scale)
    raw = struct.pack('<4sIf3fI',b'DLS1',2,scale,*spawn,len(boundary))
    raw += b''.join(struct.pack('<9f',*(v for p in edge for v in p)) for edge in boundary)
    (target/'delfino.dls').write_bytes(raw)
    nodes, links, components, nav = navigation(colliders,step=16)
    (target/'delfino.dln').write_bytes(encode(nodes,links,components,nav['step']))
    report = dict(scene='SM64 Castle Grounds',scope='Static terrain/rendering in native Melee; no SM64 adventure logic',
                  rom_sha1=SHA1,scale=scale,scale_status='explicit world-unit conversion; native fighter attributes unchanged',
                  collision_offset=offset,collision_triangles=len(colliders),render_triangles=len(geometry),
                  textures=len(textures),display_lists=source_stats,ledges=len(boundary),navigation=nav,spawn=spawn,
                  files={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in target.glob('delfino.dl?')})
    (target/'castle-report.json').write_text(json.dumps(report,indent=2)+'\n')
    return report


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--rom',type=Path,required=True)
    p.add_argument('--melee-root',type=Path,default=DEFAULT_MELEE)
    p.add_argument('--output',type=Path,default=ROOT/'work/castle-native-cache')
    p.add_argument('--scale',type=float,default=.1)
    args = p.parse_args()
    print(json.dumps(compile_scene(args.rom,args.output,args.melee_root,args.scale),indent=2))
