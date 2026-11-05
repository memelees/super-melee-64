#!/usr/bin/env python3
"""Private CSS artwork cache, using the locally installed Akaneia portraits.

No game art is distributed in outputs. Read the native DAT image/palette data
directly; do not import tools from the owner's independently edited project.
"""
from pathlib import Path
import hashlib,json,struct,re
from PIL import Image,ImageDraw,ImageFont,ImageOps
from native_lineup import PLAYERS
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[2]

class Archive:
    def __init__(self,path):
        b=path.read_bytes();_,size,rel,pub,ext=struct.unpack_from('>5I',b)
        self.data=b[32:32+size];at=32+size+rel*4;names=at+(pub+ext)*8
        self.public={b[names+n:b.index(0,names+n)].decode():p for p,n in
                     (struct.unpack_from('>II',b,at+i*8) for i in range(pub))}
    def u32(self,at):return struct.unpack_from('>I',self.data,at)[0]
    def image(self,tobj):
        return self.image_desc(self.u32(tobj+76),self.u32(tobj+80))
    def image_desc(self,im,tl):
        d=self.data;at,w,h,fmt=struct.unpack_from('>IHHI',d,im)
        if fmt!=9:raise ValueError('Expected native CI8 CSS portrait')
        pal,kind=struct.unpack_from('>II',d,tl);n=struct.unpack_from('>H',d,tl+12)[0]
        colors=[]
        for k in range(n):
            v=struct.unpack_from('>H',d,pal+2*k)[0]
            if kind==2:c=(((v>>10)&31)*255//31,((v>>5)&31)*255//31,(v&31)*255//31,255) if v&32768 else (((v>>8)&15)*17,((v>>4)&15)*17,(v&15)*17,((v>>12)&7)*255//7)
            elif kind==1:c=((v>>11)*255//31,((v>>5)&63)*255//63,(v&31)*255//31,255)
            else:c=(v&255,)*3+(v>>8,)
            colors.append(c)
        pixels=[(0,0,0,0)]*(w*h)
        for by in range(0,h,4):
            for bx in range(0,w,8):
                for y in range(4):
                    for x in range(8):
                        if bx+x<w and by+y<h:pixels[(by+y)*w+bx+x]=colors[d[at+y*8+x]]
                at+=32
        result=Image.new('RGBA',(w,h));result.putdata(pixels);return result

def build(course_enemies=False):
    assets=ROOT/'work/melee-embedded/assets';cache=ROOT/('work/melee-selection-course' if course_enemies else 'work/melee-selection');cache.mkdir(exist_ok=True)
    abi=ROOT/'work/melee-embedded/abi/src'
    # Read the frozen runtime's registered variant counts, rather than guessing
    # from the number of costume files (some share archives).
    table=(abi/'melee/ft/ftdata.c').read_text().split('CostumeListsForeachCharacter[Ft_Kind_Max] = {',1)[1].split('};',1)[0]
    arrays=re.findall(r'\{ (\w+_CostumeList),',table)
    declarations='\n'.join(f.read_text() for f in (abi/'melee/ft/kinds').rglob('*.h'))
    counts={k:int(re.search(r'\b'+name+r'\[(\d+)\]',declarations)[1]) for k,name in enumerate(arrays) if re.search(r'\b'+name+r'\[(\d+)\]',declarations)}
    added=(abi/'pc/m3d_roster_tables.inc').read_text()
    counts.update({int(k):int(n) for k,n in re.findall(r'CostumeListsForeachCharacter\[(\d+)\].numCostumes=(\d+)',added)})
    sources=[assets/'MnSlChr.usd',assets/'MxDt.dat',Path(__file__),HERE/'native_lineup.py',HERE/'campaign.json',HERE.parent/'branding/Super Melee 64.png']
    digest=hashlib.sha256(bytes([course_enemies])+json.dumps(counts,sort_keys=True).encode()+b''.join(p.read_bytes() for p in sources)).hexdigest()
    manifest=cache/'art.json';output=cache/'selection.bin'
    if manifest.exists() and output.exists() and json.loads(manifest.read_text()).get('sha256')==digest:return output
    css=Archive(sources[0]);mex=Archive(sources[1]);root=css.u32(css.public['mexSelectChr']);j=css.u32(root+8)
    table=mex.public['mexData'];menu=mex.u32(table+4);fighter=mex.u32(table+8)
    icons=mex.u32(menu+4)+220;ids=mex.u32(fighter+12);portraits=[];characters=[]
    for i in range(32):
        o=css.u32(j+16);portrait=None
        while o:
            mat=css.u32(o+8);t=css.u32(mat+8) if mat else 0
            while t:
                image=css.u32(t+76)
                if image and css.u32(image+8)==9:portrait=css.image(t)
                t=css.u32(t+4)
            o=css.u32(o+4)
        external=mex.data[icons+i*28+1];kind=mex.data[ids+external*3];kind+=6 if kind>=27 else 0
        row=next(row for row in PLAYERS if row[2]==kind)
        if row[0]=='gamewatch':
            # Native black silhouette has a transparent backdrop. Give it a
            # light inset in both the roster and selected-fighter card.
            matte=Image.new('RGBA',portrait.size)
            ImageDraw.Draw(matte).rounded_rectangle((2,2,portrait.width-3,portrait.height-3),radius=3,fill=(203,219,232,255))
            matte.alpha_composite(portrait);portrait=matte
        portraits.append(portrait);characters.append(row)
        j=css.u32(j+12)
    # The native m-ex MatAnim maps external + costume*stride frames to shared
    # CI8 image/palette entries. Read its constant U8 keyframes, as Melee does.
    mex_css=css.public['mexSelectChr'];stride=css.u32(mex_css+16)
    texanim=css.u32(css.u32(mex_css+12)+8);anim=css.u32(texanim+8)
    fobj=css.u32(anim+8);length=css.u32(fobj+4);data=css.data[css.u32(fobj+16):css.u32(fobj+16)+length]
    assert css.data[fobj+12:fobj+15]==bytes((1,128,128))
    at=0;frames=[]
    while at<len(data):
        byte=data[at];at+=1;assert byte&15==1
        packed=((byte>>4)&7)+1;shift=3
        while byte&128:
            byte=data[at];at+=1;packed+=(byte&127)<<shift;shift+=7
        for _ in range(packed):
            value=data[at];at+=1;wait=0;shift=0
            while True:
                byte=data[at];at+=1;wait|=(byte&127)<<shift;shift+=7
                if not byte&128:break
            frames.extend([value]*wait)
    images=css.u32(texanim+12);palettes=css.u32(texanim+16)
    large=Image.new('RGBA',(7*136,33*188));preview_ids=[]
    for row,character in enumerate(characters+[next(r for r in PLAYERS if r[0]=='sheik')]):
        name,external,kind=character
        native_external=external-8 if external>=34 else external
        for costume in range(counts[kind]):
            image=frames[native_external+costume*stride]
            portrait=css.image_desc(css.u32(images+image*4),css.u32(palettes+image*4))
            assert portrait.size==(136,188)
            if name=='gamewatch':
                matte=Image.new('RGBA',portrait.size,(175,187,201,255));matte.alpha_composite(portrait);portrait=matte
            large.alpha_composite(portrait,(costume*136,row*188));preview_ids.append((name,costume,image))
    # The original CSS shares Zelda's cell with Sheik. Keep that native toggle.
    names={'dk':'DONKEY KONG','falcon':'CAPTAIN FALCON','gamewatch':'MR. GAME & WATCH','iceclimbers':'ICE CLIMBERS','drmario':'DR. MARIO','younglink':'YOUNG LINK','dedede':'KING DEDEDE'}
    def font(size):
        for path in ('/System/Library/Fonts/Supplemental/Arial Bold Italic.ttf','C:/Windows/Fonts/arialbi.ttf','/usr/share/fonts/truetype/dejavu/DejaVuSans-BoldOblique.ttf'):
            if Path(path).is_file():return ImageFont.truetype(path,size)
        return ImageFont.load_default(size=size)
    W,H=960,720;bg=Image.new('RGBA',(W,H));d=ImageDraw.Draw(bg)
    for y in range(H):d.line((0,y,W,y),fill=(10+y//70,14+y//55,26+y//35,255))
    for x in range(-720,1500,90):d.line((x,0,x+720,720),fill=(29,36,52),width=2)
    d.ellipse((590,-260,1140,290),outline=(55,65,84),width=28);d.line((865,0,865,286),fill=(55,65,84),width=20);d.line((610,108,1100,108),fill=(55,65,84),width=20)
    d.polygon([(0,18),(680,18),(720,100),(0,100)],fill=(4,5,10));d.text((34,20),'SUPER MELEE 64',font=font(37),fill=(246,216,147));d.text((38,65),'ADVENTURE  /  CHARACTER SELECT',font=font(18),fill=(220,225,238))
    d.rectangle((18,8,680,100),fill=(4,5,10));bg.alpha_composite(ImageOps.contain(Image.open(sources[5]),(275,95),Image.Resampling.LANCZOS),(28,4));d=ImageDraw.Draw(bg)
    d.text((40,105),'SAVE',font=font(12),fill=(201,207,219))
    regions=[]
    for i,portrait in enumerate(portraits):
        row=i//11;col=i%11;x=40+col*80+(40 if row==2 else 0);y=125+row*76
        bg.alpha_composite(portrait.resize((80,70),Image.Resampling.LANCZOS),(x,y));regions.append((x,y,80,70))
    d=ImageDraw.Draw(bg);d.line((35,367,925,367),fill=(156,163,181),width=2)
    d.text((40,382),'CHOOSE YOUR FIGHTERS',font=font(23),fill='white')
    d.text((40,415),'Tab / L / R: edit slot    Arrows / stick / portraits: choose    Z / Y: Zelda / Sheik',font=font(14),fill=(183,191,211))
    colors=[(206,43,49),(61,113,191),(194,151,42),(52,145,100)]
    for slot,color in enumerate(colors):
        x=40+slot*226
        d.rectangle((x,450,x+212,604),fill=tuple(v//4 for v in color),outline=color,width=2)
        d.polygon([(x,450),(x+212,450),(x+195,475),(x,475)],fill=color)
        d.text((x+10,452),'P1 / YOU' if slot==0 else f'CPU {slot} / LEVEL 9',font=font(16),fill='white')
    d.text((40,612),'Enemy count: C / X or click',font=font(13),fill=(187,198,220))
    for count in range(4):
        x=284+count*44;d.rectangle((x,609,x+36,633),outline=(112,128,154),width=1)
        d.text((x+12,610),str(count),font=font(17),fill='white')
    d.text((495,614),'Click a player card to choose their fighter',font=font(13),fill=(187,198,220))
    d.polygon([(28,646),(931,646),(915,690),(44,690)],fill=(173,24,34),outline=(249,206,92),width=3)
    d.text((264,649),'READY TO ADVENTURE',font=font(31),fill=(255,236,184))
    d.text((330,696),'ENTER / A / START TO BEGIN',font=font(13),fill=(201,207,219))
    if course_enemies:
        d.rectangle((30,375,935,440),fill=(16,20,30))
        d.text((40,382),'CHOOSE YOUR FIGHTER',font=font(23),fill='white')
        d.text((40,415),'Arrows / stick / portraits: choose    Z / Y: Zelda / Sheik',font=font(14),fill=(183,191,211))
        d.rectangle((270,448,930,640),fill=(16,20,30))
        d.text((290,457),'ADVENTURE MODE',font=font(25),fill=(246,216,147))
        for x,label,width in ((290,'DEFAULT',160),(466,'IMPOSSIBLE',180),(662,'NO MELEE ENEMIES',224)):
            d.rounded_rectangle((x,492,x+width,524),radius=3,fill=(38,46,64),outline=(139,150,173),width=1)
            d.text((x+12,495),label,font=font(18),fill='white')
        d.rectangle((32,609,935,639),fill=(16,20,30))
        d.text((310,614),'L/R or [ ]: stage    M/X: mode    R: record',font=font(14),fill=(187,198,220))
        
    # Sprite atlas: selected portraits/name cards and the four CPU settings.
    atlas=Image.new('RGBA',(512,4096));ad=ImageDraw.Draw(atlas)
    for i,(portrait,row) in enumerate(zip(portraits,characters)):
        name=names.get(row[0],row[0].upper());y=i*48
        atlas.alpha_composite(portrait.resize((46,40),Image.Resampling.LANCZOS),(0,y));ad.text((56,y+7),name,font=font(24 if len(name)<17 else 20),fill='white')
    ad.text((56,32*48+7),'SHEIK',font=font(24),fill='white');atlas.alpha_composite(portraits[19].resize((46,40)),(0,32*48))
    ad.text((0,1590),'Story-style duels, weaker teams, and waves.',font=font(20),fill=(220,226,240))
    ad.text((0,1628),'Yoshi outside; Peach guards the castle interior.',font=font(19),fill=(220,226,240))
    ad.text((0,1710),'Opening: Yoshi + Peach + Bowser.',font=font(19),fill=(255,218,158))
    ad.text((0,1748),'Level 9; one Peach inside the castle.',font=font(20),fill=(255,218,158))
    ad.text((0,1880),'OFF',font=font(35),fill=(157,170,194))
    ad.text((0,1928),'Click to add CPU',font=font(19),fill=(177,190,212))
    for i,text in enumerate(('UNLOCKED','CONTINUE','NEW PLAYTHROUGH','RECORD: OFF','RECORD: ON')):
        ad.text((0,2000+i*32),text,font=font(21),fill=(255,236,184) if i<3 else (255,170,170))
    ad.text((0,3660),'CPU / LEVEL 9',font=font(18),fill='white')
    ad.text((0,3690),'SPAWN ENEMY',font=font(28),fill=(255,236,184))
    ad.text((0,3600),'STAGE PROGRESS',font=font(21),fill=(255,236,184))
    for i,label in enumerate(('READY TO FIGHT','',
        'Enter / A: confirm   Esc / B: back','',
        'COSTUME','<','>')):
        ad.text((0,2160+i*40),label,font=font(19 if i else 28),fill=(255,236,184))
    for i in range(32):
        ad.text(((i%8)*60,2480+(i//8)*32),str(i+1),font=font(23),fill='white')
    plan=json.loads((HERE/'campaign.json').read_text())
    labels=['CASTLE START']+[stage['name'] for stage in plan['stages']]
    for i,label in enumerate(labels):
        size=23
        while ad.textbbox((0,0),label,font=font(size))[2]>506:size-=1
        ad.text((0,2688+i*32),label,font=font(size),fill=(255,236,184))
    # Both startup and in-game selectors expose costumes using the same control.
    d=ImageDraw.Draw(bg)
    d.rectangle((34,436,925,447),fill=(16,20,30))
    d.rectangle((30,375,935,447),fill=(16,20,30))
    with output.open('wb') as f:
        f.write(struct.pack('<4s5I',b'SSC5',W,H,512,4096,32))
        for row,rect in zip(characters,regions):f.write(struct.pack('<7I24s',row[1],*rect,row[2],counts[row[2]],row[0].encode().ljust(24,b'\0')))
        f.write(bg.tobytes());f.write(atlas.tobytes());f.write(large.tobytes())
    manifest.write_text(json.dumps(dict(sha256=digest,portraits=32,forms=33,source='Locally installed native Akaneia CSS CI8 art',characters=characters,costume_counts=counts,native_costume_portraits=preview_ids),indent=2)+'\n')
    bg.save(cache/'preview.png');large.save(cache/'costume-portraits.png');return output
if __name__=='__main__':print(build())
