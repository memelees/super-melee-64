/* Embedded native Melee: cooperative calls on the host's main thread.
 * Private build only; original fighter logic and installed apps are untouched.
 */
#define _XOPEN_SOURCE 700
#include "bridge_protocol.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <process.h>
#undef near
#undef far
#define getpid _getpid
#else
#include <ucontext.h>
#include <unistd.h>
#endif
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <melee/ft/types.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/ftlib.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/controller.h>
#include <pc/world/m3d.h>
#include <dolphin/os.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/mp/mpcoll.h>
#include <melee/ef/eflib.h>
#include <math.h>

#ifdef _WIN32
static void *host_context,*melee_context;
static int smb_switch_context(void** from,void** to){(void)from;SwitchToFiber(*to);return 0;}
#define swapcontext smb_switch_context
#else
static ucontext_t host_context,melee_context;
#endif
static SmbInput input;
static SmbOutput output;
static int initialized, ready, failed;
static unsigned ticks, previous_buttons,cannon_grace,cannon_held;
static HSD_PadStatus command_pad;
static unsigned switch_character,switch_costume;static int switch_pending;
extern unsigned smb_embed_costumes(unsigned);
extern int smb_native_replace_player(unsigned,unsigned);
int smb_embed_switch(unsigned character,unsigned costume) {
    if(!ready||!smb_embed_costumes(character)||costume>=smb_embed_costumes(character))return -1;
    switch_character=character;switch_costume=costume;switch_pending=1;return 0;
}
static OSTime virtual_time;
static const char* disc_path;
static Vec3 tick_origin,partner_origin;
static HSD_GObj* tick_partner;
static Vec3 npc_origin[3][2];
static HSD_GObj* tick_npc[3][2];
/* Transforming forms sleep; Ice Climbers' second fighter stays awake. */
static HSD_GObj* smb_partner(void) {
    HSD_GObj* p=Player_GetEntityAtIndex(0,1);
    return p&&p!=Player_GetEntity(0)&&!((Fighter*)p->user_data)->is_sleeping?p:NULL;
}
static void smb_translate(HSD_GObj* object,Vec3 delta,int reset) {
    Fighter* p=object->user_data;
    p->cur_pos.x+=delta.x;p->cur_pos.y+=delta.y;p->cur_pos.z+=delta.z;
    p->prev_pos=p->coll_data.cur_pos=p->coll_data.prev_pos=p->coll_data.last_pos=p->cur_pos;
    if(reset){p->self_vel=(Vec3){0};p->gr_vel=0;p->x8c_kb_vel=(Vec3){0};p->xF0_ground_kb_vel=0;}
    HSD_JObjSetTranslate(object->hsd_obj,&p->cur_pos);
}
extern void smb_native_attributes(Fighter*);
extern void smb_native_audio_tick(void);
extern void smb_embed_audio_enabled(unsigned);
extern void smb_native_grab_raw_input(const SmbInput*);
extern void smb_native_grab_before_tick(void);
extern void smb_native_grab_after_tick(void);
extern int smb_native_grab_borrowed(unsigned,unsigned);
extern void smb_native_remote_test(const SmbOutput*);
extern void smb_native_water_test(void);
static void smb_native_actor_world_test(void);
extern void smb_native_remote_state(Fighter*,SmbFighterState*);
extern void smb_native_remote_model(Fighter*,SmbFighterModel*);
extern int smb_native_remote_controlled(Fighter*);
extern void smb_native_powerups(void);
extern void smb_native_cap_render(HSD_GObj*,int);
extern int smb_native_cap_translucent(HSD_GObj*);
extern int smb_native_wall_flight(Fighter*);
extern void smb_native_encounter_test(void);
extern void smb_native_ko_audio_tick(void);
extern void smb_native_npc_levels(void);
extern void smb_native_environment(const SmbInput*,Fighter*);
extern void smb_native_trails(void);
extern void smb_native_begin_respawn(HSD_GObj*,float);
extern void smb_native_begin_launch(HSD_GObj*,const float*,int);
extern int smb_native_constrain(Fighter*,const Vec3*);
extern int smb_melee_main(int, char**);
extern OSTime smb_real_OSGetTime(void);
extern void smb_embed_stop_presentation(void);
extern void m3d_lab_tick_original(void);
extern void m3d_lab_pad_original(void);

int smb_embed_active(void) { return ready; }
int smb_embed_hud_all(void) { return getenv("SM64_MELEE_LIBRARY")!=NULL; }
OSTime OSGetTime(void) { return ready ? virtual_time : smb_real_OSGetTime(); }
void smb_embed_alarm_progress(void) { virtual_time+=OSMillisecondsToTicks(1); }
void m3d_lab_tick(void) { m3d_lab_tick_original();ticks++; }

#include "melee_items.inc"

