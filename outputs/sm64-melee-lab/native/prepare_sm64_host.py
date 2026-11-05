#!/usr/bin/env python3
"""Apply the first host boundary to this chat's isolated SM64 source checkout."""
from pathlib import Path
import shutil,re,os
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
SOURCE=Path(os.environ.get('SMB_HOST_SOURCE_DIR',str(ROOT/'work/sm64ex-source')))
for name in ('sm64_air.inc','sm64_air_scenarios.inc','sm64_round_scenarios.inc','sm64_launch_coop.inc','sm64_finish_scenarios.inc','sm64_swim_control_scenarios.inc','sm64_options.inc','bridge_protocol.h','sm64_platform.h','sm64_host_bridge.c','sm64_gl_composite.inc','sm64_combat.inc','sm64_scenarios.inc','sm64_platform_fixture.inc','sm64_interaction_scenarios.inc','sm64_character_select.inc','sm64_npcs.inc','sm64_npc_environment.inc','sm64_npc_water.inc','sm64_campaign.inc','sm64_stage_starts.inc','sm64_video.inc','sm64_record_capture.inc','sm64_recording.m','sm64_app_bootstrap.m','sm64_recording_windows.cpp'):
    shutil.copy2(HERE/name,SOURCE/'src/pc'/name)

def edit(path,before,after):
    p=SOURCE/path;s=p.read_text()
    if after in s:return
    if s.count(before)!=1:raise ValueError('Unexpected source boundary: '+str(path))
    p.write_text(s.replace(before,after))

edit(Path('src/game/mario.c'),'s32 execute_mario_action(UNUSED struct Object *o) {',
     'int smb_host_frame(struct MarioState* m);\ns32 execute_mario_action(UNUSED struct Object *o) {')
edit(Path('src/game/mario.c'),'        mario_process_interactions(gMarioState);',
     '        if (smb_host_frame(gMarioState)) inLoop = FALSE;\n        mario_process_interactions(gMarioState);')
edit(Path('src/game/rendering_graph_node.c'),'static void geo_process_object(struct Object *node) {',
     'Gfx* smb_host_display_list(void);\nstatic void geo_process_object(struct Object *node) {')
edit(Path('src/game/rendering_graph_node.c'),'#include "rendering_graph_node.h"',
     '#include "rendering_graph_node.h"\n#include "object_list_processor.h"')
edit(Path('src/game/rendering_graph_node.c'),'            if (node->header.gfx.sharedChild != NULL) {',
     '''            Gfx* fighterMesh = node == gMarioObject ? smb_host_display_list() : NULL;
            if (fighterMesh != NULL) {
                /* Native skin matrices already contain world translation and
                 * facing. Append under the camera, without Mario's transform. */
                mtxf_to_mtx(mtx, gMatStack[gMatStackIndex - 1]);
                geo_append_display_list(fighterMesh, LAYER_OPAQUE);
            } else if (node->header.gfx.sharedChild != NULL) {''')
p=SOURCE/'src/pc/gfx/gfx_sdl2.c'
s=p.read_text().replace('"SM64 Melee Adventure"','"Super Melee 64"')
s=s.replace('getenv("SM64_MELEE_SOCKET") ? "Super Melee 64"',
    '(getenv("SM64_MELEE_SOCKET") || getenv("SM64_MELEE_LIBRARY")) ? "Super Melee 64"')
p.write_text(s)
edit(Path('src/pc/gfx/gfx_sdl2.c'),'        window_title,',
     '        (getenv("SM64_MELEE_SOCKET") || getenv("SM64_MELEE_LIBRARY")) ? "Super Melee 64" : window_title,')
# Normal adventure play and smoke checks enter the selected save directly.
# Upgrade the earlier smoke-only shortcut in an already prepared checkout.
for path in ('src/menu/level_select_menu.c','src/menu/file_select.c'):
    p=SOURCE/path
    p.write_text(p.read_text().replace('if (getenv("SMB_SMOKE") && arg1 == 1)',
        'if ((getenv("SMB_SMOKE") || getenv("SMB_QUICK_START")) && arg1 == 1)').replace(
        'if (getenv("SMB_SMOKE")) return 1;',
        'if (getenv("SMB_SMOKE") || getenv("SMB_QUICK_START")) return 1;'))
edit(Path('src/menu/level_select_menu.c'),'s32 lvl_intro_update(s16 arg1, UNUSED s32 arg2) {',
     '#include <stdlib.h>\nint smb_host_intro_finished(void);\ns32 lvl_intro_update(s16 arg1, UNUSED s32 arg2) {\n    if(arg1==1 && smb_host_intro_finished())return 100;')
edit(Path('src/menu/file_select.c'),'s32 lvl_update_obj_and_load_file_selected(UNUSED s32 arg, UNUSED s32 unused) {',
     '#include <stdlib.h>\ns32 lvl_update_obj_and_load_file_selected(UNUSED s32 arg, UNUSED s32 unused) {\n    if (getenv("SMB_SMOKE") || getenv("SMB_QUICK_START")) return 1;')
print(SOURCE)

# Event-backed host input avoids polling the hidden native SDL window's state.
edit(Path('src/pc/gfx/gfx_sdl2.c'),'static void gfx_sdl_onkeydown(int scancode) {',
     'void smb_host_key(int,int);\nstatic void gfx_sdl_onkeydown(int scancode) {\n    smb_host_key(scancode,1);')
edit(Path('src/pc/gfx/gfx_sdl2.c'),'static void gfx_sdl_onkeyup(int scancode) {',
     'static void gfx_sdl_onkeyup(int scancode) {\n    smb_host_key(scancode,0);')
edit(Path('src/pc/controller/controller_keyboard.c'),'void keyboard_on_all_keys_up(void) {',
     'void smb_host_key(int,int);\nvoid keyboard_on_all_keys_up(void) {\n    smb_host_key(-1,0);')
edit(Path('src/pc/controller/controller_keyboard.c'),'static int keyboard_buttons_down;',
     'static int keyboard_buttons_down;\nunsigned smb_host_keyboard_buttons(void){return keyboard_buttons_down;}')
