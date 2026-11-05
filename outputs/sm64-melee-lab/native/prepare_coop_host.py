#!/usr/bin/env python3
"""Connect the shared native bridge to a separate, pinned Coop Deluxe checkout.

Only work/sm64coopdx-melee is changed. The installed stock game and the
single-player engine remain independent of this network host.
"""
from pathlib import Path
import re,shutil,subprocess
from coop_network_safety import patched_sources
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[2]
SOURCE=ROOT/'work/sm64coopdx-melee'
def original(name):
    return subprocess.check_output(['git','-C',str(SOURCE),'show','HEAD:'+name],text=True)
def save(name,text): (SOURCE/name).write_text(text)
def replace(text,before,after):
    if text.count(before)!=1:raise ValueError('Coop source boundary: '+before[:90])
    return text.replace(before,after)
def replace_last(text,before,after):
    at=text.rindex(before);return text[:at]+after+text[at+len(before):]

def gameplay_hooks():
    # Each hook is restricted to the owning local fighter. Remote Mario
    # bookkeeping remains Coop Deluxe's authoritative network presentation.
    source=original('src/game/interaction.c')
    source='int smb_host_owns_collision(void);\nint smb_host_shielded(void);\nint smb_host_shell_mount(void);\nfloat smb_host_death_clearance(void);\nstruct MarioState;\nvoid smb_host_coin(struct MarioState*,int);\n'+source
    source=replace(source,'u32 take_damage_and_knock_back(struct MarioState *m, struct Object *o) {','u32 take_damage_and_knock_back(struct MarioState *m, struct Object *o) {\n    if(m&&m->playerIndex==0&&smb_host_shielded())return FALSE;')
    source=source.replace('m->healCounter += 4 * o->oDamageOrCoinValue;','m->healCounter += 4 * o->oDamageOrCoinValue;\n        if(m->playerIndex==0)smb_host_coin(m,o->oDamageOrCoinValue);',1)
    source=replace(source,'|| m->action == ACT_HOLD_WALKING) {','|| m->action == ACT_HOLD_WALKING || (m->playerIndex==0&&smb_host_shell_mount())) {')
    source=replace(source,'m->pos[1] < m->floorHeight + 2048.0f','m->pos[1] < m->floorHeight + (m->playerIndex==0?smb_host_death_clearance():2048.0f)')
    source=source.replace('gInteractionInvulnerable = (m->action & ACT_FLAG_INVULNERABLE) || m->invincTimer != 0;', 'gInteractionInvulnerable = (m->action & ACT_FLAG_INVULNERABLE) || m->invincTimer != 0 || (!m->playerIndex&&smb_host_shielded());')
    source=source.replace('u32 interact_player(struct MarioState* m, UNUSED u32 interactType, struct Object* o) {','u32 interact_player(struct MarioState* m, UNUSED u32 interactType, struct Object* o) {\n    if(smb_host_owns_collision())return FALSE;')
    source=source.replace('u32 interact_player_pvp(struct MarioState* attacker, struct MarioState* victim) {','u32 interact_player_pvp(struct MarioState* attacker, struct MarioState* victim) {\n    if(smb_host_owns_collision())return FALSE;')
    save('src/game/interaction.c',source)
    source=original('src/game/mario_actions_automatic.c')
    source='struct MarioState;\nvoid smb_host_cannon_fired(struct MarioState*);\n'+source
    source=replace(source,'                set_mario_action(m, ACT_SHOT_FROM_CANNON, 0);','                set_mario_action(m, ACT_SHOT_FROM_CANNON, 0);\n                if(m->playerIndex==0)smb_host_cannon_fired(m);')
    save('src/game/mario_actions_automatic.c',source)
    path=SOURCE/'src/game/mario.c';source=path.read_text()
    source='struct MarioState;\nint smb_host_owns_collision(void);\nvoid smb_host_recover_floor(struct MarioState*);\n'+source
    source=replace(source,'    f32_find_wall_collision(&m->pos[0], &m->pos[1], &m->pos[2], 60.0f, 50.0f);\n    f32_find_wall_collision(&m->pos[0], &m->pos[1], &m->pos[2], 30.0f, 24.0f);','    if(m->playerIndex||!smb_host_owns_collision()){\n        f32_find_wall_collision(&m->pos[0], &m->pos[1], &m->pos[2], 60.0f, 50.0f);\n        f32_find_wall_collision(&m->pos[0], &m->pos[1], &m->pos[2], 30.0f, 24.0f);\n    }')
    source=replace(source,'    m->waterLevel = find_water_level(m->pos[0], m->pos[2]);','    m->waterLevel = find_water_level(m->pos[0], m->pos[2]);\n    if(!m->playerIndex)smb_host_recover_floor(m);')
    source=source.replace('void init_mario_from_save_file(void) {','void smb_host_unlock_save(void);\nvoid init_mario_from_save_file(void) {\n    smb_host_unlock_save();')
    save('src/game/mario.c',source)
    source=original('src/game/platform_displacement.c')
    source='struct Object* smb_host_hanging_platform(void);\n'+source
    source=source.replace('        switch (awayFromFloor) {','        if(!i){struct Object* hanging=smb_host_hanging_platform();if(hanging){gMarioPlatform=hanging;player->platform=hanging;continue;}}\n        switch (awayFromFloor) {')
    save('src/game/platform_displacement.c',source)
    source=original('src/game/paintings.c').replace('gPaintingMarioFloorType = surface->type;','gPaintingMarioFloorType = surface?surface->type:SURFACE_DEFAULT;')
    save('src/game/paintings.c',source)
    source=original('src/game/hud.c')
    source='int smb_host_air_meter(void);\nint smb_host_damage_percent(void);\n'+source
    source=replace(source,'                render_hud_power_meter();','                if(smb_host_damage_percent()<0||smb_host_air_meter())render_hud_power_meter();')
    save('src/game/hud.c',source)
    source=original('src/game/camera.c')
    source='struct Camera;\nint smb_host_camera(struct Camera*);\nint smb_host_camera_r(unsigned);\nfloat smb_host_camera_distance(float);\nfloat smb_host_camera_fov(void);\nfloat smb_host_combat_fov(float);\nvoid smb_host_combat_focus(struct Camera*,int);\nvoid smb_host_camera_pose(struct Camera*);\n'+source
    source=source.replace('    f32 baseDist = 1000.f;','    f32 baseDist = smb_host_camera_distance(1000.f);').replace('    f32 dist = 1000.f;','    f32 dist = smb_host_camera_distance(1000.f);')
    source=replace(source,'    gCamera = c;\n    update_camera_hud_status(c);','    gCamera = c;\n    if(smb_host_camera(c))return;\n    update_camera_hud_status(c);')
    source=source.replace('    perspective->fov = sFOVState.fov;','    perspective->fov=smb_host_camera_fov()>0?smb_host_camera_fov():smb_host_combat_fov(sFOVState.fov);')
    for field in ('buttonDown','buttonPressed'):
        source=source.replace('gPlayer1Controller->'+field+' & R_TRIG','smb_host_camera_r(gPlayer1Controller->'+field+')')
    source=source.replace('    update_lakitu(c);','    smb_host_combat_focus(c,1);\n    update_lakitu(c);\n    smb_host_combat_focus(c,0);\n    smb_host_camera_pose(c);')
    save('src/game/camera.c',source)