static void bones(HSD_JObj* joint,SmbOutput* out,uintptr_t root_id,int hidden) {
    for(;joint;joint=joint->next) {
        if(out->bones>=SMB_BONES)abort();
        unsigned index=out->bones;
        HSD_JObjSetupMatrix(joint);
        memcpy(out->matrices[out->bones++],joint->mtx,sizeof(float)*12);
        uintptr_t id=joint->id;
        out->joint_ids[index]=id>=root_id&&id-root_id<0x1000000?(uint32_t)(id-root_id):UINT32_MAX;
        int invisible=hidden||(joint->flags&JOBJ_HIDDEN);
        unsigned ordinal=0;
        for(HSD_DObj* d=HSD_JObjGetDObj(joint);d;d=d->next,ordinal++) {
            if(ordinal>=128)abort();
            if(!invisible&&!(d->flags&DOBJ_HIDDEN))out->visible[index][ordinal/32]|=1u<<(ordinal%32);
        }
        if(joint->child)bones(joint->child,out,root_id,invisible);
    }
}

/* Runs after GObj physics, animation, and collision have all completed. */
void smb_embed_after_tick(void) {
    if(ticks<180)return;
    HSD_GObj* object=Player_GetEntity(0);if(!object)return;
    Fighter* fp=object->user_data;
    static int previous_kind=-1;
    if(previous_kind!=fp->kind){previous_kind=fp->kind;smb_native_attributes(fp);}
    if(!ready) {
        smb_embed_stop_presentation();
        virtual_time=smb_real_OSGetTime();ready=1;
        HSD_PadFlushQueue(HSD_PAD_FLUSH_QUEUE_LEAVE1);
        fprintf(stderr,"SMB EMBED READY pid=%d kind=%d ticks=%u\n",getpid(),fp->kind,ticks);
        fprintf(stderr,"SMB FIGHTER ABILITY wall_jump=%u\n",fp->can_walljump);
    } else {
        smb_native_audio_tick();
        smb_native_ko_audio_tick();
        smb_native_trails();
        smb_native_encounter_test();
        smb_native_grab_after_tick();
        if(!smb_native_remote_controlled(fp)&&!smb_native_grab_borrowed(0,0))smb_native_constrain(fp,&tick_origin);
        HSD_GObj* partner=smb_partner();
        if(partner&&partner==tick_partner&&!smb_native_remote_controlled(partner->user_data)&&!smb_native_grab_borrowed(0,1))smb_native_constrain(partner->user_data,&partner_origin);
        for(unsigned seat=0;seat<3;seat++)for(unsigned sub=0;sub<2;sub++) {
            HSD_GObj* npc=Player_GetEntityAtIndex(seat+1,sub);
            if(npc&&npc==tick_npc[seat][sub]&&!((Fighter*)npc->user_data)->is_sleeping&&!smb_native_remote_controlled(npc->user_data)&&!smb_native_grab_borrowed(seat+1,sub))smb_native_constrain(npc->user_data,&npc_origin[seat][sub]);
        }
        if(getenv("SMB_EFFECT_PROBE")&&ticks%20==0) {
            Vec3 point=fp->cur_pos;point.x+=20;point.y+=10;
            efLib_CreateGenerator(0x3f,&point);
        }
        smb_native_water_test();
        smb_native_actor_world_test();
        memset(&output,0,sizeof output);SmbOutput* destination=&output;
        SmbOutput out={0};out.magic=SMB_MAGIC;out.sequence=input.sequence;
        out.reserved=ticks-180;
        out.kind=fp->kind;out.motion=fp->motion_id;out.grounded=fp->ground_or_air==0;
        out.character=Player_GetPlayerCharacter(0);out.costume=Player_GetCostumeId(0);
        out.x=fp->cur_pos.x;out.y=fp->cur_pos.y;out.z=fp->cur_pos.z;
        out.vx=fp->cur_pos.x-tick_origin.x;out.vy=fp->cur_pos.y-tick_origin.y;out.vz=fp->cur_pos.z-tick_origin.z;
        if(getenv("SMB_SCENARIO")&&!strcmp(getenv("SMB_SCENARIO"),"slope"))fprintf(stderr,"SMB NATIVE FOOT seq=%u root=%.4f bottom=%.4f foot=%.4f\n",input.sequence,fp->cur_pos.y,fp->coll_data.ecb.bottom.y,fp->cur_pos.y+fp->coll_data.ecb.bottom.y);
        if(smb_native_wall_flight(fp))out.abilities|=SMB_WALL_KICK;
        if(fp->fall_fast)out.abilities|=4;
        out.shielding=ftLib_IsShielding(object)||fp->motion_id==ftCo_MS_GuardReflect;
        float lx=1,lz=0;m3d_GetLane(fp,&lx,&lz);
        out.facing[0]=lx*fp->facing_dir;out.facing[2]=lz*fp->facing_dir;
        out.percent=fp->dmg.x1830_percent;out.animation=fp->cur_anim_frame;
        if(getenv("SMB_INPUT_TRACE"))fprintf(stderr,"SMB NATIVE INPUT seq=%u buttons=%x trigger=%x local_y=%.3f move=%d motion=%d y=%.3f\n",input.sequence,input.buttons,command_pad.trigger,fp->input.lstick[0].y,fp->x2068_attackID,fp->motion_id,fp->cur_pos.y);
        for(int i=0;i<4;i++)if(fp->x914[i].state)out.hitboxes|=1u<<i;
        HSD_GObj* synced_partner=smb_partner();
        if(synced_partner&&!((Fighter*)synced_partner->user_data)->is_sleeping){out.partner_active=1;smb_native_remote_state(synced_partner->user_data,&out.partner);}
        smb_collect_items(&out,fp);
        smb_native_remote_test(&out);
        HSD_JObj* root=object->hsd_obj;
        /* Part masks are normally applied immediately before native drawing,
         * after this simulation callback. Apply that same normal render group
         * before taking the snapshot, so unused weapons/alternate hands stay
         * hidden in the SM64 renderer as they do in Melee. */
        ftParts_800750C8(fp,1,0);ftParts_800750C8(fp,2,0);
        ftParts_800750C8(fp,4,0);ftParts_800750C8(fp,0,1);
        smb_native_remote_model(fp,&out.model);
        bones(root,&out,root->id,0);
        *destination=out;
    }
    if(swapcontext(&melee_context,&host_context))abort();
    if(input.magic!=SMB_MAGIC)abort();
    if(switch_pending) {
        /* Locals across swapcontext refer to the previous fighter. Reacquire
         * them before processing this tick or any native callbacks. */
        if(smb_native_replace_player(switch_character,switch_costume))abort();
        switch_pending=0;object=Player_GetEntity(0);fp=object->user_data;
        previous_kind=fp->kind;previous_buttons=0;tick_partner=NULL;
        cannon_grace=0;cannon_held=0;
    }
    smb_native_grab_raw_input(&input);
    smb_native_powerups();
    smb_native_npc_levels();
    virtual_time+=OSMillisecondsToTicks(1000)/60;
    if(input.flags&1){m3d_CameraSetMode(2);m3d_CameraSetYaw(input.camera_yaw);m3d_CameraSetPitch(.663225116f);}
    if((getenv("SMB_CAMERA")&&!strcmp(getenv("SMB_CAMERA"),"sm64"))||
       (getenv("SMB_SMOKE")&&getenv("SMB_SCENARIO")&&strcmp(getenv("SMB_SCENARIO"),"controls")&&strcmp(getenv("SMB_SCENARIO"),"controller")))
        m3d_CameraSetYaw(input.camera_yaw); /* Test fixtures supply an explicit world heading. */
    if(input.flags&32) {
        input.stick_x=input.stick_y=0;
        input.buttons&=0x400; /* Jump can leave a slide; walking cannot. */
        if(!input.buttons)input.buttons|=0x40; /* Hold the real crouch pose. */
    }
    if(input.flags&1){cannon_grace=0;cannon_held=0;}
    if(input.flags&256){cannon_grace=18;cannon_held=input.buttons&0x700;}
    cannon_held&=input.buttons;input.buttons&=~cannon_held;
    if(cannon_grace){cannon_grace--;input.buttons=0;input.stick_x=input.stick_y=0;}
    /* Every consumer within this native tick sees the same button edge.
     * Camera and companion input sampling must not consume it first. */
    memset(&command_pad,0,sizeof command_pad);
    unsigned pad_buttons=input.buttons&SMB_PAD_BUTTONS;
    command_pad.button=pad_buttons;command_pad.last_button=previous_buttons;
    command_pad.trigger=pad_buttons&~previous_buttons;
    command_pad.release=previous_buttons&~pad_buttons;
    command_pad.repeat=command_pad.trigger;
    command_pad.nml_stickX=input.stick_x;command_pad.nml_stickY=input.stick_y;
    command_pad.stickX=(int8_t)(input.stick_x*80);command_pad.stickY=(int8_t)(input.stick_y*80);
    command_pad.nml_subStickX=input.look_x;command_pad.nml_subStickY=input.look_y;
    command_pad.subStickX=(int8_t)(input.look_x*80);command_pad.subStickY=(int8_t)(input.look_y*80);
    previous_buttons=pad_buttons;
    HSD_GObj* partner=smb_partner();
    Vec3 old_position=fp->cur_pos;
    if(input.flags&1) {
        m3d_ExitLocal(fp);
        fp->input_disabled=false;fp->is_sleeping=false;
        fp->xD4_unk_vel=(Vec3){0};
        fp->cur_pos=(Vec3){input.x,input.y,input.z};
        fp->prev_pos=fp->coll_data.cur_pos=fp->coll_data.prev_pos=fp->coll_data.last_pos=fp->cur_pos;
        fp->self_vel=(Vec3){0};fp->gr_vel=0;
        HSD_JObjSetTranslate(object->hsd_obj,&fp->cur_pos);
        if(input.flags&4) {
            fp->facing_dir=1;fp->x2064_ledgeCooldown=0;fp->x213C=-1;
            fp->x8c_kb_vel=(Vec3){0};fp->xF0_ground_kb_vel=0;
            mpColl_80043680(&fp->coll_data,&fp->cur_pos);
            fp->coll_data.floor.index=-1;fp->coll_data.env_flags=0;
            ftCommon_8007D5D4(fp);ftCo_Fall_Enter(object);fp->self_vel.y=-.1f;
            m3d_SetLane(fp,-sinf(input.camera_yaw),-cosf(input.camera_yaw));
            if(input.flags&8)fp->self_vel.x=2.5f;
        }
    } else if(input.flags&2) {
        Vec3 delta={input.x,input.y,input.z};
        fp->cur_pos.x+=delta.x;fp->cur_pos.y+=delta.y;fp->cur_pos.z+=delta.z;
        fp->prev_pos=fp->coll_data.cur_pos=fp->coll_data.prev_pos=fp->coll_data.last_pos=fp->cur_pos;
        HSD_JObjSetTranslate(object->hsd_obj,&fp->cur_pos);
    }
    if(input.flags&16) {
        float lx,lz;if(m3d_GetLane(fp,&lx,&lz)) {
            float c=cosf(input.platform_yaw),s=sinf(input.platform_yaw);
            m3d_SetLane(fp,lx*c+lz*s,-lx*s+lz*c);
        }
    }
    if(partner&&(input.flags&3)) {
        Vec3 delta={fp->cur_pos.x-old_position.x,fp->cur_pos.y-old_position.y,fp->cur_pos.z-old_position.z};
        smb_translate(partner,delta,(input.flags&1)!=0);
        if(input.flags&16){float lx,lz;if(m3d_GetLane(partner->user_data,&lx,&lz)){float c=cosf(input.platform_yaw),s=sinf(input.platform_yaw);m3d_SetLane(partner->user_data,lx*c+lz*s,-lx*s+lz*c);}}
    }
    if(input.flags&64) {
        smb_native_begin_respawn(object,input.spawn_headroom);
        if(partner)smb_native_begin_respawn(partner,input.spawn_headroom);
    }
    if(input.flags&128) {
        smb_native_begin_launch(object,input.slope_velocity,(input.flags&256)!=0);
        if(partner)smb_native_begin_launch(partner,input.slope_velocity,(input.flags&256)!=0);
    }
    smb_native_environment(&input,fp);
    tick_partner=partner;if(partner)partner_origin=((Fighter*)partner->user_data)->cur_pos;
    smb_native_grab_before_tick();
    tick_origin=fp->cur_pos;
    for(unsigned seat=0;seat<3;seat++)for(unsigned sub=0;sub<2;sub++) {
        HSD_GObj* npc=Player_GetEntityAtIndex(seat+1,sub);tick_npc[seat][sub]=npc;
        if(npc)npc_origin[seat][sub]=((Fighter*)npc->user_data)->cur_pos;
    }
}
void m3d_lab_pad(void) {
    if(!smb_embed_active()){m3d_lab_pad_original();return;}
    HSD_PadGameStatus[0]=command_pad;
}