p=SOURCE/'src/pc/controller/controller_sdl2.c'
s=p.read_text()
s=re.sub(r'^void smb_host_controller_(?:look|state)\([^\n]*\);\n','',s,flags=re.M)
s=re.sub(r'^    smb_host_controller_(?:look\(0,0\)|state\(0,0,0,0\));\n','',s,flags=re.M)
p.write_text(s)
edit(Path('src/pc/controller/controller_sdl2.c'),'static void controller_sdl_read(OSContPad *pad) {',
     'void smb_host_controller_look(float,float);\nvoid smb_host_controller_state(unsigned,float,float,int);\nstatic void controller_sdl_read(OSContPad *pad) {\n    smb_host_controller_look(0,0);\n    smb_host_controller_state(0,0,0,0);')
edit(Path('src/pc/controller/controller_sdl2.c'),'    int16_t righty = SDL_GameControllerGetAxis(sdl_cntrl, SDL_CONTROLLER_AXIS_RIGHTY);',
     '    int16_t righty = SDL_GameControllerGetAxis(sdl_cntrl, SDL_CONTROLLER_AXIS_RIGHTY);\n    smb_host_controller_look(rightx/32768.f,-righty/32768.f);')
edit(Path('src/pc/controller/controller_sdl2.c'),'    int16_t rtrig = SDL_GameControllerGetAxis(sdl_cntrl, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);',
     '    int16_t rtrig = SDL_GameControllerGetAxis(sdl_cntrl, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);\n    unsigned native_buttons=0;\n    for(int i=0;i<SDL_CONTROLLER_BUTTON_MAX;i++)if(SDL_GameControllerGetButton(sdl_cntrl,i))native_buttons|=1u<<i;\n    smb_host_controller_state(native_buttons,ltrig/32768.f,rtrig/32768.f,1);')
edit(Path('src/game/camera.c'),'void update_camera(struct Camera *c) {',
     'int smb_host_camera(struct Camera*);\nfloat smb_host_camera_fov(void);\nfloat smb_host_combat_fov(float);\nvoid smb_host_combat_focus(struct Camera*,int);\nvoid update_camera(struct Camera *c) {')
edit(Path('src/game/camera.c'),'    gCamera = c;\n    update_camera_hud_status(c);',
     '    gCamera = c;\n    if(smb_host_camera(c))return;\n    update_camera_hud_status(c);')
edit(Path('src/game/camera.c'),'    perspective->fov = sFOVState.fov;',
     '    perspective->fov = smb_host_camera_fov()>0?smb_host_camera_fov():smb_host_combat_fov(sFOVState.fov);')
edit(Path('src/game/camera.c'),'#define INCLUDED_FROM_CAMERA_C',
     'float smb_host_camera_distance(float);\n#define INCLUDED_FROM_CAMERA_C')
p=SOURCE/'src/game/camera.c';s=p.read_text()
if '    f32 baseDist = 1000.f;' in s:
    if s.count('    f32 baseDist = 1000.f;')!=3:raise ValueError('Unexpected stock radial camera distance boundaries')
    p.write_text(s.replace('    f32 baseDist = 1000.f;','    f32 baseDist = smb_host_camera_distance(1000.f);'))
edit(Path('src/game/camera.c'),'    f32 dist = 1000.f;', '    f32 dist = smb_host_camera_distance(1000.f);')
# Apply the preferred distance before the stock wall/floor/ceiling avoidance.
edit(Path('src/game/camera.c'),'s16 update_default_camera(struct Camera *c) {',
     '#include <stdlib.h>\n#include <stdio.h>\nfloat smb_host_camera_distance(float);\ns16 update_default_camera(struct Camera *c) {')
edit(Path('src/game/camera.c'),'    // If not zooming out, only allow dist to decrease',
     '    zoomDist = smb_host_camera_distance(zoomDist);\n    /* Recover the preferred follow distance after an obstruction clears. */\n    if(smb_host_camera_distance(1.f)>1.f && dist<zoomDist && sZoomAmount<30.f)sZoomAmount=30.f;\n    // If not zooming out, only allow dist to decrease')

# Reserve the coroutine's real main-thread stack. Boot after the host window.
p=SOURCE/'src/pc/pc_main.c'
p.write_text(p.read_text().replace('void smb_host_initialize(void);\nint main(int argc, char *argv[]) {\n    smb_host_initialize();',
    'void smb_host_initialize(void*,size_t);\nint main(int argc, char *argv[]) {\n#ifdef _WIN32\n    smb_host_initialize(NULL,0);\n#else\n    unsigned char smb_native_stack[4*1024*1024];\n    smb_host_initialize(smb_native_stack,sizeof smb_native_stack);\n#endif'))

edit(Path('src/pc/pc_main.c'),'int main(int argc, char *argv[]) {',
     'void smb_host_initialize(void*,size_t);\nint main(int argc, char *argv[]) {\n#ifdef _WIN32\n    smb_host_initialize(NULL,0);\n#else\n    unsigned char smb_native_stack[4*1024*1024];\n    smb_host_initialize(smb_native_stack,sizeof smb_native_stack);\n#endif')

edit(Path('src/pc/pc_main.c'),'    parse_cli_opts(argc, argv);',
     '    /* Cocoa reads these launch-only options during embedded initialization.\n     * Keep them out of SM64\'s own command-line parser. */\n    char* smb_cli[argc+1];int smb_argc=0;\n    for(int i=0;i<argc;i++) {\n        if((!strcmp(argv[i],"-ApplePersistenceIgnoreState") || !strcmp(argv[i],"-NSQuitAlwaysKeepsWindows")) && i+1<argc) {i++;continue;}\n        smb_cli[smb_argc++]=argv[i];\n    }\n    smb_cli[smb_argc]=NULL;\n    parse_cli_opts(smb_argc,smb_cli);')

