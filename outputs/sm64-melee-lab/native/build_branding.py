#!/usr/bin/env python3
"""Package the rendered transparent logo into native Mac/Windows icon formats."""
from pathlib import Path
import struct
from PIL import Image,ImageOps
HERE=Path(__file__).resolve().parent;OUT=HERE.parent/'branding'
def build():
    OUT.mkdir(exist_ok=True)
    source=Image.open(OUT/'Super Melee 64 Rendered.png').convert('RGBA')
    assert source.getchannel('A').getextrema()==(0,255),'Logo must retain real alpha transparency'
    # Format conversion only: the rendered artwork and alpha are unmodified.
    mark=ImageOps.contain(source,(1024,1024),Image.Resampling.LANCZOS)
    icon=Image.new('RGBA',(1024,1024));icon.alpha_composite(mark,((1024-mark.width)//2,(1024-mark.height)//2))
    icon.save(OUT/'Super Melee 64 Icon.png');icon.save(OUT/'Super Melee 64.png')
    icon.save(OUT/'Super Melee 64.icns')
    icon.save(OUT/'Super Melee 64.ico',sizes=[(16,16),(24,24),(32,32),(48,48),(64,64),(128,128),(256,256)])
    with (OUT/'logo.rgba').open('wb') as f:f.write(struct.pack('<II',1024,1024));f.write(icon.tobytes())
    print(OUT)
if __name__=='__main__':build()