static void game_entry(void) {
    char* args[]={"embedded-melee","--no-card",(char*)disc_path,NULL};
    int rc=smb_melee_main(3,args);
    fprintf(stderr,"SMB EMBED FAIL native entry returned=%d\n",rc);
    failed=1;
    if(swapcontext(&melee_context,&host_context))abort();
    abort();
}
#ifdef _WIN32
static void WINAPI smb_fiber_entry(void* unused){(void)unused;game_entry();}
#endif
int smb_embed_init(uint32_t abi,const char* disc,void* stack,size_t size) {
    if(abi!=SMB_MAGIC||initialized||!disc)return -1;
    initialized=1;disc_path=disc;
    /* Metal's diagnostic backtraces require the main thread's real stack
     * bounds. The host reserves this live region in main(), keeping both
     * cooperative contexts inside those bounds. */
#ifdef _WIN32
    (void)stack;(void)size;
    extern void m3d_guest_init(void);m3d_guest_init();
    host_context=IsThreadAFiber()?GetCurrentFiber():ConvertThreadToFiberEx(NULL,FIBER_FLAG_FLOAT_SWITCH);
    if(!host_context)return -2;
    melee_context=CreateFiberEx(1024*1024,4*1024*1024,FIBER_FLAG_FLOAT_SWITCH,smb_fiber_entry,NULL);
    if(!melee_context)return -3;
#else
    if(!stack||size<1024*1024)return -2;
    if(getcontext(&melee_context))return -3;
    melee_context.uc_stack.ss_sp=stack;
    melee_context.uc_stack.ss_size=size;
    melee_context.uc_link=&host_context;
    makecontext(&melee_context,game_entry,0);
#endif
    if(swapcontext(&host_context,&melee_context))return -4;
    return ready&&!failed?0:-5;
}
int smb_embed_step(const SmbInput* command,SmbOutput* result) {
    if(!ready||failed||!command||!result||command->magic!=SMB_MAGIC)return -1;
    input=*command;
    if(swapcontext(&host_context,&melee_context)||failed)return -2;
    *result=output;
    return output.sequence==command->sequence?0:-3;
}