p=SOURCE/'src/pc/gfx/gfx_sdl2.c'
p.write_text(p.read_text().replace('    if (getenv("SM64_MELEE_LIBRARY")) SDL_RaiseWindow(wnd);',
    '    if (getenv("SM64_MELEE_LIBRARY")) {SDL_RaiseWindow(wnd);SDL_SetWindowInputFocus(wnd);}'))
edit(Path('src/pc/gfx/gfx_sdl2.c'),'    ctx = SDL_GL_CreateContext(wnd);',
     '    ctx = SDL_GL_CreateContext(wnd);\n    if (getenv("SM64_MELEE_LIBRARY")) {SDL_RaiseWindow(wnd);SDL_SetWindowInputFocus(wnd);}')
edit(Path('src/pc/gfx/gfx_sdl2.c'),'static SDL_Window *wnd;',
     '#include "../sm64_character_select.inc"\nstatic SDL_Window *wnd;')
edit(Path('src/pc/gfx/gfx_sdl2.c'),'    if (getenv("SM64_MELEE_LIBRARY")) {SDL_RaiseWindow(wnd);SDL_SetWindowInputFocus(wnd);}',
     '    if (getenv("SM64_MELEE_LIBRARY")) {SDL_RaiseWindow(wnd);SDL_SetWindowInputFocus(wnd);}\n    if (getenv("SM64_MELEE_LIBRARY")) {smb_host_character_select(wnd);smb_host_boot();SDL_GL_MakeCurrent(wnd,ctx);SDL_RaiseWindow(wnd);SDL_SetWindowInputFocus(wnd);}')

edit(Path('src/pc/pc_main.c'),'void game_exit(void) {',
     'void smb_host_shutdown(void);\nvoid game_exit(void) {\n    smb_host_shutdown();')

edit(Path('src/pc/gfx/gfx_pc.c'),'static void gfx_run_dl(Gfx* cmd) {',
     'void smb_host_render_native(const float*,const float*,unsigned,unsigned,float);\nstatic void gfx_run_dl(Gfx* cmd) {')
edit(Path('src/pc/gfx/gfx_pc.c'),'            // RSP commands:',
     '            case 0xC1: /* Private native Melee display-list pass. */\n                gfx_flush();\n                smb_host_render_native(&rsp.modelview_matrix_stack[rsp.modelview_matrix_stack_size-1][0][0],\n                    &rsp.P_matrix[0][0],gfx_current_dimensions.width,gfx_current_dimensions.height,gfx_adjust_x_for_aspect_ratio(1));\n                break;\n            // RSP commands:')
p=SOURCE/'src/pc/gfx/gfx_opengl.c'
s=p.read_text()
if '#include "../sm64_gl_composite.inc"' not in s:
    index=s.rfind('#endif')
    s=s[:index]+'#include "../sm64_gl_composite.inc"\n'+s[index:]
    p.write_text(s)

edit(Path('src/game/mario.c'),'int smb_host_frame(struct MarioState* m);',
     'int smb_host_frame(struct MarioState* m);\nvoid smb_host_post_interactions(struct MarioState* m);')
edit(Path('src/game/mario.c'),'        mario_process_interactions(gMarioState);',
     '        mario_process_interactions(gMarioState);\n        smb_host_post_interactions(gMarioState);')
if 'SMB STAGE SELECTOR' not in (SOURCE/'src/menu/star_select.c').read_text():
    edit(Path('src/menu/star_select.c'),'s32 lvl_update_obj_and_load_act_button_actions(UNUSED s32 arg, UNUSED s32 unused) {',
         '#include <stdlib.h>\ns32 lvl_update_obj_and_load_act_button_actions(UNUSED s32 arg, UNUSED s32 unused) {\n    if(getenv("SMB_SCENARIO")&&getenv("SMB_SMOKE"))return 1;')
edit(Path('src/menu/star_select.c'),'#include <stdlib.h>','#include <stdlib.h>\n#include <stdio.h>\n#include <string.h>')
# Exercise the real selector in the progression regression: allow it to create
# its actual stars, select the next act, then confirm with the usual button.
edit(Path('src/menu/star_select.c'),'    if(getenv("SMB_SCENARIO")&&getenv("SMB_SMOKE"))return 1;',
     '''    if(getenv("SMB_SCENARIO")&&getenv("SMB_SMOKE")) {
        if(strcmp(getenv("SMB_SCENARIO"),"stage-stars"))return 1;
        gPlayer3Controller->buttonPressed&=~(A_BUTTON|B_BUTTON|START_BUTTON);
        if(sActSelectorMenuTimer==30)fprintf(stderr,"SMB STAGE SELECTOR course=%d flags=%02x next=%d\\n",gCurrCourseNum,save_file_get_star_flags(gCurrSaveFileNum-1,gCurrCourseNum-1),sInitSelectedActNum);
        if(sActSelectorMenuTimer>=45)gPlayer3Controller->buttonPressed|=A_BUTTON;
    }''')
edit(Path('src/game/ingame_menu.c'),'void render_dialog_entries(void) {',
     '#include <stdlib.h>\nvoid render_dialog_entries(void) {\n    if(getenv("SMB_SMOKE")&&getenv("SMB_SCENARIO"))gPlayer3Controller->buttonPressed|=A_BUTTON;')
edit(Path('src/game/hud.c'),'void render_hud(void) {',
     'int smb_host_damage_percent(void);\nvoid render_hud(void) {')
# Upgrade the original placeholder to the native percentage/portrait pass.
p=SOURCE/'src/game/hud.c'
p.write_text(p.read_text().replace('        if(configHUD&&smb_host_damage_percent()>=0)print_text_fmt_int(22,30,"DAMAGE %d",smb_host_damage_percent());\n\n',''))
p=SOURCE/'src/game/hud.c';p.write_text(p.read_text().replace('if(smb_host_damage_percent()<0||smb_host_air_meter())render_hud_power_meter();','if(smb_host_damage_percent()<0)render_hud_power_meter();'))
edit(Path('src/game/hud.c'),'            render_hud_power_meter();',
     '            if(smb_host_damage_percent()<0)render_hud_power_meter();')