def prepare():
    make='SMB_TOOLS_CC ?= cc\nSMB_TOOLS_CXX ?= c++\n'+original('Makefile').replace('DUMMY != $(MAKE) -C $(TOOLS_DIR)', 'DUMMY != $(MAKE) -C $(TOOLS_DIR) CC=$(SMB_TOOLS_CC) CXX=$(SMB_TOOLS_CXX)')
    make=make.replace('-Llib/lua/win64 -l:liblua53.a','$(SMB_LUA_LIB)')
    make+='\n# Private cross-build includes and libraries.\nCFLAGS += $(SMB_EXTRA_CFLAGS)\nCC_CHECK_CFLAGS += $(SMB_EXTRA_CFLAGS)\nLDFLAGS += $(SMB_EXTRA_LDFLAGS)\n'
    make+='''
# Shared in-process recorder.
ifeq ($(OSX_BUILD),1)
O_FILES += $(BUILD_DIR)/src/pc/sm64_recording.o
LDFLAGS += -framework AVFoundation -framework AudioToolbox -framework CoreMedia -framework CoreVideo -framework Foundation
$(EXE): $(BUILD_DIR)/src/pc/sm64_recording.o
$(BUILD_DIR)/src/pc/sm64_recording.o: src/pc/sm64_recording.m
	@mkdir -p $(@D)
	xcrun clang -O2 -fobjc-arc -fblocks -Wno-deprecated-declarations -c $< -o $@
endif
ifeq ($(WINDOWS_BUILD),1)
LDFLAGS += -lmfplat -lmfreadwrite -lmfuuid -lole32
endif
'''
    save('Makefile',make)
    for path in HERE.iterdir():
        if path.name in ('sm64_air.inc','sm64_air_scenarios.inc','sm64_round_scenarios.inc','bridge_protocol.h','sm64_platform.h','sm64_gl_composite.inc','sm64_npcs.inc','sm64_npc_environment.inc','sm64_npc_water.inc','sm64_campaign.inc','sm64_stage_starts.inc','sm64_combat.inc','sm64_options.inc','sm64_coop.inc','sm64_coop_transport.inc','sm64_coop_grabs.inc','sm64_coop_campaign.inc','sm64_launch_coop.inc','sm64_record_capture.inc','sm64_recording.m','sm64_recording_windows.cpp','sm64_finish_scenarios.inc','sm64_swim_control_scenarios.inc','sm64_interaction_scenarios.inc'):
            shutil.copy2(path,SOURCE/'src/pc'/path.name)
    source=(HERE/'sm64_host_bridge.c').read_text()
    source='#define SMB_COOP_CAMPAIGN 1\n#include "game/object_helpers.h"\n#include "behavior_data.h"\n#include "model_ids.h"\n'+source
    source=source.replace('void smb_record_toggle(void);', '#define WARP_OP_UNKNOWN_01 WARP_OP_LOOK_UP\n#define save_file_get_cannon_flags(file,course) save_file_is_cannon_unlocked(file,(course)+1)\n#define save_file_do_save(file) save_file_do_save(file, TRUE)\n#define find_surface_on_ray(a,b,c,d) find_surface_on_ray(a,b,c,d,1.f)\nstatic float gDefaultSoundArgs[3];\nvoid smb_record_toggle(void);')
    source=source.replace('extern s16 gDialogCourseActNum;', 'extern s8 gDialogCourseActNum;')
    source=source.replace('extern struct Object* gMarioPlatform;', 'extern struct Object* gMarioPlatform;\nstruct Surface* smb_coop_surface(unsigned);')
    source=source.replace('&sSurfacePool[i]', 'smb_coop_surface(i)').replace('sSurfacePool[j].object','smb_coop_surface(j)->object')
    source=source.replace('struct Surface* a=smb_coop_surface(i);unsigned slot=i;', 'struct Surface* a=smb_coop_surface(i);if(!a)continue;unsigned slot=i;')
    source=source.replace('if(smb_coop_surface(j)->object==a->object)', 'if(smb_coop_surface(j)&&smb_coop_surface(j)->object==a->object)')
    source=source.replace('#include "sm64_scenarios.inc"\n#include "sm64_interaction_scenarios.inc"\n#include "sm64_finish_scenarios.inc"',
        'static void smb_scenario(struct MarioState*m,SmbInput*c){if(!smb_coop_test_input(m,c))smb_coop_input(c);}\n#include "sm64_interaction_scenarios.inc"\n#include "sm64_finish_scenarios.inc"')
    source=source.replace('static int (*embedded_step)(const SmbInput*,SmbOutput*);','static int (*embedded_step)(const SmbInput*,SmbOutput*);\n#include "sm64_coop.inc"')
    # The include needs the completed snapshot and engine function declarations.
    source=source.replace('\n#include "sm64_coop.inc"','')
    boundary='static int character_menu_requested,character_input_latch;'
    source=source.replace(boundary,'#include "sm64_coop.inc"\n'+boundary)
    source=source.replace('#include "sm64_npcs.inc"','#include "sm64_npcs.inc"\n#include "sm64_coop_campaign.inc"\n#include "sm64_coop_transport.inc"')
    source=source.replace('static int (*embedded_remote)(unsigned,const SmbRemote*);','static int (*embedded_remote)(unsigned,const SmbRemote*);\nstatic int (*embedded_remote_articles)(unsigned,const SmbItem*,unsigned);\nstatic int (*embedded_campaign_snapshot)(unsigned,SmbRemote*);\nstatic int (*embedded_campaign_remote)(unsigned,const SmbRemote*);\nstatic int (*embedded_campaign_release)(unsigned);\nstatic int (*embedded_net_snapshot)(unsigned,SmbNetFrame*);\nstatic int (*embedded_net_hits)(unsigned,const SmbHit*,unsigned);\nstatic int (*embedded_net_event)(unsigned,uint32_t*,float*);\nstatic unsigned (*embedded_sounds)(unsigned,SmbSound*,unsigned);\nstatic int (*embedded_remote_sounds)(unsigned,const SmbSound*,unsigned);\nstatic void (*embedded_audio_enabled)(unsigned);\nstatic int (*embedded_grab_begin)(unsigned,unsigned,unsigned,unsigned,unsigned);\nstatic int (*embedded_grab_present)(unsigned,unsigned,unsigned,const SmbGrabState*);\nstatic int (*embedded_grab_snapshot)(unsigned,SmbGrabState*);\nstatic int (*embedded_grab_mash)(unsigned,unsigned,float,float);\nstatic int (*embedded_grab_input)(unsigned,uint32_t*,float*);\nstatic int (*embedded_grab_end)(unsigned,unsigned);\nstatic void (*embedded_grab_cancel_all)(void);')
    source=source.replace('    embedded_stair=dlsym', '    embedded_remote_articles=dlsym(handle,"smb_embed_remote_articles");\n    embedded_campaign_snapshot=dlsym(handle,"smb_embed_campaign_snapshot");\n    embedded_campaign_remote=dlsym(handle,"smb_embed_campaign_remote");\n    embedded_campaign_release=dlsym(handle,"smb_embed_campaign_release");\n    embedded_net_snapshot=dlsym(handle,"smb_embed_net_snapshot");\n    embedded_net_hits=dlsym(handle,"smb_embed_net_hits");\n    embedded_net_event=dlsym(handle,"smb_embed_net_event");\n    embedded_sounds=dlsym(handle,"smb_embed_sounds");\n    embedded_remote_sounds=dlsym(handle,"smb_embed_remote_sounds");\n    embedded_audio_enabled=dlsym(handle,"smb_embed_audio_enabled");\n    embedded_grab_begin=dlsym(handle,"smb_embed_grab_begin");\n    embedded_grab_present=dlsym(handle,"smb_embed_grab_present");\n    embedded_grab_snapshot=dlsym(handle,"smb_embed_grab_snapshot");\n    embedded_grab_mash=dlsym(handle,"smb_embed_grab_mash");\n    embedded_grab_input=dlsym(handle,"smb_embed_grab_input");\n    embedded_grab_end=dlsym(handle,"smb_embed_grab_end");\n    embedded_grab_cancel_all=dlsym(handle,"smb_embed_grab_cancel_all");\n    embedded_stair=dlsym')
    source=source.replace('int smb_host_frame(struct MarioState* m) {','int smb_host_frame(struct MarioState* m) {\n    if(m->playerIndex)return 0;\n    smb_coop_update();\n    smb_coop_test_prepare(m);')
    source=source.replace('    if(active)m->health=air_remaining;', '    if(active&&m->playerIndex==0)m->health=air_remaining;')
    source=source.replace('void smb_host_post_interactions(struct MarioState* m) {','void smb_host_post_interactions(struct MarioState* m) {\n    if(m->playerIndex)return;')
    source=source.replace('if(!embedded_step||!active||m->health<0x100)return 0;', 'if(m->playerIndex||!embedded_step||!active||m->health<0x100)return 0;')
    source=source.replace('gMarioState->pos[axis]', 'gMarioStates[0].pos[axis]').replace('gMarioState->faceAngle[1]*(6.28318530718f/65536.f)', 'gMarioStates[0].faceAngle[1]*(6.28318530718f/65536.f)')

    source+='\nvoid smb_host_title_frame_stub(void){}\n'
    save('src/pc/sm64_host_bridge.c',source)
    save('src/pc/djui/djui_text.c','#include <ctype.h>\n'+original('src/pc/djui/djui_text.c'))
    # SDL can report an already-held controller button before the first area
    # and its object lists exist. The disconnect shortcut must not traverse
    # that uninitialized list while the native fighter engine is booting.
    source=original('src/pc/djui/djui_panel_pause.c')
    source='#include "game/area.h"\n#include "game/object_list_processor.h"\n'+source
    source=replace(source,'if (gDjuiInMainMenu || find_object_with_behavior(bhvActSelector) != NULL)',
        'if (!gCurrentArea || !gObjectLists || gDjuiInMainMenu || find_object_with_behavior(bhvActSelector) != NULL)')
    save('src/pc/djui/djui_panel_pause.c',source)
    save('src/pc/controller/controller_sdl.c',original('src/pc/controller/controller_sdl.c').replace('    extern void WIN_UpdateKeymap(void);\n    WIN_UpdateKeymap();','    SDL_PumpEvents(); /* SDL owns keyboard layout refresh. */'))
    source=original('src/engine/surface_load.c')+'''
/* The live bridge needs the same stable static IDs as Coop's own pools. */
struct Surface* smb_coop_surface(unsigned index) {
    struct GrowingArray* pools[]={sSurfaceStaticPool,sSurfaceSOCPool,sSurfaceDynamicPool};
    for(unsigned p=0;p<3;p++)if(pools[p]){if(index<pools[p]->count)return pools[p]->buffer[index];index-=pools[p]->count;}
    return NULL;
}
''';save('src/engine/surface_load.c',source)
    npc=(HERE/'sm64_npcs.inc').read_text().replace('&sSurfacePool[i]','smb_coop_surface(i)').replace('if(s->type==SURFACE_DEATH_PLANE', 'if(!s||s->type==SURFACE_DEATH_PLANE')
    save('src/pc/sm64_npcs.inc',npc)
    source=original('src/game/mario.c')
    source='struct MarioState;\nint smb_host_frame(struct MarioState*);\nvoid smb_host_post_interactions(struct MarioState*);\nint smb_host_update_health(struct MarioState*);\n'+source
    source=replace(source,'        while (inLoop) {','        if(gMarioState->playerIndex==0&&smb_host_frame(gMarioState))inLoop=FALSE;\n        while (inLoop) {')
    source=replace(source,'void update_mario_health(struct MarioState *m) {','void update_mario_health(struct MarioState *m) {\n    if(smb_host_update_health(m))return;')
    source=replace(source,'        mario_process_interactions(gMarioState);','        mario_process_interactions(gMarioState);\n        if(gMarioState->playerIndex==0)smb_host_post_interactions(gMarioState);')
    save('src/game/mario.c',source)
    source=original('src/pc/pc_main.c')
    source=replace(source,'int main(int argc, char *argv[]) {','void smb_host_initialize(void*,size_t);\nint main(int argc, char *argv[]) {\n#ifdef _WIN32\n    smb_host_initialize(NULL,0);\n#else\n    unsigned char smb_stack[4*1024*1024];smb_host_initialize(smb_stack,sizeof smb_stack);\n#endif')
    source=replace(source,'void game_exit(void) {','void smb_host_shutdown(void);\nvoid game_exit(void) {\n    smb_host_shutdown();')
    source=source.replace('static u32 get_target_refresh_rate(void) {','static u32 get_target_refresh_rate(void) {\n    if(getenv("SM64_MELEE_LIBRARY"))return FRAMERATE;').replace('bool shouldDelay = configFramerateMode != RRM_UNLIMITED;', 'bool shouldDelay = getenv("SM64_MELEE_LIBRARY")||configFramerateMode != RRM_UNLIMITED;')
    source=source.replace('inline static void buffer_audio(void) {','inline static void buffer_audio(void) {\n    /* Fresh profiles can return from config loading before initializing the mixer. */\n    gMasterVolume=(f32)configMasterVolume/127.f*(f32)gLuaVolumeMaster/127.f;')
    source='void smb_record_sm64_audio(const short*,unsigned);\n'+source
    source=source.replace('        for (u16 i=0; i < ARRAY_COUNT(sAudioBuffer); i++) {\n            sAudioBuffer[i] *= gMasterVolume;\n        }', '')
    source=source.replace('    if (!shouldMute) {\n\n        gAudioApi->play(', '    for(u16 i=0;i<ARRAY_COUNT(sAudioBuffer);i++)sAudioBuffer[i]*=gMasterVolume;\n    smb_record_sm64_audio(sAudioBuffer,2*numAudioSamples);\n    if (!shouldMute) {\n        gAudioApi->play(')
    save('src/pc/pc_main.c',source)
    source='#include <GL/glew.h>\n'+original('src/pc/gfx/gfx_sdl.c')
    source=replace(source,'    ctx = SDL_GL_CreateContext(wnd);','    ctx = SDL_GL_CreateContext(wnd);\n    if(getenv("SM64_MELEE_LIBRARY")){extern void smb_host_boot(void);glewInit();smb_host_boot();SDL_GL_MakeCurrent(wnd,ctx);SDL_RaiseWindow(wnd);SDL_SetWindowInputFocus(wnd);}')
    source=replace(source,'static void gfx_sdl_onkeydown(int scancode) {','void smb_host_key(int,int);\nstatic void gfx_sdl_onkeydown(int scancode) {\n    smb_host_key(scancode,1);')
    source=replace(source,'static void gfx_sdl_onkeyup(int scancode) {','static void gfx_sdl_onkeyup(int scancode) {\n    smb_host_key(scancode,0);')
    picker=(HERE/'sm64_character_select.inc').read_text().replace('gCLIOpts.SkipIntro','gCLIOpts.skipIntro')
    save('src/pc/sm64_character_select.inc',picker)
    source=source.replace('static void gfx_sdl_main_loop(', '#include "../sm64_character_select.inc"\nstatic void gfx_sdl_main_loop(')
    source=source.replace('    run_one_game_iter();','    run_one_game_iter();\n    int picker=smb_host_change_menu_ready();\n    if(picker&&getenv("SMB_SELECTION_ART")){smb_host_select(wnd,picker);smb_host_change_menu_closed();}')
    save('src/pc/gfx/gfx_sdl.c',source)
    source=original('src/pc/gfx/gfx_pc.c')
    source=replace(source,'static void OPTIMIZE_O3 gfx_run_dl(Gfx* cmd) {','void smb_host_render_native(const float*,const float*,unsigned,unsigned,float);\nstatic void OPTIMIZE_O3 gfx_run_dl(Gfx* cmd) {')
    source=replace(source,'            // RSP commands:','            case 0xC1:\n                gfx_flush();smb_host_render_native(&rsp.modelview_matrix_stack[rsp.modelview_matrix_stack_size-1][0][0],&rsp.P_matrix[0][0],gfx_current_dimensions.width,gfx_current_dimensions.height,gfx_current_dimensions.x_adjust_ratio);break;\n            // RSP commands:')
    save('src/pc/gfx/gfx_pc.c',source)
    source=original('src/game/rendering_graph_node.c')
    source=replace(source,'static void geo_process_object(struct Object *node) {','Gfx* smb_host_display_list(void);\nint smb_coop_hidden(struct Object*);\nstatic void geo_process_object(struct Object *node) {')
    source=replace(source,'            if (node->header.gfx.sharedChild != NULL) {','''            Gfx* meleeMesh=node==gMarioStates[0].marioObj?smb_host_display_list():NULL;
            if(meleeMesh) {
                mtxf_to_mtx(mtx,gMatStack[gMatStackIndex-1]);
                geo_append_display_list(meleeMesh,LAYER_OPAQUE);
                struct GraphNodeShadow shadow={0};shadow.shadowScale=100;shadow.shadowSolidity=0xB4;shadow.shadowType=SHADOW_CIRCLE_PLAYER;
                struct GraphNodeObject* previous=gCurGraphNodeObject;u8 animation=gCurAnimEnabled;
                gCurGraphNodeObject=(struct GraphNodeObject*)node;gCurAnimEnabled=0;geo_process_shadow(&shadow);
                gCurGraphNodeObject=previous;gCurAnimEnabled=animation;
            } else if (!smb_coop_hidden(node)&&node->header.gfx.sharedChild != NULL) {''')
    source=source.replace('Gfx* smb_host_display_list(void);','''Gfx* smb_host_display_list(void);
void geo_process_held_object(struct GraphNodeHeldObject*);
static Gfx* smb_held_transform(s32 context,struct GraphNode* node,void* matrix) {
    if(context==GEO_CONTEXT_HELD_OBJ)get_pos_from_transform_mtx(gMarioStates[0].marioBodyState->heldObjLastPosition,matrix,*gCurGraphNodeCamera->matrixPtr);
    return NULL;
}''')
    source=source.replace('gCurGraphNodeObject=previous;gCurAnimEnabled=animation;','''if(gMarioStates[0].heldObj) {
                    struct GraphNodeHeldObject held={0};held.objNode=gMarioStates[0].heldObj;held.fnNode.func=smb_held_transform;
                    held.translation[1]=240;held.translation[2]=gMarioStates[0].marioBodyState->grabPos==GRAB_POS_BOWSER?1260:300;geo_process_held_object(&held);
                }
                gCurGraphNodeObject=previous;gCurAnimEnabled=animation;''')
    save('src/game/rendering_graph_node.c',source)
    source=original('src/pc/gfx/gfx_opengl.c')
    source+='#include "../sm64_gl_composite.inc"\n'
    source=replace(source,'static void gfx_opengl_end_frame(void) {','void smb_gl_capture_frame(void);\nstatic void gfx_opengl_end_frame(void) {\n    smb_gl_capture_frame();')
    include=(HERE/'sm64_gl_composite.inc').read_text()
    save('src/pc/sm64_gl_composite.inc',include)
    save('src/pc/gfx/gfx_opengl.c',source)
    source=original('src/pc/network/packets/packet_player.c')
    source='#include "../../bridge_protocol.h"\nvoid smb_coop_send(SmbRemote*);\nvoid smb_coop_receive(unsigned,const SmbRemote*);\n'+source
    source=replace(source,'#include "../network.h"','#include "../network.h"\n#include "../../sm64_coop_identity.h"')
    source=replace(source,'    s32 dialogId;','    s32 dialogId;\n    SmbRemote melee;')
    source=replace(source,'    data->dialogId = get_dialog_id();','    data->dialogId = get_dialog_id();\n    if(m->playerIndex==0)smb_coop_send(&data->melee);')
    source=replace(source,'    memcpy(m->marioObj->rawData.asU32, data->rawData, sizeof(u32) * OBJECT_NUM_REGULAR_FIELDS);','    smb_coop_receive(m->playerIndex,&data->melee);\n    memcpy(m->marioObj->rawData.asU32, data->rawData, sizeof(u32) * OBJECT_NUM_REGULAR_FIELDS);')
    # Validate before any raw field peek, forwarding or mutation. Socket hosts
    # authenticate the author; clients trust only authenticated host relays.
    source=replace(source,'void network_receive_player(struct Packet* p) {','void network_receive_player(struct Packet* p) {\n    if(!p||p->error||p->cursor>p->dataLength||p->dataLength-p->cursor!=1+sizeof(struct PacketPlayerData))return;')
    source=replace(source,'    struct NetworkPlayer* np = network_player_from_global_index(globalIndex);','    if(p->error||!smb_net_sender_allowed(p,globalIndex))return;\n    struct NetworkPlayer* np = network_player_from_global_index(globalIndex);')
    source=replace(source,'    if (gNetworkType == NT_SERVER && *((u32*)(p->buffer + p->cursor + offsetof(struct PacketPlayerData, action))) == ACT_DEBUG_FREE_MOVE) {','    u32 receivedAction=0;memcpy(&receivedAction,p->buffer+p->cursor+offsetof(struct PacketPlayerData,action),sizeof receivedAction);\n    if (gNetworkType == NT_SERVER && receivedAction == ACT_DEBUG_FREE_MOVE) {')
    save('src/pc/network/packets/packet_player.c',source)
    save('src/pc/network/packets/packet_custom.c','#include \"../network.h\"\nvoid smb_coop_packet_receive(struct Packet*);\nvoid network_receive_custom(struct Packet*p){smb_coop_packet_receive(p); }\nu8 network_register_custom_packet(void(*s)(struct Packet*,void*),void(*r)(struct Packet*)){(void)s;(void)r;return 0;}\nvoid network_send_custom(u8 id,bool reliable,enum PacketLevelMatchType match,void*params){(void)id;(void)reliable;(void)match;(void)params;}\n')
    # SDL keyboard mapper stays authoritative for the host, as in single player.
    source=original('src/pc/cliopts.c')
    source=replace(source,'            if ((i + 2) < argc) {','            if ((i + 1) < argc && argv[i+1][0]!=0x2d) {')
    save('src/pc/cliopts.c',source)
    source=original('src/audio/external.c')
    source=replace(source,'void play_sound(s32 soundBits, f32 *pos) {','int smb_host_sound(unsigned);\nvoid play_sound(s32 soundBits, f32 *pos) {\n    if(smb_host_sound((unsigned)soundBits))return;')
    source=source.replace('    synthesis_execute(gAudioCmdBuffers[0], &writtenCmds, samples, num_samples);','    synthesis_execute(gAudioCmdBuffers[0], &writtenCmds, samples, num_samples);\n    if(getenv("SMB_SMOKE")&&gAudioFrameCount%60==0){int peak=0;for(unsigned i=0;i<num_samples*2;i++)if(abs(samples[i])>peak)peak=abs(samples[i]);fprintf(stderr,"SMB HOST AUDIO synth_peak=%d notes=%d players=%u,%u,%u muted=%u,%u,%u seq=%u,%u,%u\\n",peak,gMaxSimultaneousNotes,gSequencePlayers[0].enabled,gSequencePlayers[1].enabled,gSequencePlayers[2].enabled,gSequencePlayers[0].muted,gSequencePlayers[1].muted,gSequencePlayers[2].muted,gSequencePlayers[0].seqId,gSequencePlayers[1].seqId,gSequencePlayers[2].seqId);}')
    save('src/audio/external.c',source)
    source=original('src/pc/network/version.h').replace('#define SM64COOPDX_VERSION "v1.5.1"','#define SM64COOPDX_VERSION "SuperMelee64-native14-beta1"').replace('"Super Mario 64 Coop Deluxe"','"Super Melee 64 - Co-op"')
    source=source.replace('"sm64coopdx-dev"','"supermelee64-dev"').replace('"sm64coopdx-intl"','"supermelee64-intl"').replace('"sm64coopdx"','"supermelee64"')
    save('src/pc/network/version.h',source)
    for name,boundary,addition in (
        ('src/menu/level_select_menu.c','s32 lvl_intro_update(s16 arg1, UNUSED s32 arg2) {','if(getenv("SMB_SMOKE")&&arg1==1)return 100;'),
        ('src/menu/file_select.c','s32 lvl_update_obj_and_load_file_selected(UNUSED s32 arg, UNUSED s32 unused) {','if(getenv("SMB_SMOKE"))return 1;')):
        source=original(name);source=replace(source,boundary,boundary+'\n    '+addition);save(name,source)
    source=original('src/pc/controller/controller_sdl.c')
    source='void smb_host_controller_state(unsigned,float,float,int);\nvoid smb_host_controller_look(float,float);\nunsigned smb_host_camera_pad(unsigned);\n'+source
    source=replace(source,'static void controller_sdl_read(OSContPad *pad) {','static void controller_sdl_read(OSContPad *pad) {\n    smb_host_controller_state(0,0,0,0);smb_host_controller_look(0,0);')
    source=replace_last(source,'    pad->button |= buttons_down;','''    if(getenv("SM64_MELEE_LIBRARY")&&sdl_cntrl) {
        unsigned physical=0;for(unsigned b=0;b<SDL_CONTROLLER_BUTTON_MAX;b++)if(SDL_GameControllerGetButton(sdl_cntrl,b))physical|=1u<<b;
        smb_host_controller_state(physical,ltrig/32767.f,rtrig/32767.f,1);
        smb_host_controller_look(rightx/32767.f,-righty/32767.f);
        pad->button|=smb_host_camera_pad(physical);
        update_analog_stick(&pad->stick_x,&pad->stick_y,leftx,lefty);return;
    }
    pad->button |= buttons_down;''')
    source=source.replace('    extern void WIN_UpdateKeymap(void);\n    WIN_UpdateKeymap();','    SDL_PumpEvents(); /* SDL owns keyboard layout refresh. */')
    save('src/pc/controller/controller_sdl.c',source)
    source=original('src/pc/controller/controller_keyboard.c')
    source+='\nunsigned smb_host_keyboard_buttons(void){return keyboard_buttons_down;}\n'
    save('src/pc/controller/controller_keyboard.c',source)
    source=original('src/menu/star_select.c');source=replace(source,'s32 lvl_update_obj_and_load_act_button_actions(UNUSED s32 arg, UNUSED s32 unused) {','s32 lvl_update_obj_and_load_act_button_actions(UNUSED s32 arg, UNUSED s32 unused) {\n    if(getenv("SMB_SMOKE")&&getenv("SMB_SCENARIO"))return 1;');save('src/menu/star_select.c',source)
    gameplay_hooks()
    for name,source in patched_sources(original).items():save(name,source)
if __name__=='__main__':prepare()