/* The host calls this before its SDL/audio teardown. atexit then sees the
 * platform shutdown's existing once flag and does no second destruction. */
extern void pc_shutdown_once(void);
void smb_embed_shutdown(void) {
    smb_embed_audio_enabled(0);
    if(initialized)pc_shutdown_once();
}

#include <aurora/gfx.h>
#include <melee/ft/ftdrawcommon.h>
#include <melee/cm/camera.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/lobj.h>
#include <sysdolphin/baselib/tev.h>
#include <sysdolphin/baselib/state.h>
#include <sysdolphin/baselib/initialize.h>
#include <sysdolphin/baselib/gobjgxlink.h>
#include <melee/if/ifall.h>
#include <melee/if/ifstatus.h>
#include <melee/if/ifstock.h>
#include <melee/if/types.h>
int smb_embed_camera(float aspect,SmbCamera* result) {
    if(!ready||!result)return -1;
    static HSD_CObj* cobj;if(!cobj)cobj=HSD_CObjAlloc();
    HSD_CObjSetPerspective(cobj,65,fmaxf(.5f,aspect));
    if(!m3d_CameraApply(cobj))return -2;
    Vec3 eye,target;HSD_CObjGetEyePosition(cobj,&eye);HSD_CObjGetInterest(cobj,&target);
    memcpy(result->eye,&eye,sizeof eye);memcpy(result->target,&target,sizeof target);
    result->fov=HSD_CObjGetFov(cobj);result->yaw=m3d_CameraGetYaw();
    return 0;
}
extern HSD_GObj* smb_native_celebrant(unsigned);
extern int smb_native_victory_item_hidden(HSD_GObj*);
int smb_native_rendering,smb_native_hud_rendering,smb_native_effect_depth;
extern int smb_gpu_begin(unsigned,unsigned);
extern int smb_gpu_end(float*,float*,unsigned char*);
#include "melee_lives.inc"
#include "melee_composite_probe.inc"
extern int smb_native_npc_visible(unsigned);
extern void smb_native_ground_markers(void);
#include "melee_hud_layout.inc"
static void smb_render_hud(const SmbRender* request) {
    smb_hud_layout_begin(request);
    /* Draw only the real damage digits and character emblem using Melee's
     * authored HUD camera. Pin their depth to the front of the host buffer. */
    HSD_GObj* previous=HSD_GObj_804D7814;
    HSD_GObj* hud_camera=ifAll_GetHUDGObj();
    IfDamageState* hud=&ifStatus_GetHUDInfo()->players[0];
    if(hud_camera&&HSD_CObjSetCurrent(hud_camera->hsd_obj)) {
        smb_native_hud_rendering=1;
        GXSetViewport(0,0,640,480,0,0);
        GXSetScissor(0,0,640,480);
        HSD_StateInvalidate(-1);HSD_StateInitTev();
        for(unsigned seat=0;seat<GM_MAX_PLAYERS;seat++)if(smb_hud_visible(seat,request)) {
            IfDamageState* panel=&ifStatus_GetHUDInfo()->players[seat];
            HSD_GObj* objects[]={panel->next,panel->HUD_parent_entity};
            for(unsigned i=0;i<2;i++)smb_hud_place(objects[i],seat);
            for(int pass=0;pass<3;pass++)for(unsigned i=0;i<2;i++)if(objects[i]&&objects[i]->render_cb) {
                HSD_GObj_804D7814=objects[i];objects[i]->render_cb(objects[i],pass);
            }
            static unsigned logged;
            if(!(logged&(1u<<seat))) {
                logged|=1u<<seat;
                fprintf(stderr,"SMB HUD SLOT slot=%u character=%d damage_digits=%d emblem=%d x=%.3f\n",seat,Player_GetPlayerCharacter(seat),panel->HUD_parent_entity!=NULL,panel->next!=NULL,panel->HUD_parent_entity?HSD_JObjGetTranslationX(panel->HUD_parent_entity->hsd_obj):0);
            }
        }
        for(int pass=0;pass<3;pass++)for(HSD_GObj* object=HSD_GObjPLinkHead[15];object;object=object->next)if(object->render_cb==fn_802F94E0) {
            struct IfStockUserData* data=object->user_data;
            if(data&&!smb_hud_visible(data->player,request))continue;
            if(data&&pass==0)smb_hud_place(object,data->player);
            HSD_GObj_804D7814=object;object->render_cb(object,pass);static unsigned logged_icons;
            if(data&&data->player<32&&!(logged_icons&(1u<<data->player))) {
                logged_icons|=1u<<data->player;fprintf(stderr,"SMB HUD ICON slot=%u character=%d native_stock=1\n",data->player,Player_GetPlayerCharacter(data->player));
            }
        }
        smb_hud_layout_end();
        smb_native_hud_rendering=0;
        HSD_CObjEndCurrent();HSD_GObj_804D7814=previous;
        static int logged;if(!logged++){fprintf(stderr,"SMB NATIVE HUD damage_digits=%d emblem=%d\n",hud->HUD_parent_entity!=NULL,hud->next!=NULL);}
    }
    smb_draw_lives_icon((float)request->width/request->height);HSD_GObj_804D7814=previous;
}
int smb_embed_render(const SmbRender* request,float* color,float* depth,unsigned char* transmission) {
    if(!ready||!request||!color||!depth)return -1;
    smb_native_powerups();
    if(smb_gpu_begin(request->width,request->height))return -2;
    smb_native_rendering=1;
    if(request->flags&2) {
        HSD_StartRender(HSD_RP_SCREEN);smb_render_hud(request);
        int result=smb_gpu_end(color,depth,transmission);smb_native_rendering=0;return result;
    }
    static HSD_CObj* camera;
    if(!camera)camera=HSD_CObjAlloc();
    camera->flags|=2; /* Explicit host viewing matrix. */
    camera->projection_type=PROJ_PERSPECTIVE;camera->near=10;camera->far=2000;
    camera->projection_param.perspective.fov=45;camera->projection_param.perspective.aspect=(float)request->width/request->height;
    HSD_CObjSetViewportfx4(camera,0,640,0,480);
    camera->scissor=(Scissor){0,640,0,480};
    memcpy(camera->view_mtx,request->view,sizeof camera->view_mtx);
    camera->flags|=0x80000000u;
    HSD_StartRender(HSD_RP_SCREEN);HSD_CObjSetCurrent(camera);
    GXSetViewport(0,0,640,480,0,1);GXSetScissor(0,0,640,480);
    Mtx44 projection;memcpy(projection,request->projection,sizeof projection);
    GXSetProjection(projection,GX_PERSPECTIVE);
    HSD_StateInvalidate(-1);HSD_StateInitTev();
    GXSetPixelFmt(GX_PF_RGBA6_Z24,GX_ZC_LINEAR);
    GXSetColorUpdate(GX_ENABLE);GXSetAlphaUpdate(GX_ENABLE);
    static HSD_LObj* ambient;static HSD_LObj* sun;
    if(!ambient) {
        ambient=HSD_LObjAlloc();sun=HSD_LObjAlloc();ambient->next=sun;
        HSD_LObjSetFlags(ambient,LOBJ_AMBIENT|LOBJ_DIFFUSE);HSD_LObjSetColor(ambient,(GXColor){220,220,220,255});
        HSD_LObjSetFlags(sun,LOBJ_INFINITE|LOBJ_DIFFUSE|LOBJ_SPECULAR);
        HSD_LObjSetColor(sun,(GXColor){230,230,230,255});
        Vec3 position={500,1000,500},interest={0,0,0};
        HSD_LObjSetPosition(sun,&position);HSD_LObjSetInterest(sun,&interest);
    }
    /* Despite its name, SetCurrentAll registers only the supplied light.
     * Linking sun through ambient->next does not enable diffuse lighting. */
    HSD_LObjSetCurrentAll(ambient);
    if(!getenv("SMB_LIGHTING_LEGACY"))HSD_LObjAddCurrent(sun);
    HSD_LObjSetupInit(camera);
    static int logged_lights;if(!logged_lights++)fprintf(stderr,"SMB LIGHTS diffuse_mask=%x specular_mask=%x directional=%d\n",HSD_LObjGetLightMaskDiffuse(),HSD_LObjGetLightMaskSpecular(),HSD_LObjGetNbActive());
    HSD_GObj* fighter=smb_native_celebrant(0);
    HSD_GObj* partner=fighter?smb_native_celebrant(1):smb_partner();
    if(!fighter)fighter=Player_GetEntity(0);
    if(fighter) {
        Fighter* fp=fighter->user_data;
        Vec3 delta={request->position[0]-fp->cur_pos.x,request->position[1]-fp->cur_pos.y,request->position[2]-fp->cur_pos.z};
        fp->cur_pos=(Vec3){request->position[0],request->position[1],request->position[2]};
        HSD_JObjSetTranslate(fighter->hsd_obj,&fp->cur_pos);
        if(partner){Fighter* p=partner->user_data;p->cur_pos.x+=delta.x;p->cur_pos.y+=delta.y;p->cur_pos.z+=delta.z;HSD_JObjSetTranslate(partner->hsd_obj,&p->cur_pos);}
    }
    HSD_GObj* previous=HSD_GObj_804D7814;
    HSD_GObj* fighters[GM_MAX_PLAYERS*2]={fighter,partner};unsigned fighter_count=2;
    for(unsigned seat=1;seat<GM_MAX_PLAYERS;seat++)for(unsigned sub=0;sub<2;sub++) {
        HSD_GObj* npc=Player_GetEntityAtIndex(seat,sub);
        if(!(request->flags&4)&&npc&&!((Fighter*)npc->user_data)->is_sleeping)fighters[fighter_count++]=npc;
    }
    float posed_yaw[GM_MAX_PLAYERS*2]={0};
    for(unsigned i=0;i<2;i++)if(fighters[i]&&(request->flags&1)) {
        Fighter* fp=fighters[i]->user_data;HSD_JObj* root=fp->parts[0].joint;
        posed_yaw[i]=HSD_JObjGetRotationY(root);
        ftPartSetRotY_Raw(fp,0,request->yaw);
    }
    /* Melee's camera normally runs a separate outline/material pass before
     * its normal fighter pass. Game & Watch's authored visibility group lives
     * there; replacing the camera must retain that pass without changing view. */
    HSD_JObj* flat_models[GM_MAX_PLAYERS*2]={0};float flat_yaws[GM_MAX_PLAYERS*2]={0};
    for(unsigned seat=0;seat<fighter_count;seat++)if(fighters[seat]&&((Fighter*)fighters[seat]->user_data)->kind==24) {
        Fighter* fp=fighters[seat]->user_data;HSD_JObj* model=fp->parts[0].joint;
        if(model&&(HSD_JObjGetFlags(model)&JOBJ_USE_QUATERNION))model=fp->parts[0].x4_jobj2;
        if(model) {
            flat_models[seat]=model;flat_yaws[seat]=HSD_JObjGetRotationY(model);
            float lx=1,lz=0;m3d_GetLane(fp,&lx,&lz);
            float side=fp->facing_dir*(lx*request->view[0]+lz*request->view[2]);
            float yaw=atan2f(-request->view[10],request->view[8])+(side<-.05f?3.14159265f:0);
            HSD_JObjSetRotationY(model,yaw);
            static uint64_t logged;if(!(logged&(UINT64_C(1)<<seat))){logged|=UINT64_C(1)<<seat;fprintf(stderr,"SMB FLAT FIGHTER seat=%u original_yaw=%.4f render_yaw=%.4f view_right=%.4f,%.4f view_back=%.4f,%.4f\n",seat,flat_yaws[seat],yaw,request->view[0],request->view[2],request->view[8],request->view[10]);}
        }
    }
    /* Retail refreshes these visibility caches around its shadow render.
     * We use SM64 shadows, so invalidate the groups here before native drawing
     * reapplies the current model selections (shell, ball, body, equipment). */
    if(!getenv("SMB_MODEL_CACHE_LEGACY"))for(unsigned i=0;i<fighter_count;i++)if(fighters[i]) {
        Fighter* fp=fighters[i]->user_data;
        ftParts_800750C8(fp,0,0);ftParts_800750C8(fp,1,0);
        ftParts_800750C8(fp,2,0);ftParts_800750C8(fp,4,0);
    }
    for(unsigned i=0;i<fighter_count;i++)smb_native_cap_render(fighters[i],1);
    int native_pass=Camera_80031060();
    for(int i=0;i<fighter_count;i++)if(fighters[i]&&((Fighter*)fighters[i]->user_data)->x5AC.xC[4]) {
        Camera_80031074(1);HSD_GObj_804D7814=fighters[i];
        for(int pass=0;pass<3;pass++)ftDrawCommon_80080E18(fighters[i],pass);
    }
    Camera_80031074(native_pass);
    for(int pass=0;pass<3;pass++)for(int i=0;i<fighter_count;i++)if(fighters[i]) {
        HSD_GObj_804D7814=fighters[i];ftDrawCommon_80080E18(fighters[i],pass);
    }
    /* Native opacity disables depth writes. Publish the same cloak geometry's
     * coverage depth so SM64 can occlude it without hiding it behind terrain. */
    smb_native_effect_depth=1;HSD_StateInvalidate(-1);GXSetColorUpdate(GX_DISABLE);GXSetAlphaUpdate(GX_DISABLE);
    for(int pass=0;pass<3;pass++)for(unsigned i=0;i<fighter_count;i++)if(smb_native_cap_translucent(fighters[i])) {
        HSD_GObj_804D7814=fighters[i];ftDrawCommon_80080E18(fighters[i],pass);
    }
    smb_native_effect_depth=0;HSD_StateInvalidate(-1);GXSetColorUpdate(GX_ENABLE);GXSetAlphaUpdate(GX_ENABLE);
    for(unsigned i=0;i<fighter_count;i++)smb_native_cap_render(fighters[i],0);
    extern void smb_native_model_probe(HSD_GObj*);
    if(getenv("SMB_MODEL_TRACE"))for(unsigned i=0;i<fighter_count;i++)smb_native_model_probe(fighters[i]);
    for(unsigned seat=0;seat<fighter_count;seat++)if(flat_models[seat])HSD_JObjSetRotationY(flat_models[seat],flat_yaws[seat]);
    for(unsigned i=0;i<2;i++)if(fighters[i]&&(request->flags&1))ftPartSetRotY_Raw(fighters[i]->user_data,0,posed_yaw[i]);
    static int had_platform;
    int has_platform=fighter&&((Fighter*)fighter->user_data)->x20A0_accessory&&(((Fighter*)fighter->user_data)->motion_id==12||((Fighter*)fighter->user_data)->motion_id==13);
    if(has_platform!=had_platform){had_platform=has_platform;fprintf(stderr,"SMB RESPAWN RENDER platform=%d\n",has_platform);}
    static int previous_count=-1;int visible_count=(fighter!=NULL)+(partner!=NULL)+fighter_count-2;
    if(previous_count!=visible_count){previous_count=visible_count;fprintf(stderr,"SMB NATIVE FIGHTERS count=%d primary=%d partner=%d\n",visible_count,fighter?((Fighter*)fighter->user_data)->kind:-1,partner?((Fighter*)partner->user_data)->kind:-1);}
    static unsigned previous_items=UINT32_MAX;unsigned items_drawn=0;
    for(HSD_GObj* object=HSD_GObjPLinkHead[9];object;object=object->next)if(object->render_cb)items_drawn++;
    if(items_drawn!=previous_items){previous_items=items_drawn;fprintf(stderr,"SMB ITEM RENDER count=%u\n",items_drawn);}
    const int links[]={6,7,8};
    for(int pass=0;pass<3;pass++)for(unsigned i=0;i<3;i++)for(HSD_GObj* object=HSD_GObjGXLinkHead[links[i]];object;object=object->next_gx) {
        if(!(request->flags&4)&&object->render_cb&&!(object->p_link==9&&smb_native_victory_item_hidden(((Item*)object->user_data)->owner))){HSD_GObj_804D7814=object;object->render_cb(object,pass);}
    }
    /* Transparent GX particles often disable depth writes. Retain their
     * colour pass, then record coverage depth without changing that colour.
     * Otherwise GL receives the untouched background depth and hides them
     * behind SM64 terrain. Skip pass 0: it advances native particle bookkeeping. */
    smb_native_effect_depth=1;
    HSD_StateInvalidate(-1);GXSetColorUpdate(GX_DISABLE);GXSetAlphaUpdate(GX_DISABLE);
    if(!getenv("SMB_EFFECT_DEPTH_OFF"))for(int pass=1;pass<3;pass++)for(unsigned i=0;i<3;i++)for(HSD_GObj* object=HSD_GObjGXLinkHead[links[i]];object;object=object->next_gx) {
        if(!(request->flags&4)&&object->render_cb&&!(object->p_link==9&&smb_native_victory_item_hidden(((Item*)object->user_data)->owner))){HSD_GObj_804D7814=object;object->render_cb(object,pass);}
    }
    for(int pass=1;pass<3;pass++)for(int i=0;i<fighter_count;i++)if(fighters[i]) {
        Fighter* p=fighters[i]->user_data;
        if(p->x20A0_accessory){HSD_GObj_804D7814=fighters[i];HSD_JObjDispAll(p->x20A0_accessory,NULL,HSD_GObj_80390EB8(pass),0);}
    }
    smb_native_effect_depth=0;
    HSD_StateInvalidate(-1);GXSetColorUpdate(GX_ENABLE);GXSetAlphaUpdate(GX_ENABLE);
    if(!(request->flags&4))smb_native_ground_markers();
    HSD_CObjEndCurrent();HSD_GObj_804D7814=previous;
    if(getenv("SMB_RENDER_DIAGNOSTIC"))smb_draw_composite_probe();
    int status=smb_gpu_end(color,depth,transmission);smb_native_rendering=0;
    static unsigned frames;
    if(getenv("SMB_RENDER_DIAGNOSTIC")&&frames==90)smb_check_composite_probe(request,color,transmission);
    if(getenv("SMB_EFFECT_PROBE")&&frames%15==0&&fighter) {
        Fighter* fp=fighter->user_data;float point[4]={fp->cur_pos.x+20,fp->cur_pos.y+10,fp->cur_pos.z,1},eye[4]={0,0,0,1},clip[4]={0};
        for(int r=0;r<3;r++)for(int c=0;c<4;c++)eye[r]+=request->view[r*4+c]*point[c];
        for(int r=0;r<4;r++)for(int c=0;c<4;c++)clip[r]+=request->projection[r*4+c]*eye[c];
        int x=(clip[0]/clip[3]*.5f+.5f)*request->width,y=(.5f-clip[1]/clip[3]*.5f)*request->height;
        unsigned visible=0,without_depth=0;
        for(int v=y-35;v<=y+35;v++)for(int u=x-35;u<=x+35;u++)if(u>=0&&v>=0&&u<(int)request->width&&v<(int)request->height) {
            unsigned i=v*request->width+u;
            if(color[i*4+3]&&(color[i*4]||color[i*4+1]||color[i*4+2])) {
                if(depth[i]>0.000001f&&depth[i]<.99999f)visible++;else without_depth++;
            }
        }
        fprintf(stderr,"SMB EFFECT DEPTH frame=%u visible=%u background_depth=%u x=%d y=%d\n",frames,visible,without_depth,x,y);
    }
    if(frames++%90==0||(items_drawn&&frames%15==0))fprintf(stderr,"SMB NATIVE RENDER frame=%u draws=%u status=%d items=%u\n",frames,aurora_get_stats()->drawCallCount,status,items_drawn);
    return status;
}