edit(Path('src/game/mario.c'),'void update_mario_health(struct MarioState *m) {',
     'int smb_host_update_health(struct MarioState* m);\nvoid update_mario_health(struct MarioState *m) {\n    if(smb_host_update_health(m))return;')
edit(Path('src/game/interaction.c'),'void check_death_barrier(struct MarioState *m) {\n    if (m->pos[1] < m->floorHeight + 2048.0f) {',
     'float smb_host_death_clearance(void);\nvoid check_death_barrier(struct MarioState *m) {\n    if (m->pos[1] < m->floorHeight + smb_host_death_clearance()) {')
edit(Path('src/game/mario.c'),'void update_mario_geometry_inputs(struct MarioState *m) {',
     'void smb_host_recover_floor(struct MarioState* m);\nvoid update_mario_geometry_inputs(struct MarioState *m) {')
edit(Path('src/game/mario.c'),'    m->waterLevel = find_water_level(m->pos[0], m->pos[2]);',
     '    m->waterLevel = find_water_level(m->pos[0], m->pos[2]);\n    smb_host_recover_floor(m);')
edit(Path('src/game/level_update.c'),'s16 level_trigger_warp(struct MarioState *m, s32 warpOp) {',
     'void smb_host_warp_requested(struct MarioState*,int);\ns16 level_trigger_warp(struct MarioState *m, s32 warpOp) {\n    smb_host_warp_requested(m,warpOp);')
edit(Path('src/game/level_update.c'),'void initiate_delayed_warp(void) {',
     'int smb_host_star_exit_destination(s16*,s16*,s16*);\nvoid initiate_delayed_warp(void) {')
edit(Path('src/game/level_update.c'),'                default:\n                    warpNode = area_get_warp_node(sSourceWarpNodeId);',
     '''                default:
                    if(sDelayedWarpOp==WARP_OP_STAR_EXIT) {
                        s16 level,area,node;
                        if(smb_host_star_exit_destination(&level,&area,&node)) {
                            initiate_warp(level,area,node,0);
                            /* Same course still needs the level-script loop,
                             * whose first step is SM64's real star selector. */
                            sWarpDest.type=WARP_TYPE_CHANGE_LEVEL;
                            D_8032C9E0=0;
                            break;
                        }
                    }
                    warpNode = area_get_warp_node(sSourceWarpNodeId);''')
edit(Path('src/game/interaction.c'),'u32 interact_star_or_key(struct MarioState *m, UNUSED u32 interactType, struct Object *o) {',
     'int smb_host_recommended_course(void);\nu32 interact_star_or_key(struct MarioState *m, UNUSED u32 interactType, struct Object *o) {')
edit(Path('src/game/interaction.c'),'    u32 noExit = (o->oInteractionSubtype & INT_SUBTYPE_NO_EXIT) != 0;',
     '    u32 noExit = (o->oInteractionSubtype & INT_SUBTYPE_NO_EXIT) != 0 && !smb_host_recommended_course();')
edit(Path('src/game/mario.c'),'void smb_host_recover_floor(struct MarioState* m);',
     'void smb_host_recover_floor(struct MarioState* m);\nint smb_host_owns_collision(void);')
edit(Path('src/game/mario.c'),'    f32_find_wall_collision(&m->pos[0], &m->pos[1], &m->pos[2], 60.0f, 50.0f);\n    f32_find_wall_collision(&m->pos[0], &m->pos[1], &m->pos[2], 30.0f, 24.0f);',
     '    if(!smb_host_owns_collision()) {\n        f32_find_wall_collision(&m->pos[0], &m->pos[1], &m->pos[2], 60.0f, 50.0f);\n        f32_find_wall_collision(&m->pos[0], &m->pos[1], &m->pos[2], 30.0f, 24.0f);\n    }')
edit(Path('src/game/platform_displacement.c'),'void update_mario_platform(void) {',
     'struct Object* smb_host_hanging_platform(void);\nvoid update_mario_platform(void) {')
edit(Path('src/game/platform_displacement.c'),'    switch (awayFromFloor) {',
     '    struct Object* hanging=smb_host_hanging_platform();\n    if(hanging){gMarioPlatform=hanging;gMarioObject->platform=hanging;return;}\n    switch (awayFromFloor) {')
edit(Path('src/game/ingame_menu.c'),'s16 render_menus_and_dialogs() {',
     's16 render_menus_and_dialogs() {\n    if(getenv("SMB_SMOKE")&&getenv("SMB_SCENARIO"))gPlayer3Controller->buttonPressed|=A_BUTTON;')

# Grant a dedicated playtest profile through SM64's checksummed save API.
edit(Path('src/game/mario.c'),'void init_mario_from_save_file(void) {',
     'void smb_host_unlock_save(void);\nvoid init_mario_from_save_file(void) {\n    smb_host_unlock_save();')

# Native guard is invulnerable to SM64 object attacks; native shield drain and
# release still run normally. Fatal world hazards retain their normal handling.
edit(Path('src/game/interaction.c'),'    sInvulnerable = (m->action & ACT_FLAG_INVULNERABLE) || m->invincTimer != 0;',
     '    sInvulnerable = (m->action & ACT_FLAG_INVULNERABLE) || m->invincTimer != 0 || smb_host_shielded();')
edit(Path('src/game/interaction.c'),'void mario_process_interactions(struct MarioState *m) {',
     'int smb_host_shielded(void);\nvoid mario_process_interactions(struct MarioState *m) {')

# Render both worlds at the physical drawable resolution on Retina displays.
edit(Path('src/pc/gfx/gfx_sdl2.c'),'        SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE',
     '        SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | (getenv("SM64_MELEE_LIBRARY") ? SDL_WINDOW_ALLOW_HIGHDPI : 0)')
