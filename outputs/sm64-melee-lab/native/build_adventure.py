#!/usr/bin/env python3
"""Rebuild the adventure against this experiment's frozen native ABI and objects.

The owner's concurrently edited Melee checkout is never compiled or modified.
Requires the pinned bootstrap and local-disc asset caches under work/.
"""
from pathlib import Path
import os,sys,shutil
import hashlib, json, subprocess
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
OUT=Path(os.environ.get('SMB_RUNTIME_DIR',str(ROOT/'work/melee-embedded')))

def build():
    plan=json.loads((OUT/'cached-build-plan.json').read_text())
    if 'game_compiler' not in plan or 'gpu_compiler' not in plan:
        raise RuntimeError('Missing frozen GCC/Aurora compiler plan for this local experiment')
    def compile_game(relative,source,extra=()):
        path=OUT/relative;path.parent.mkdir(parents=True,exist_ok=True);path.write_text(source)
        obj=Path(str(path)+('.obj' if plan.get('target')=='windows' else '.o'))
        subprocess.run(plan['game_compiler']+['-I',str(OUT/'abi'/Path(relative).parent),'-I',str(HERE),*extra,'-c',str(path),'-o',str(obj)],check=True)
        subprocess.run([plan.get('archiver','ar'),'r',str(OUT/'libmelee_game.a'),str(obj)],check=True)
    source=(OUT/'src/pc/m3d_lab.c').read_text()
    source=source.replace('enabled=getenv("MELEE3D_NATIVE_AUDIO_CHECK")!=NULL;',
        'enabled=getenv("MELEE3D_NATIVE_AUDIO_CHECK")!=NULL||getenv("SMB_SOUND_TRACE")!=NULL;')
    source=source.replace('if(!getenv("MELEE3D_NATIVE_CAPTURE_CHECK")&&(panels!=',
        'if(!getenv("SM64_MELEE_LIBRARY")&&!getenv("MELEE3D_NATIVE_CAPTURE_CHECK")&&(panels!=')
    boundary='        HSD_GObj* entity=human>=0?Player_GetEntity(human):NULL;'
    if 'SMB HUD SETUP CHECK' not in source:
        source=source.replace(boundary,'''        if(getenv("SM64_MELEE_LIBRARY")) {
            int expected=0;for(int slot=0;slot<GM_MAX_PLAYERS;slot++)if(Player_GetPlayerSlotType(slot)!=Gm_PKind_NA)expected++;
            if(panels!=expected){fprintf(stderr,"SMB HUD SETUP FAIL panels=%d expected=%d\\n",panels,expected);abort();}
            fprintf(stderr,"SMB HUD SETUP CHECK panels=%d expected=%d\\n",panels,expected);
        }
'''+boundary)
    source=source.replace('for(int slot=0;slot<4;slot++)if(Player_GetPlayerSlotType(slot)', 'for(int slot=0;slot<GM_MAX_PLAYERS;slot++)if(Player_GetPlayerSlotType(slot)')
    if 'SMB ENEMY SETUP' not in source:
        boundary='    fprintf(stderr, "M3D LAB SETUP slots=%d capacity=%d ports=4\\n", count, GM_MAX_PLAYERS);'
        if boundary not in source:raise RuntimeError('Unexpected native CPU setup boundary')
        source=source.replace(boundary,'''    if(getenv("SM64_MELEE_LIBRARY")) {
        start->rules.is_teams=1;start->rules.friendly_fire=0;
        for(int seat=0;seat<count;seat++) {
            start->players[seat].team=seat?1:0;
            if(seat){start->players[seat].cpu_kind=0;start->players[seat].cpu_level=9;}
        }
        fprintf(stderr,"SMB ENEMY SETUP count=%d level=9 hostile_to=player friendly_fire=0\\n",count-1);
    }
'''+boundary)
    source=source.replace('start->rules.is_teams=count>1;','start->rules.is_teams=1;')
    if 'SMB_PLAYER_COSTUME' not in source:
        source=source.replace('        player->color=color;player->sub_color=0;expected_costumes[i]=color;',
            '        if(i==0&&getenv("SMB_PLAYER_COSTUME"))color=strtoul(getenv("SMB_PLAYER_COSTUME"),NULL,10)%variants;\n        player->color=color;player->sub_color=0;expected_costumes[i]=color;')
    if 'void smb_native_expect_costume(' not in source:source+='\nvoid smb_native_expect_costume(unsigned seat,unsigned costume){if(seat<GM_MAX_PLAYERS)expected_costumes[seat]=costume;}\n'
    compile_game('src/pc/m3d_lab.c',source,('-Dm3d_lab_tick=m3d_lab_tick_original','-Dm3d_lab_pad=m3d_lab_pad_original'))
    # Circular ground shadows use the live SM64 floor provider; no height lines.
    marker=(HERE/'smash_royale_ground_markers.c').read_text()
    compile_game('src/pc/world/m3d_arena_visual.c',marker)
    # Campaign opponents and player swaps can appear after Melee's match-start
    # preload. Load a swallowed retail fighter's copy archive and Kirby costume
    # before its hat/article callbacks dereference that archive (notably Peach).
    source='#include <stdlib.h>\nextern int smb_embed_active(void);\n'+(OUT/'abi/src/melee/ft/kinds/ftKirby/ftkirby.c').read_text()
    boundary='''    Fighter* fp = gobj->user_data;
    HSD_GObjEvent cb;

    if (fp->u.kb.hat.kind != kind) {'''
    if source.count(boundary)!=1:raise RuntimeError('Unexpected Kirby copy boundary')
    source=source.replace(boundary,'''    Fighter* fp = gobj->user_data;
    HSD_GObjEvent cb;

    if (smb_embed_active()) {
        if (kind < 0 || kind >= Ft_Kind_Akaneia0) kind = Ft_Kind_Kirby;
        if (kind != Ft_Kind_Kirby) {
            if (!ftKb_Init_803CA9D0[kind].filename) kind = Ft_Kind_Kirby;
            else ftKb_SpecialN_800EED50(kind, fp->costume_id);
        }
        if (getenv("SMB_SCENARIO")) fprintf(stderr,"SMB KIRBY COPY kind=%d costume=%u ready=%d\\n",kind,fp->costume_id,kind==Ft_Kind_Kirby||ft_80459B88.copies[kind]!=NULL);
    }
    if (fp->u.kb.hat.kind != kind) {''')
    compile_game('src/melee/ft/kinds/ftKirby/ftkirby.c',source)
    source='extern int smb_embed_active(void);\n#include <melee/ft/types.h>\nextern Fighter* smb_native_campaign_target(Fighter*);\n'+(OUT/'abi/src/pc/world/m3d_cpu.c').read_text()
    boundary='    if (fp->cpu.kind == CpuKind_Nana) {'
    source=source.replace(boundary,'''    if(smb_embed_active() && fp->player_idx>0 && fp->cpu.kind!=CpuKind_Nana) {
        target=smb_native_campaign_target(fp);fp->cpu.x44=fp->cpu.x48=target;
    }
'''+boundary,1)
    # The donor navigation graph and target filter refer to its original map.
    # They must never decide recovery or steering in a live SM64 course.
    for call in ('m3d_DelfinoNavRoute(fp,target,&goal)', 'm3d_DelfinoNavTarget(fp,target)',
                 'm3d_DelfinoNavDrive(fp)', 'm3d_PartyHold(fp)'):
        if call=='m3d_DelfinoNavRoute(fp,target,&goal)':
            source=source.replace(call, '(!smb_embed_active()&&'+call+')')
        elif call=='m3d_DelfinoNavTarget(fp,target)':
            source=source.replace(call, '(smb_embed_active()?target:'+call+')')
        else:source=source.replace(call+';', 'if(!smb_embed_active())'+call+';')
    compile_game('src/pc/world/m3d_cpu.c',source)
    source='extern int smb_embed_active(void);\n#include <melee/ft/types.h>\nextern Fighter* smb_native_campaign_target(Fighter*);\n'+(OUT/'abi/src/pc/world/m3d_move.c').read_text()
    # Human directional attacks keep their requested heading. Native CPU
    # aiming remains enabled; it cannot steer the player's dash attack.
    source=source.replace('        if(fp->is_sleeping||fp->motion_id<14',
        '        if((smb_embed_active()&&fp->player_idx==0)||fp->is_sleeping||fp->motion_id<14')
    compile_game('src/pc/world/m3d_move.c',source)

    # The donor plaza normally exposes only the local viewer's HUD. This
    # adventure has up to four visible combatants, using Melee's four anchors.
    for filename in ('ifstatus.c','ifstock.c'):
        source='extern int smb_embed_hud_all(void);\n'+(OUT/'abi/src/melee/if'/filename).read_text()
        # Current upstream also uses ReferenceOverlayActive in these guards.
        # Gate the shared predicates so visibility, anchors and sizing all
        # return to native four-player layout in the embedded adventure.
        for predicate in ('m3d_DelfinoActive','m3d_ReferenceOverlayActive'):
            source=source.replace(predicate+'()', '('+predicate+'()&&!smb_embed_hud_all())')
        if filename=='ifstatus.c':source=source.replace('(m3d_room_enabled() || (s32) ifStatus_804D6D60 > arg0)', '(smb_embed_hud_all() || m3d_room_enabled() || (s32) ifStatus_804D6D60 > arg0)')
        compile_game('src/melee/if/'+filename,source)
    source=(OUT/'abi/src/pc/paths.c').read_text()
    source=source.replace('char* pc_get_pref_path(void) {',
        'char* pc_get_pref_path(void) {\n    if(SDL_getenv("SM64_MELEE_PROFILE"))return SDL_strdup(SDL_getenv("SM64_MELEE_PROFILE"));')
    path=OUT/'src/pc/paths.c';path.write_text(source);obj=Path(str(path)+('.obj' if plan.get('target')=='windows' else '.o'))
    subprocess.run(plan['compiler']+['-c',str(path),'-o',str(obj)],check=True)
    plan['link']=[str(obj) if p.endswith(('/CMakeFiles/melee.dir/src/pc/paths.c.o','/CMakeFiles/melee.dir/src/pc/paths.c.obj')) else p for p in plan['link']]
    source=(OUT/'abi/src/pc/world/m3d_delfino.c').read_text()
    source='#include <sysdolphin/baselib/memory.h>\n#include <melee/ft/types.h>\nstatic int host_world;\nstatic void smb_native_rebirth_position(Fighter*);\n'+source
    source=source.replace('    if(!active)return 0;\n    if(pos->y<spawn_center.y-DL_KILL_DEPTH)return 2;',
        '    if(host_world)return 0;\n    if(!active)return 0;\n    if(pos->y<spawn_center.y-DL_KILL_DEPTH)return 2;')
    source=source.replace('return m3d_surface_kind(t->n.y);',
        'return host_world?(t->n.y>=.01f?0:t->n.y<-.01f?1:2):m3d_surface_kind(t->n.y);')
    source=source.replace('static int floor_parts(unsigned i) {',
        'static int floor_parts(unsigned i) {\n    if(host_world)return 1;')
    source=source.replace('return t->n.y>=DL_WALKABLE?0:t->n.y<-.05f?1:2;',
        'return t->n.y>=(host_world?.01f:DL_WALKABLE)?0:t->n.y<-(host_world?.01f:.05f)?1:2;')
    source=source.replace('k==0 && !(t->flags&4)?LINE_FLAG_LEDGE:0;',
        'k==0 && !(t->flags&4) && (!host_world||t->n.y>=DL_WALKABLE)?LINE_FLAG_LEDGE:0;')
    source=source.replace('void m3d_DelfinoRebirth(Fighter* fp) {', 'void m3d_DelfinoRebirth(Fighter* fp) {\n    if(host_world){smb_native_rebirth_position(fp);return;}')
    source=source.replace('bool m3d_DelfinoCameraFade(void) {',
        'bool m3d_DelfinoCameraFade(void) {\n    if(host_world)return false;')
    # SM64's castle stairs use 25/26-unit risers, with some 51-unit steps.
    # Reuse Melee3D's supported-riser bevel, capped at 60 SM64 units.
    source=source.replace('rise<.5f||rise>3)continue;',
        'rise<.5f||rise>(host_world?6.f:3.f))continue;')
    source=source.replace('fprintf(stderr,"M3D DELFINO STAIRS risers=',
        'if(!host_world)fprintf(stderr,"M3D DELFINO STAIRS risers=')
    # Four permanent native line slots per host triangle. Opening/unloading a
    # dynamic object must never renumber a static ledge held by the fighter.
    source=source.replace('    for(int k=0;k<4;k++)total+=counts[k];',
        '    if(host_world)for(int k=0;k<4;k++)counts[k]=triangle_count;\n    for(int k=0;k<4;k++)total+=counts[k];')
    source=source.replace('if(t->flags&3 || (kind(t)!=k && !(k==3&&kind(t)==2)))continue;',
        'if(!host_world && (t->flags&3 || (kind(t)!=k && !(k==3&&kind(t)==2))))continue;')
    source=source.replace('lines_for_triangle[i][k==0?part:k==3?1:0]=start;triangle_for_line[start++]=i;',
        'if(!host_world||k==kind(t)||(k==3&&kind(t)==2))lines_for_triangle[i][k==0?part:k==3?1:0]=start;triangle_for_line[start++]=i;')
    source=source.replace('lines_for_triangle[i][k==3?1:0]=start;triangle_for_line[start++]=i;',
        'if(!host_world||k==kind(t)||(k==3&&kind(t)==2))lines_for_triangle[i][k==3?1:0]=start;triangle_for_line[start++]=i;')
    compile_game('src/pc/world/m3d_delfino.c',source+'\n#include "melee_world.inc"\n')
    source='extern int smb_embed_active(void);\n#include <melee/ft/types.h>\nextern Fighter* smb_native_campaign_target(Fighter*);\n'+(OUT/'abi/src/melee/mp/mplib.c').read_text()
    source=source.replace('    groundCollVtx = HSD_MemAlloc(', '    if(!groundCollVtx)groundCollVtx = HSD_MemAlloc(')
    source=source.replace('    groundCollLine =\n        HSD_MemAlloc(', '    if(!groundCollLine)groundCollLine =\n        HSD_MemAlloc(')
    source=source.replace('    groundCollJoint =\n        HSD_MemAlloc(', '    if(!groundCollJoint)groundCollJoint =\n        HSD_MemAlloc(')
    source=source.replace('    if (coll_data != NULL) {\n        mpIsland_8005A728();','    if (coll_data != NULL && !smb_embed_active()) {\n        mpIsland_8005A728();')
    compile_game('src/melee/mp/mplib.c',source)
    compile_game('src/pc/world/m3d_item.c',(OUT/'abi/src/pc/world/m3d_item.c').read_text(),
        ('-Dm3d_ItemInit=m3d_ItemInit_original','-Dm3d_ItemForget=m3d_ItemForget_original'))
    # Clear fighter references at retirement, before deferred GObj destruction.
    # Waiting for Item_OnUserDataRemove is too late for same-tick hitstun cleanup.
    source='extern void smb_native_item_retiring(void*);\n'+(OUT/'abi/src/melee/it/item.c').read_text()
    boundary='void Item_8026A8EC(Item_GObj* gobj)\n{'
    assert source.count(boundary)==1
    source=source.replace(boundary,boundary+'\n    smb_native_item_retiring(gobj);')
    compile_game('src/melee/it/item.c',source)
    source='extern int smb_native_rendering,smb_native_hud_rendering,smb_native_effect_depth;\n'+(OUT/'abi/src/sysdolphin/baselib/state.c').read_text()
    boundary='void HSD_StateSetAlphaUpdate(int arg0)\n{'
    if boundary not in source:raise RuntimeError('Unexpected native alpha-state boundary')
    source=source.replace(boundary,boundary+'\n    if(smb_native_effect_depth)arg0=0;else if(smb_native_rendering)arg0=1;')
    source=source.replace('void HSD_StateSetZMode(int arg0, int arg1, int arg2)\n{',
        'void HSD_StateSetZMode(int arg0, int arg1, int arg2)\n{\n    if(smb_native_hud_rendering){arg0=1;arg1=GX_ALWAYS;arg2=1;}')
    source=source.replace('void HSD_StateSetColorUpdate(int arg0)\n{',
        'void HSD_StateSetColorUpdate(int arg0)\n{\n    if(smb_native_effect_depth)arg0=0;')
    source=source.replace('    if(smb_native_hud_rendering){arg0=1;arg1=GX_ALWAYS;arg2=1;}',
        '    if(smb_native_effect_depth){arg0=1;arg2=1;}\n    if(smb_native_hud_rendering){arg0=1;arg1=GX_ALWAYS;arg2=1;}')
    compile_game('src/sysdolphin/baselib/state.c',source)
    # Retail drawing resets its overlay flags before each pass. Apply the
    # zero-colour-ratio native opacity stage after metal material selection.
    source='extern int smb_native_rendering;\n'+(OUT/'abi/src/melee/ft/ftmaterial.c').read_text()
    source=source.replace('    fp = GET_FIGHTER(HSD_GObj_804D7814);',
        '    fp = GET_FIGHTER(HSD_GObj_804D7814);\n    int smb_cap_overlay=fp->x2223_b2;')
    boundary='    HSD_StateInitTev();'
    assert source.count(boundary)==1
    source=source.replace(boundary,"""    if(smb_native_rendering && fp->x2226_b4) {
        fp->x2223_b2=true;fp->x610_color_rgba[1]=(GXColor){0,0,0,0};
    }
"""+boundary)
    boundary='\n}\n\nHSD_TExp* ftMaterial_800BF534'
    assert source.count(boundary)==1
    source=source.replace(boundary,'\n    if(smb_native_rendering && fp->x2226_b4)fp->x2223_b2=smb_cap_overlay;'+boundary)
    compile_game('src/melee/ft/ftmaterial.c',source)
    source='extern int smb_native_effect_depth;\n'+(OUT/'abi/src/sysdolphin/baselib/psdisp.c').read_text()
    source=source.replace('GXSetZMode(GX_TRUE, GX_LEQUAL, GX_FALSE);',
        'GXSetZMode(GX_TRUE, GX_LEQUAL, smb_native_effect_depth?GX_TRUE:GX_FALSE);')
    compile_game('src/sysdolphin/baselib/psdisp.c',source)
    # The embedded donor has no soundtrack. Keep its AX character/SFX voices;
    # mute both HPS music voices and the optional replacement stream mixer.
    source=(OUT/'abi/src/pc/audio.c').read_text()
    source=source.replace('float voice_gain = is_music_stream(v) ? pc_get_music_volume() : pc_get_sfx_volume();',
        '''/* Stream transport is also used by looping/moving SFX. Only HPS's
     * dedicated music priority is a music mute target. */
    int music=v->vpb.priority==0x1D;
    float voice_gain = music ? 0.0f : pc_get_sfx_volume();
    static int logged_music,logged_sfx;
    if(music&&!logged_music++){fprintf(stderr,"SMB AUDIO music_gain=0 replacement_music=disabled\\n");}
    static int logged_stream;
    if(!music&&v->is_stream&&voice_gain>0&&!logged_stream++){fprintf(stderr,"SMB AUDIO streamed_sfx=enabled priority=%d\\n",v->vpb.priority);}
    if(!music&&voice_gain>0&&!logged_sfx++){fprintf(stderr,"SMB AUDIO sfx_gain=%.2f voices=enabled\\n",voice_gain);}''')
    source=source.replace('if (pc_music_stream_mix) {','if (0 && pc_music_stream_mix) {')
    source=source.replace('bool is_stream; /* true for HPS music stream voices */','bool is_stream; /* Stream transport, including SFX. */\n    bool smb_music; /* HPS provenance survives a priority change. */')
    source=source.replace('pick->is_stream = true;', 'pick->is_stream = true;pick->smb_music = true;')
    source=source.replace('if (priority == 0x1D) {\n        v->is_stream = true;', 'if (priority == 0x1D) {\n        v->smb_music=true;v->is_stream = true;')
    source=source.replace('int music=v->vpb.priority==0x1D;', 'int music=v->smb_music;')
    source=source.replace('static void render_frame(float* out) {', 'extern int smb_embed_active(void);\nstatic void render_frame(float* out) {')
    marker='\n}\n\n/* MELEE_AUDIO_DUMP='
    trace="""
    if(getenv("SMB_AUDIO_TRACE")&&smb_embed_active()) {
        static unsigned frames;double energy=0;float peak=0;
        for(int i=0;i<AX_FRAME*2;i++){energy+=out[i]*out[i];float a=fabsf(out[i]);if(a>peak)peak=a;}
        if(++frames%40==0)fprintf(stderr,"SMB AUDIO OUTPUT frame=%u rms=%.6f peak=%.6f voices=%d\\n",frames,sqrt(energy/(AX_FRAME*2)),peak,n_running);
    }
"""
    source=source.replace(marker,trace+marker)
    source=source.replace('static FILE* s_dump;', 'static FILE* s_dump;\nstatic void (*smb_audio_sink)(const float*,unsigned);\nvoid smb_embed_audio_sink(void (*sink)(const float*,unsigned)){__atomic_store_n(&smb_audio_sink,sink,__ATOMIC_RELEASE);}')
    source=source.replace('        SDL_PutAudioStreamData(stream, frame, sizeof(frame));','        void (*sink)(const float*,unsigned)=__atomic_load_n(&smb_audio_sink,__ATOMIC_ACQUIRE);\n        if(sink)sink(frame,AX_FRAME);\n        SDL_PutAudioStreamData(stream, frame, sizeof(frame));')
    audio=OUT/'src/pc/audio.c';audio.write_text(source)
    audio_obj=Path(str(audio)+'.o')
    subprocess.run(plan['compiler']+['-c',str(audio),'-o',str(audio_obj)],check=True)
    plan['link']=[str(audio_obj) if p.endswith(('/CMakeFiles/melee.dir/src/pc/audio.c.o','/CMakeFiles/melee.dir/src/pc/audio.c.obj')) else p for p in plan['link']]
    (OUT/'cached-build-plan.json').write_text(json.dumps(plan,indent=2)+'\n')
    # SM64 owns fatal hazards. Melee's stage blast planes are unrelated to
    # the height/depth of adventure courses (notably VCUTM below the castle).
    source=(OUT/'abi/src/melee/ft/ft_0D31.c').read_text()
    source='extern int smb_embed_active(void);\n#include <melee/ft/types.h>\nextern Fighter* smb_native_campaign_target(Fighter*);\n'+source
    source=source.replace('bool ftCo_800D3158(Fighter_GObj* gobj)\n{',
        'bool ftCo_800D3158(Fighter_GObj* gobj)\n{\n    if(smb_embed_active())return false;')
    compile_game('src/melee/ft/ft_0D31.c',source)
    source=(OUT/'abi/src/melee/ft/ftwalljump.c').read_text()
    # Upstream now projects approach speed too. Our wrapper already enters
    # the lane-local frame and projects pos_delta for the retail predicate.
    # Keep that projection in one place rather than projecting it twice.
    source=source.replace("                    float lx,lz;\n                    if(m3d_GetLane(fp0,&lx,&lz))\n                        wall_relative_velocity=fp0->pos_delta.x*lx+fp0->pos_delta.z*lz-wall_pos.x;\n","")
    source='#include <pc/world/m3d.h>\n'+source.replace('bool ftWallJump_8008169C(HSD_GObj* gobj)', 'bool ftWallJump_8008169C_2D(HSD_GObj* gobj)').replace('bool ftWallJump_8008169C(Fighter_GObj* gobj)', 'bool ftWallJump_8008169C_2D(Fighter_GObj* gobj)')
    source+='\n#include "melee_walljump.inc"\n'
    compile_game('src/melee/ft/ftwalljump.c',source)
    source=(OUT/'abi/src/melee/ft/kinds/ftCommon/ftCo_Ottotto.c').read_text()
    source=source.replace('/* 09A410 */ static void',
        'extern int smb_native_step_down(Fighter*);\n\n/* 09A410 */ static void')
    source=source.replace('    if (fp->coll_data.env_flags & Collide_Edge && !fp->is_sandbag) {',
        '    if (fp->coll_data.env_flags & Collide_Edge && !fp->is_sandbag) {\n        if(smb_native_step_down(fp))return true;')
    compile_game('src/melee/ft/kinds/ftCommon/ftCo_Ottotto.c',source)
    # Motion bounds come from the actual pinned state tables, including mods.
    import re
    source=(OUT/'abi/src/melee/ef/eflib.c').read_text()
    for name in ('efLib_Cb_SetRotY_FromFighterDir','efLib_Cb_SetRotYZ_FromFighter'):
        source=source.replace('void '+name+'(', 'void '+name+'_2D(',1)
    source+='\n#include "melee_effect_directions.inc"\n'
    compile_game('src/melee/ef/eflib.c',source)
    source=(OUT/'abi/src/melee/ft/ftdata.c').read_text()
    table=re.search(r'MotionState\* ftData_CharacterStateTables\[Ft_Kind_Max\] = \{(.*?)\};',source,re.S)[1]
    entries=[x.strip() for x in table.split(',') if x.strip()]
    counts=','.join('0' if x=='NULL' else 'sizeof('+x+')/sizeof(MotionState)' for x in entries)
    source+='\nextern unsigned smb_native_added_motion_count(unsigned);\nunsigned smb_native_motion_count(unsigned kind){static const unsigned count[]={'+counts+'};return kind>=33&&kind<40?341+smb_native_added_motion_count(kind):kind<sizeof(count)/sizeof(count[0])?341+count[kind]:0;}\n'
    compile_game('src/melee/ft/ftdata.c',source)
    source=(OUT/'abi/src/pc/m3d_akaneia.c').read_text()+'\nunsigned smb_native_added_motion_count(unsigned kind){return kind>=33&&kind<40?move_counts[kind-33]:0;}\n'
    compile_game('src/pc/m3d_akaneia.c',source)
    # Remote actors import presentation; authored gameplay commands still run
    # only on their owner (or on a temporary native grab borrower).
    source=(OUT/'abi/src/melee/ft/ftaction.c').read_text()
    source='#include <melee/ft/types.h>\nextern int smb_native_presentation_only(Fighter*);\nextern int smb_native_presentation_command(unsigned);\n'+source
    for pointer,actor,table in [('ftCommand','fighter_gobj','ftAction_803C06E8'),('cmd','gobj','ftAction_803C07AC')]:
        prefix='if (Command_Execute('+pointer+', eventCode) == false) {'
        replacement='if(smb_native_presentation_only(fp)&&eventCode==9){'+pointer+'->x8.u++;} else '+prefix
        source=source.replace(prefix,replacement,1)
        call=table+'[eventCode]('+actor+', '+pointer+');'
        guarded='if(eventCode>=ARRAY_SIZE('+table+')){'+pointer+'->x8.u=NULL;} else if(smb_native_presentation_only(fp)&&!smb_native_presentation_command(eventCode)){'+pointer+'->x8.u+=ftAction_803C0870[eventCode];}else '+call
        source=source.replace(call,guarded,1)
    source=source.replace('if (!Command_Execute(cmd, id)) {','if(smb_native_presentation_only(fp)&&id==9){cmd->x8.u++;} else if (!Command_Execute(cmd, id)) {',1)
    compile_game('src/melee/ft/ftaction.c',source)
    source=(OUT/'abi/src/melee/ft/ftanim.c').read_text()
    source='#include <melee/ft/types.h>\nextern int smb_native_presentation_only(Fighter*);\n'+source
    source=source.replace('    ftCo_800DB500(gobj);','    if(!smb_native_presentation_only(fp))ftCo_800DB500(gobj);')
    compile_game('src/melee/ft/ftanim.c',source)
    source=(OUT/'abi/src/melee/ft/ftlib.c').read_text()
    source='#include <melee/ft/types.h>\nextern int smb_native_remote_controlled(Fighter*);\n'+source
    source=source.replace('bool ftLib_IsSamePlayer(HSD_GObj* gobj0, HSD_GObj* gobj1)\n{','bool ftLib_IsSamePlayer(HSD_GObj* gobj0, HSD_GObj* gobj1)\n{\n    if((gobj0&&smb_native_remote_controlled(GET_FIGHTER(gobj0)))||(gobj1&&smb_native_remote_controlled(GET_FIGHTER(gobj1))))return true;')
    compile_game('src/melee/ft/ftlib.c',source)
    # Rename audio definitions, keeping their nested call sites routed through
    # the capture wrappers so local native channel/stop ordering is preserved.
    audio_sources={
        'src/melee/ft/ft_0877.c':['ft_PlaySFX'],
        'src/melee/ft/ft_0881.c':['ft_800881D8','ft_80088328','ft_80088478','ft_80088510','ft_800885A8','ft_80088640','ftCo_800886D8','ft_80088770','ft_800887CC','ft_80088828','ft_80088884','ft_800888E0','ft_8008893C','ft_80088998','ft_80088A50','ft_80088C5C'],
        'src/melee/it/item.c':['Item_8026AE84','Item_8026AF0C','Item_8026AFA0','Item_8026B034','Item_8026B074'],
        'src/melee/lb/lbaudio_ax.c':['lbAudioAx_800263E8'],
    }
    for relative,names in audio_sources.items():
        source=(OUT/'abi'/relative).read_text()
        for name in names:
            pattern=r'(?m)^([A-Za-z_][A-Za-z0-9_ \t*]*\s)'+name+r'(\s*\()'
            source,count=re.subn(pattern,lambda m:m[1]+'smb_audio_orig_'+name+m[2],source)
            if not count:raise RuntimeError('Missing native audio definition '+name)
        if relative.endswith('/item.c'):
            source='#include <melee/it/types.h>\nextern void smb_native_audio_item_retire(Item*);\n'+source
            needle='    HSD_ObjFree(&item_alloc_data, item_data);'
            if needle not in source:raise RuntimeError('Missing item audio retirement boundary')
            source=source.replace(needle,'    smb_native_audio_item_retire(item_data);\n'+needle)
        compile_game(relative,source)
    source=(OUT/'abi/src/melee/ft/fighter.c').read_text()
    source='#include <melee/ft/types.h>\nextern void smb_native_damage_applied(Fighter*,float);\nextern void smb_native_damage_reset(Fighter*);\n'+source
    boundary='    fp->x8_spawnNum = Fighter_NewSpawn_80068E40();'
    if source.count(boundary)!=1:raise RuntimeError('Missing native damage reset boundary')
    source=source.replace(boundary,boundary+'\n    smb_native_damage_reset(fp);')
    boundary='        fp->dmg.x1830_percent += damage_amount;'
    if source.count(boundary)!=1:raise RuntimeError('Missing actual native damage boundary')
    source=source.replace(boundary,'        smb_native_damage_applied(fp,damage_amount);\n'+boundary)
    source='#include <melee/ft/types.h>\nextern void smb_native_slide_velocity(Fighter*);\nextern int smb_native_travel_physics(Fighter*);\nextern void smb_native_water_physics(Fighter*);\nextern int smb_native_remote_controlled(Fighter*);\n'+source
    source=source.replace('        if (fp->phys_cb) {','        if (!smb_native_travel_physics(fp) && fp->phys_cb) {')
    source='#include <melee/ft/types.h>\nextern int smb_native_wall_before_input(Fighter*);\nextern int smb_native_water_before_input(Fighter*);\n'+source
    source=source.replace('            if (fp->input_cb) {','            if (!smb_native_water_before_input(fp) && !smb_native_wall_before_input(fp) && fp->input_cb) {')
    source=source.replace('    ftCo_800D3158(gobj);','    smb_native_slide_velocity(fp);\n    ftCo_800D3158(gobj);')
    source=source.replace('            fp->phys_cb(gobj);','            fp->phys_cb(gobj);\n            smb_native_water_physics(fp);')
    source=source.replace('    m3d_EnterLocal(fp);\n    Fighter_procUpdate2D(gobj);','    if(smb_native_remote_controlled(fp))return;\n    m3d_EnterLocal(fp);\n    Fighter_procUpdate2D(gobj);')
    source=source.replace('    m3d_EnterLocal(fp);\n    Fighter_procMap2D(gobj);','    if(smb_native_remote_controlled(fp))return;\n    m3d_EnterLocal(fp);\n    Fighter_procMap2D(gobj);')
    for name in ('Fighter_procGrabColl','Fighter_procAttackColl','Fighter_procCpu','Fighter_procCamera','Fighter_procHitlag','Fighter_procInput','Fighter_procCollResolve'):
        source=source.replace('void '+name+'(Fighter_GObj* gobj)\n{','void '+name+'(Fighter_GObj* gobj)\n{\n    if(smb_native_remote_controlled(GET_FIGHTER(gobj)))return;')
    source='#include <melee/ft/types.h>\nextern void smb_native_remote_anim(HSD_GObj*);\n'+source
    boundary='void Fighter_procAnim(Fighter_GObj* gobj)\n{'
    if boundary not in source:raise RuntimeError('Missing proxy animation boundary')
    source=source.replace(boundary,boundary+'\n    if(smb_native_remote_controlled(GET_FIGHTER(gobj))){smb_native_remote_anim(gobj);return;}')
    compile_game('src/melee/ft/fighter.c',source)
    if not os.environ.get('SMB_SKIP_GPU_BUILD'):subprocess.run([sys.executable,str(HERE/'build_compositor.py')],check=True)
    exports=['init','step','shutdown','render','world','contact','impact','health','actor_state','actor_environment','actor_world_hit','actor_world_ko','actor_damage','ledge','wall','support','stair','camera','celebrate','hold','npc','npc_wait','encounter','npc_ko','npc_configure','npc_add','remote','remote_articles','campaign_snapshot','campaign_remote','campaign_release','net_snapshot','net_hits','net_event','sounds','remote_sounds','audio_enabled','grab_begin','grab_present','grab_snapshot','grab_mash','grab_input','grab_end','grab_cancel_all','audio_sink','switch','costumes','powerups']
    (OUT/'exports.txt').write_text(''.join('_smb_embed_'+name+'\n' for name in exports))
    subprocess.run([sys.executable,str(HERE/'relink_embedded.py')],check=True)
    subprocess.run([sys.executable,str(HERE/'prepare_sm64_host.py')],check=True)
    from build_host import build as build_host
    build_host()
    pin_path=OUT/'source-pin.json';pin=json.loads(pin_path.read_text())
    pin.update(scope='Single-process native rendering, live SM64 collision and adventure bridge',protocol=14,
        host_sha256=hashlib.sha256((ROOT/('work/sm64ex-source/build/us_melee_bridge_windows/sm64.us.f3dex2e.exe' if plan.get('target')=='windows' else 'work/sm64ex-source/build/us_melee_bridge/sm64.us.f3dex2e')).read_bytes()).hexdigest(),
        adapter_sources={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in HERE.iterdir() if p.suffix in ('.c','.cpp','.h','.inc','.m')})
    pin['gpu_patched_sources']=['window.cpp','gfx/recording.cpp','gx/shader.cpp','gx/pipeline.cpp','gx/gx.cpp']
    pin['private_gpu_libraries']={name:hashlib.sha256((OUT/'pinned-link-inputs/extern/aurora'/name).read_bytes()).hexdigest()
        for name in ('libaurora_gx.a','libaurora_core.a')}
    pin['integration_build_scripts']={name:hashlib.sha256((HERE/name).read_bytes()).hexdigest()
        for name in ('build_adventure.py','build_compositor.py','relink_embedded.py','prepare_sm64_host.py','build_selection_art.py')}
    pin['launch_scripts']={name:hashlib.sha256((HERE/name).read_bytes()).hexdigest()
        for name in ('play_embedded.py','choose_character.py')}
    pin_path.write_text(json.dumps(pin,indent=2)+'\n')

if __name__=='__main__':build()