extern int smb_native_world(const SmbSurface*,unsigned,unsigned);
int smb_embed_world(const SmbSurface* surfaces,unsigned count,unsigned revision) {
    if(!ready)return -1;
    return smb_native_world(surfaces,count,revision);
}

#include "melee_health.inc"

int smb_embed_ledge(unsigned ordinal,float* edge,float* inward) {
    Vec3 e,n;if(!ready||!m3d_DelfinoLedgeProbe(ordinal,&e,&n))return -1;
    memcpy(edge,&e,12);memcpy(inward,&n,12);return 0;
}

int smb_embed_wall(unsigned ordinal,float* start,float* normal) {
    Vec3 s,n,face;float top;
    if(!ready||!m3d_DelfinoWallProbe(ordinal,&s,&n,&face,&top))return -1;
    memcpy(start,&s,12);memcpy(normal,&n,12);return 0;
}

int smb_embed_support(float* point) {
    HSD_GObj* object=Player_GetEntity(0);if(!ready||!object)return -1;
    Fighter* fp=object->user_data;Vec3 edge;float nx,nz;
    if(!m3d_DelfinoLedge(&fp->cur_pos,&edge,&nx,&nz))return -1;
    float dx=edge.x-fp->cur_pos.x,dz=edge.z-fp->cur_pos.z;
    if(dx*dx+dz*dz>20*20||edge.y-fp->cur_pos.y>35)return -1;
    Vec3 support={edge.x+nx*2,edge.y+4,edge.z+nz*2};memcpy(point,&support,12);return 0;
}

extern int smb_native_stair_probe(unsigned,Vec3*,Vec3*,float*,float*);
int smb_embed_stair(unsigned ordinal,float* point,float* normal,float* rise,float* run) {
    Vec3 p,n;if(!ready||!point||!normal||!rise||!run)return -1;
    int result=smb_native_stair_probe(ordinal,&p,&n,rise,run);
    if(!result){memcpy(point,&p,12);memcpy(normal,&n,12);}return result;
}

#include "melee_network_combat.inc"
#include "melee_actor_environment.inc"