if 'smb_video_dimensions(wnd, &w, &h)' not in (SOURCE/'src/pc/gfx/gfx_sdl2.c').read_text():
    edit(Path('src/pc/gfx/gfx_sdl2.c'),'    SDL_GetWindowSize(wnd, &w, &h);',
         '    if(getenv("SM64_MELEE_LIBRARY"))SDL_GL_GetDrawableSize(wnd, &w, &h);else SDL_GetWindowSize(wnd, &w, &h);')
# Existing SM64 interaction sounds remain the authoritative world-event sounds.
# Optional tracing verifies requests reach the real host audio engine.
p=SOURCE/'src/audio/external.c'
p.write_text(p.read_text().replace('void smb_host_sound(unsigned);','int smb_host_sound(unsigned);').replace(
    '    smb_host_sound((unsigned)soundBits);','    if(smb_host_sound((unsigned)soundBits))return;'))
edit(Path('src/audio/external.c'),'void play_sound(s32 soundBits, f32 *pos) {',
     'int smb_host_sound(unsigned);\nvoid play_sound(s32 soundBits, f32 *pos) {\n    if(smb_host_sound((unsigned)soundBits))return;')

# Replacing Mario's mesh bypasses his hand node. Render held SM64 objects
# through the real held-object path, including HOLP updates used when throwing.
edit(Path('src/game/rendering_graph_node.c'),'Gfx* smb_host_display_list(void);',
     'Gfx* smb_host_display_list(void);\nvoid geo_process_held_object(struct GraphNodeHeldObject*);\nstatic Gfx* smb_held_transform(s32 context,struct GraphNode* node,void* matrix) {\n    if(context==GEO_CONTEXT_HELD_OBJ)get_pos_from_transform_mtx(gMarioState->marioBodyState->heldObjLastPosition,matrix,*gCurGraphNodeCamera->matrixPtr);\n    return NULL;\n}')
edit(Path('src/game/rendering_graph_node.c'),'#include "object_list_processor.h"', '#include "object_list_processor.h"\n#include "level_update.h"')

# Normalize the full replacement block, so evolving held-object support cannot
# cause repeated shadow insertion when this installer is rerun.
p=SOURCE/'src/game/rendering_graph_node.c';s=p.read_text()
a=s.find('            if (fighterMesh != NULL) {',s.index('geo_append_display_list(fighterMesh'))
b=s.index('            if (node->header.gfx.node.children != NULL) {',s.index('Gfx* fighterMesh ='))
if a<0:a=b
s=s[:a]+"""            if (fighterMesh != NULL) {
                struct GraphNodeShadow shadow = {0};
                shadow.shadowScale=100;shadow.shadowSolidity=0xB4;shadow.shadowType=SHADOW_CIRCLE_PLAYER;
                s32 oldAnim=gCurAnimEnabled;struct GraphNodeObject* oldOwner=gCurGraphNodeObject;
                gCurAnimEnabled=0;gCurGraphNodeObject=(struct GraphNodeObject*)node;
                geo_process_shadow(&shadow);
                if(gMarioState->heldObj) {
                    struct GraphNodeHeldObject held={0};held.objNode=gMarioState->heldObj;
                    held.fnNode.func=smb_held_transform;
                    held.translation[1]=240;held.translation[2]=gMarioState->marioBodyState->grabPos==GRAB_POS_BOWSER?1260:300;
                    geo_process_held_object(&held);
                }
                gCurGraphNodeObject=oldOwner;gCurAnimEnabled=oldAnim;
            }
"""+s[b:]
p.write_text(s)
edit(Path('src/game/hud.c'),'    print_text(GFX_DIMENSIONS_RECT_FROM_LEFT_EDGE(22), HUD_TOP_Y, ",");',
     '    if(smb_host_damage_percent()<0)print_text(GFX_DIMENSIONS_RECT_FROM_LEFT_EDGE(22), HUD_TOP_Y, ",");')
edit(Path('src/game/hud.c'),'void render_hud_mario_lives(void) {','int smb_host_damage_percent(void);\nvoid render_hud_mario_lives(void) {')

# Record the actual SM64 throw after its state and release position are set.
p=SOURCE/'src/game/interaction.c';s=p.read_text()
a=s.index('void mario_throw_held_object(struct MarioState *m) {');b=s.index('\nvoid ',a+10)
part=s[a:b]
if 'smb_host_thrown(m)' not in part:
    s=s[:a]+part.replace('m->heldObj = NULL;','smb_host_thrown(m);m->heldObj = NULL;')+s[b:]
if 'void smb_host_thrown(struct MarioState*);' not in s:s=s.replace('void mario_throw_held_object(', 'void smb_host_thrown(struct MarioState*);\nvoid mario_throw_held_object(',1)
p.write_text(s)

edit(Path('src/pc/gfx/gfx_opengl.c'),'static void gfx_opengl_end_frame(void) {', 'void smb_gl_capture_frame(void);\nstatic void gfx_opengl_end_frame(void) {\n    smb_gl_capture_frame();')

# Melee owns all shoulders/triggers. Keep SM64 camera exclusively on look/D-pad.
p=SOURCE/'src/pc/controller/controller_sdl2.c';s=p.read_text()
old='    pad->button |= buttons_down;'
new="""    if(getenv("SM64_MELEE_LIBRARY"))buttons_down=smb_host_camera_pad(native_buttons);
    pad->button |= buttons_down;"""
a=s.index('    if(getenv("SM64_MELEE_LIBRARY")) {') if '    if(getenv("SM64_MELEE_LIBRARY")) {' in s else -1
if a>=0:
    b=s.index('    pad->button |= buttons_down;',a)+len('    pad->button |= buttons_down;')
    s=s[:a]+old+s[b:]
s=s.replace('    if(getenv("SM64_MELEE_LIBRARY"))buttons_down=smb_host_camera_pad(native_buttons);\n','')
s=s.replace(old,new)
if 'unsigned smb_host_camera_pad(unsigned);' not in s:s=s.replace('void smb_host_controller_look(float,float);','unsigned smb_host_camera_pad(unsigned);\nvoid smb_host_controller_look(float,float);',1)
p.write_text(s)
p=SOURCE/'src/game/camera.c';s=p.read_text()
for field in ('buttonDown','buttonPressed'):
    s=s.replace('gPlayer1Controller->'+field+' & R_TRIG','smb_host_camera_r(gPlayer1Controller->'+field+')')
if 'int smb_host_camera_r(unsigned);' not in s:s=s.replace('int smb_host_camera(struct Camera*);','int smb_host_camera_r(unsigned);\nint smb_host_camera(struct Camera*);')
p.write_text(s)
# Place the healing hook after the stock coin accumulator, leaving other heals alone.
p=SOURCE/'src/game/interaction.c';s=p.read_text()
if 'void smb_host_coin(struct MarioState*,int);' not in s:s=s.replace('u32 interact_coin(', 'void smb_host_coin(struct MarioState*,int);\nu32 interact_coin(',1)
s=s.replace('    smb_host_coin(m,o->oDamageOrCoinValue);\n','')
a=s.rindex('u32 interact_coin(');b=s.index('\nu32 ',a+4)
part=s[a:b].replace('m->healCounter += 4 * o->oDamageOrCoinValue;', 'm->healCounter += 4 * o->oDamageOrCoinValue;\n    smb_host_coin(m,o->oDamageOrCoinValue);')
s=s[:a]+part+s[b:];p.write_text(s)

# Temporary focus bias is consumed by Lakitu smoothing, then removed so the
# following stock camera frame never accumulates a previous combat offset.
edit(Path('src/game/camera.c'),'    update_lakitu(c);',
     '    smb_host_combat_focus(c,1);\n    update_lakitu(c);\n    smb_host_combat_focus(c,0);')
p=SOURCE/'src/game/camera.c';s=p.read_text()
if 'float smb_host_combat_fov(float);' not in s:
    s=s.replace('float smb_host_camera_fov(void);','float smb_host_camera_fov(void);\nfloat smb_host_combat_fov(float);\nvoid smb_host_combat_focus(struct Camera*,int);')
s=s.replace(':sFOVState.fov;',':smb_host_combat_fov(sFOVState.fov);')
p.write_text(s)

# Capture the initial cannon vector at firing, before any later host action or
# collision can replace its position/velocity.
edit(Path('src/game/mario_actions_automatic.c'),'s32 act_in_cannon(struct MarioState *m) {',
     'void smb_host_cannon_fired(struct MarioState*);\ns32 act_in_cannon(struct MarioState *m) {')
edit(Path('src/game/mario_actions_automatic.c'),'                set_mario_action(m, ACT_SHOT_FROM_CANNON, 0);',
     '                set_mario_action(m, ACT_SHOT_FROM_CANNON, 0);\n                smb_host_cannon_fired(m);')

# Native macOS encoder; no helper process or desktop/audio permissions.
p=SOURCE/'Makefile';s=p.read_text()
if '# Super Melee 64 recording' not in s:
    s+='\n# Super Melee 64 recording\nifeq ($(OSX_BUILD),1)\nO_FILES += $(BUILD_DIR)/src/pc/sm64_recording.o\nLDFLAGS += -framework AVFoundation -framework AudioToolbox -framework CoreMedia -framework CoreVideo -framework Foundation\n$(EXE): $(BUILD_DIR)/src/pc/sm64_recording.o\n$(BUILD_DIR)/src/pc/sm64_recording.o: src/pc/sm64_recording.m\n\t@mkdir -p $(@D)\n\txcrun clang -O2 -fobjc-arc -fblocks -Wno-deprecated-declarations -c $< -o $@\nendif\n'
p.write_text(s)
edit(Path('src/pc/pc_main.c'),'    audio_api->play((u8 *)audio_buffer, 2 * num_audio_samples * 4);',
     '    smb_record_sm64_audio(audio_buffer, 2 * num_audio_samples);\n    audio_api->play((u8 *)audio_buffer, 2 * num_audio_samples * 4);')
p=SOURCE/'src/pc/pc_main.c';s=p.read_text()
if 'void smb_record_sm64_audio(const short*,unsigned);' not in s:s='void smb_record_sm64_audio(const short*,unsigned);\n'+s
p.write_text(s)
# Saves must be read from the selected profile only. A new profile must never
# fall through to the unlocked save that was mounted when the window opened.
p=SOURCE/'src/pc/ultra_reimplementation.c';s=p.read_text();a=s.index('s32 osEepromLongRead(');b=s.index('s32 osEepromLongWrite(',a)
part=s[a:b].replace('fs_file_t *fp = fs_open(SAVE_FILENAME);','FILE *fp = fopen(fs_get_write_path(SAVE_FILENAME), "rb");').replace('fs_read(fp, content, 512)','fread(content, 1, 512, fp)').replace('fs_close(fp);','fclose(fp);')
s=s[:a]+part+s[b:];p.write_text(s)

# Native Windows encoder uses the same C capture/audio API.
p=SOURCE/'Makefile';s=p.read_text()
if '# Super Melee 64 Windows recording' not in s:
    s+='\n# Super Melee 64 Windows recording\nifeq ($(WINDOWS_BUILD),1)\nLDFLAGS += -lmfplat -lmfreadwrite -lmfuuid -lole32\n$(EXE): $(BUILD_DIR)/src/pc/sm64_recording_windows.o\n$(BUILD_DIR)/src/pc/sm64_recording_windows.o: src/pc/sm64_recording_windows.cpp\n\t@mkdir -p $(@D)\n\t$(CXX) -std=c++17 -O2 -c $< -o $@\nendif\n'
p.write_text(s)

# Retain SM64's original logo, tiled title backdrop, star wipe and intro scripts.
# Only its Goddard head display list becomes a native selected-fighter pass.
p=SOURCE/'src/pc/gfx/gfx_sdl2.c';s=p.read_text().replace('#include "../sm64_title.inc"\n','').replace('smb_host_fighter_intro(wnd);','')
lines=s.splitlines();seen=False;clean=[]
for line in lines:
    if line=='#include "../sm64_character_select.inc"':
        if seen:continue
        seen=True
    clean.append(line)
p.write_text('\n'.join(clean)+'\n')
edit(Path('src/game/mario_misc.c'),'Gfx *geo_draw_mario_head_goddard(s32 callContext, struct GraphNode *node, Mat4 *c) {',
    'Gfx* smb_host_title_display_list(void);\nGfx *geo_draw_mario_head_goddard(s32 callContext, struct GraphNode *node, Mat4 *c) {\n    if(callContext==GEO_CONTEXT_RENDER){Gfx* fighter=smb_host_title_display_list();if(fighter)return fighter;}')
p=SOURCE/'src/pc/gfx/gfx_pc.c';s=p.read_text()
if 'case 0xC2:' not in s:
    s=s.replace('void smb_host_render_native(const float*,const float*,unsigned,unsigned,float);','void smb_host_render_title(unsigned,unsigned);\nvoid smb_host_render_native(const float*,const float*,unsigned,unsigned,float);')
    s=s.replace('            case 0xC1:', '            case 0xC2: /* Selected fighter in the original SM64 title scene. */\n                gfx_flush();smb_host_render_title(gfx_current_dimensions.width,gfx_current_dimensions.height);break;\n            case 0xC1:')
p.write_text(s)
p=SOURCE/'src/menu/level_select_menu.c';s=p.read_text()
s=s.replace('if ((getenv("SMB_SMOKE") || getenv("SMB_QUICK_START")) && arg1 == 1) return 100;',
    'if(arg1==1 && smb_host_intro_finished())return 100;')
if 'int smb_host_intro_finished(void);' not in s:s=s.replace('s32 lvl_intro_update(s16 arg1, UNUSED s32 arg2) {','int smb_host_intro_finished(void);\ns32 lvl_intro_update(s16 arg1, UNUSED s32 arg2) {')
p.write_text(s)

# Older local preparations may have inserted boot beside a changing include.
p=SOURCE/'src/pc/gfx/gfx_sdl2.c';lines=p.read_text().splitlines();seen=False;clean=[]
for line in lines:
    if 'smb_host_character_select(wnd);smb_host_boot();' in line:
        if seen:continue
        seen=True
    clean.append(line)
p.write_text('\n'.join(clean)+'\n')

# Tools run on the build machine, even when the game targets Windows.
p=SOURCE/'Makefile';s=p.read_text().replace('DUMMY != CC=$(CC) CXX=$(CXX) $(MAKE) -C tools -j1 >&2 || echo FAIL', 'DUMMY != CC="$(SMB_TOOLS_CC)" CXX="$(SMB_TOOLS_CXX)" $(MAKE) -C tools -j1 >&2 || echo FAIL')
if 'SMB_TOOLS_CC ?=' not in s:s='SMB_TOOLS_CC ?= cc\nSMB_TOOLS_CXX ?= c++\n'+s
if '# Super Melee 64 extra flags' not in s:s+='\n# Super Melee 64 extra flags\nCFLAGS += $(SMB_EXTRA_CFLAGS)\nLDFLAGS += $(SMB_EXTRA_LDFLAGS)\n'
p.write_text(s)

# Windows SDL headers do not indirectly supply these C standard declarations.
for name in ('src/pc/gfx/gfx_opengl.c','src/pc/audio/audio_sdl2.c'):
    p=SOURCE/name;s=p.read_text()
    for header in ('stdio.h','stdlib.h','math.h'):
        line='#include <'+header+'>\n'
        if line not in s:s=line+s
    p.write_text(s)


# Canonical bootstrap region also repairs old local preparations.
p=SOURCE/'src/pc/pc_main.c';s=p.read_text();prefix='int main(int argc, char *argv[]) {'
a=s.index(prefix)+len(prefix);b=s.index('    /* Cocoa reads these launch-only options',a)
bootstrap='\n#ifdef _WIN32\n    smb_host_initialize(NULL,0);\n#else\n    extern void smb_app_bootstrap(int*,char***);\n    smb_app_bootstrap(&argc,&argv);\n    unsigned char smb_native_stack[4*1024*1024];\n    smb_host_initialize(smb_native_stack,sizeof smb_native_stack);\n#endif\n'
s=s[:a]+bootstrap+s[b:];seen=False;clean=[]
for line in s.splitlines():
    if line=='void smb_host_initialize(void*,size_t);':
        if seen:continue
        seen=True
    clean.append(line)
p.write_text('\n'.join(clean)+'\n')

# Shared declarations can be separated from their original insertion boundary
# by later hooks. Keep one declaration across repeated clean preparations.
for name in ('src/game/mario.c','src/game/rendering_graph_node.c'):
    p=SOURCE/name;seen=set();clean=[]
    declarations={'void smb_host_recover_floor(struct MarioState* m);','int smb_host_frame(struct MarioState* m);','Gfx* smb_host_display_list(void);'}
    for line in p.read_text().splitlines():
        if line in declarations:
            if line in seen:continue
            seen.add(line)
        clean.append(line)
    p.write_text('\n'.join(clean)+'\n')

edit(Path('src/pc/gfx/gfx_sdl2.c'),'    run_one_game_iter();',
     '    run_one_game_iter();\n    int picker_action=smb_host_change_menu_ready();\n    if(picker_action) {\n        smb_host_select(wnd,picker_action);\n        if(kb_all_keys_up)kb_all_keys_up();\n        smb_host_change_menu_closed();\n        frame_time=SDL_GetPerformanceCounter();\n    }')
edit(Path('src/pc/gfx/gfx_sdl2.c'),'            case SDL_QUIT:',
     '            case SDL_CONTROLLERBUTTONDOWN:\n                if(event.cbutton.button==SDL_CONTROLLER_BUTTON_BACK)smb_host_change_menu_request();\n                break;\n            case SDL_QUIT:')

# A Finder launch configures the native executable before SDL or Melee starts.
p=SOURCE/'Makefile';s=p.read_text()
if '# Super Melee 64 native app bootstrap' not in s:
    s+='\n# Super Melee 64 native app bootstrap\nifeq ($(OSX_BUILD),1)\nO_FILES += $(BUILD_DIR)/src/pc/sm64_app_bootstrap.o\n$(EXE): $(BUILD_DIR)/src/pc/sm64_app_bootstrap.o\n$(BUILD_DIR)/src/pc/sm64_app_bootstrap.o: src/pc/sm64_app_bootstrap.m\n\t@mkdir -p $(@D)\n\txcrun clang -O2 -fobjc-arc -Wno-deprecated-declarations -c $< -o $@\nendif\n'
    p.write_text(s)

# A native fighter may legitimately have no floor during a launch or a warp.
edit(Path('src/game/paintings.c'),'gPaintingMarioFloorType = surface->type;',
     'gPaintingMarioFloorType = surface ? surface->type : SURFACE_DEFAULT;')

# One bounded scene target for SM64 and native fighters, presented to Retina.
p=SOURCE/'src/pc/gfx/gfx_sdl2.c';s=p.read_text()
if '#include "../sm64_video.inc"' not in s:
    s=s.replace('#ifdef WAPI_SDL2','#ifdef WAPI_SDL2\n#include <GL/glew.h>',1)
    s=s.replace('static SDL_Window *wnd;','static SDL_Window *wnd;\n#include "../sm64_video.inc"',1)
s=s.replace('if(getenv("SM64_MELEE_LIBRARY"))SDL_GL_GetDrawableSize(wnd, &w, &h);else SDL_GetWindowSize(wnd, &w, &h);','if(getenv("SM64_MELEE_LIBRARY"))smb_video_dimensions(wnd, &w, &h);else SDL_GetWindowSize(wnd, &w, &h);')
s=s.replace('static bool gfx_sdl_start_frame(void) {\n    return true;','static bool gfx_sdl_start_frame(void) {\n    smb_video_start(wnd);\n    return true;')
s=s.replace('glewInit();smb_host_character_select(wnd);','smb_host_character_select(wnd);')
s=s.replace('smb_host_character_select(wnd);smb_host_boot();','glewInit();smb_host_character_select(wnd);smb_host_boot();')
s=s.replace('static void gfx_sdl_main_loop(void (*run_one_game_iter)(void)) {\n    run_one_game_iter();','static void gfx_sdl_main_loop(void (*run_one_game_iter)(void)) {\n    smb_video_begin=SDL_GetPerformanceCounter();\n    run_one_game_iter();')
s=s.replace('    smb_video_present(wnd);\n','')
s=s.replace('    SDL_GL_SwapWindow(wnd);','    smb_video_present(wnd);\n    SDL_GL_SwapWindow(wnd);')
p.write_text(s)

# GLEW must be static from its first include in the Windows host.
p=SOURCE/'src/pc/gfx/gfx_sdl2.c';s=p.read_text()
s=s.replace('#ifdef WAPI_SDL2\n#include <GL/glew.h>', '#ifdef WAPI_SDL2\n#ifdef _WIN32\n#define GLEW_STATIC\n#endif\n#include <GL/glew.h>')
# Finish the last millisecond against the performance clock, rather than
# allowing scheduler oversleep to miss the next presentation interval.
boundary='static inline void sync_framerate_with_timer(void) {'
if 'SMB precise presentation deadline' not in s:
    s=s.replace(boundary,boundary+'''
    if(getenv("SM64_MELEE_LIBRARY")) { /* SMB precise presentation deadline */
        double deadline=frame_time+frame_rate,now=SDL_GetPerformanceCounter();
        double microseconds=(deadline-now)/perf_freq*1000000.0;
        if(microseconds>1200)sys_sleep(microseconds-1000);
        while((now=SDL_GetPerformanceCounter())<deadline) {}
        frame_time=microseconds>0?deadline:now;
        return;
    }''')
p.write_text(s)

# Pitch is a final rendered-view offset, with no feedback into stock history.
edit(Path('src/game/camera.c'),'void smb_host_combat_focus(struct Camera*,int);',
     'void smb_host_combat_focus(struct Camera*,int);\nvoid smb_host_camera_pose(struct Camera*);')
edit(Path('src/game/camera.c'),'    smb_host_combat_focus(c,0);',
     '    smb_host_combat_focus(c,0);\n    smb_host_camera_pose(c);')

# SM64's native power meter doubles as the breath meter in the water.
p=SOURCE/'src/game/hud.c';s=p.read_text()
if 'int smb_host_air_meter(void);' not in s:s='int smb_host_air_meter(void);\n'+s
s=s.replace('if(smb_host_damage_percent()<0)render_hud_power_meter();',
    'if(smb_host_damage_percent()<0||smb_host_air_meter())render_hud_power_meter();')
p.write_text(s)

# Keep the picker action (change player / add opponent) distinct.
p=SOURCE/'src/pc/gfx/gfx_sdl2.c';s=p.read_text()
s=s.replace('if(smb_host_change_menu_ready()) {\n        smb_host_select(wnd,1);',
    'int picker_action=smb_host_change_menu_ready();\n    if(picker_action) {\n        smb_host_select(wnd,picker_action);')
p.write_text(s)

p=SOURCE/'src/pc/gfx/gfx_sdl2.c';s=p.read_text()
block='    int picker_action=smb_host_change_menu_ready();\n    if(picker_action) {\n        smb_host_select(wnd,picker_action);\n        if(kb_all_keys_up)kb_all_keys_up();\n        smb_host_change_menu_closed();\n        frame_time=SDL_GetPerformanceCounter();\n    }\n'
while block+block in s:s=s.replace(block+block,block)
p.write_text(s)

# Native standing/landing is the equivalent shell pickup posture as walking.
p=SOURCE/'src/game/interaction.c';s=p.read_text()
if 'int smb_host_shell_mount(void);' not in s:s='int smb_host_shell_mount(void);\n'+s
s=s.replace('|| m->action == ACT_HOLD_WALKING) {', '|| m->action == ACT_HOLD_WALKING || smb_host_shell_mount()) {')
p.write_text(s)
