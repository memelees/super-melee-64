/* SM64 owns the adventure; native Melee owns fighter simulation and rendering. */
#include "bridge_protocol.h"
#include "sm64.h"
#include "game/mario.h"
#include "audio/external.h"
#include "game/save_file.h"
#include "course_table.h"
#include "game/game_init.h"
#include "game/camera.h"
#include "game/level_update.h"
#include "game/ingame_menu.h"
#ifdef SMB_COOP_CAMPAIGN
#include "pc/djui/djui_panel.h"
#endif
#include "engine/surface_collision.h"
#include "engine/surface_load.h"
#include "engine/math_util.h"
#include "game/object_list_processor.h"
#include "game/area.h"
#include "surface_terrains.h"
#include "game/interaction.h"
#include "game/platform_displacement.h"
extern struct Object* gMarioPlatform;
#include <SDL2/SDL.h>
#ifdef _WIN32
#define GLEW_STATIC
#endif
#include <GL/glew.h>
#include <sys/stat.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "sm64_platform.h"

void smb_record_toggle(void);
void smb_record_stop(void);
void smb_record_melee_audio(const float*,unsigned);
void smb_host_key(int,int);
static int connection=-1, active, world_carry;
static int recording_scene_ready;
int smb_record_scene_ready(void){return recording_scene_ready;}
static struct Object* native_grab_target;
static unsigned sequence, frames;
static unsigned world_generation;
unsigned smb_host_game_frames(void){return frames;}
static int respawn_pending, host_dead;
static int cannon_pending;
static unsigned pound_air,pound_land;
static int water_active;static int air_remaining=0x880;
static float air_heal_pending,air_hurt_pending;
#include "sm64_air.inc"
static float cannon_position[3],cannon_velocity[3];
static unsigned last_motion=UINT32_MAX,last_hitboxes=UINT32_MAX;
static SmbOutput snapshot;
static int (*embedded_step)(const SmbInput*,SmbOutput*);
static int (*embedded_remote)(unsigned,const SmbRemote*);
static int (*embedded_switch)(unsigned,unsigned);
static unsigned (*embedded_costumes)(unsigned);
static int (*embedded_powerups)(unsigned);
static int (*embedded_npc_add)(unsigned,unsigned,unsigned,unsigned,const float*);
static int character_menu_requested,character_input_latch;
static int round_input_pending;
static int character_menu_open;
int smb_host_spawn_round(void);
static unsigned character_changed_at;
void smb_host_spawn_menu_request(void){if(embedded_step)character_menu_requested=2;}
void smb_host_change_menu_request(void){if(embedded_step)character_menu_requested=1;}
void smb_host_cannon_fired(struct MarioState* m) {
    if(!embedded_step)return;
    memcpy(cannon_position,m->pos,sizeof cannon_position);
    cannon_velocity[0]=m->forwardVel*sins(m->faceAngle[1])*.05f;
    cannon_velocity[1]=m->vel[1]*.05f;
    cannon_velocity[2]=m->forwardVel*coss(m->faceAngle[1])*.05f;
    cannon_pending=1;
}

static int (*embedded_camera)(float,SmbCamera*);
static SmbCamera native_camera;
static int camera_ready;
static unsigned char host_keys[512];
static float controller_look[2];
static int controller_vertical;
static float camera_pitch;
static int controller_active;
static unsigned controller_buttons;
static int controller_round_button,controller_round_latched;
static int smb_round_controls_ready(void) {
    extern s16 sCurrPlayMode;
    extern s16 gMenuMode;
    if(!embedded_step||!active||character_menu_open||character_input_latch||sCurrPlayMode!=0||gMenuMode!=-1||get_dialog_id()!=-1)return 0;
#ifdef SMB_COOP_CAMPAIGN
    if(djui_panel_is_active())return 0;
#endif
    return 1;
}
unsigned smb_host_keyboard_buttons(void);
/* SDL semantic positions match Melee3D's default modern-controller layout. */
void smb_host_controller_state(unsigned physical,float left,float right,int connected) {
    controller_active=connected;controller_buttons=0;controller_vertical=0;
    controller_round_button=connected&&!!(physical&(1u<<SDL_CONTROLLER_BUTTON_LEFTSTICK));
    if(!smb_round_controls_ready()) {
        round_input_pending=0;
        if(connected)controller_round_latched=controller_round_button;
    }
    if(!connected)return;
    controller_vertical=!!(physical&(1u<<SDL_CONTROLLER_BUTTON_DPAD_UP))-!!(physical&(1u<<SDL_CONTROLLER_BUTTON_DPAD_DOWN));
    if(physical&(1u<<SDL_CONTROLLER_BUTTON_A))controller_buttons|=0x100;
    if(physical&(1u<<SDL_CONTROLLER_BUTTON_B))controller_buttons|=0x200;
    if(physical&((1u<<SDL_CONTROLLER_BUTTON_X)|(1u<<SDL_CONTROLLER_BUTTON_Y)))controller_buttons|=0x400;
    if(physical&(1u<<SDL_CONTROLLER_BUTTON_LEFTSHOULDER))controller_buttons|=0x8;
    if(physical&(1u<<SDL_CONTROLLER_BUTTON_RIGHTSHOULDER))controller_buttons|=0x10;
    if(left>.3f)controller_buttons|=0x40;
    if(right>.3f)controller_buttons|=0x20;
}
/* This is also the SDL backend's only host-button mapping while embedded. */
unsigned smb_host_camera_pad(unsigned physical) {
    unsigned buttons=0;
    if(physical&(1u<<SDL_CONTROLLER_BUTTON_START))buttons|=START_BUTTON;
    if(physical&((1u<<SDL_CONTROLLER_BUTTON_A)|(1u<<SDL_CONTROLLER_BUTTON_X)|(1u<<SDL_CONTROLLER_BUTTON_Y)))buttons|=A_BUTTON; /* SM64 dialogs/star menus. */
    if(physical&(1u<<SDL_CONTROLLER_BUTTON_B))buttons|=B_BUTTON;
    if(physical&(1u<<SDL_CONTROLLER_BUTTON_DPAD_LEFT))buttons|=L_CBUTTONS;
    if(physical&(1u<<SDL_CONTROLLER_BUTTON_DPAD_RIGHT))buttons|=R_CBUTTONS;
    if(physical&(1u<<SDL_CONTROLLER_BUTTON_DPAD_UP))buttons|=U_CBUTTONS;
    if(physical&(1u<<SDL_CONTROLLER_BUTTON_DPAD_DOWN))buttons|=D_CBUTTONS;
    return buttons;
}
#include "sm64_options.inc"
#include "sm64_launch_coop.inc"
void smb_host_key(int code,int down) {
    if(code<0){memset(host_keys,0,sizeof host_keys);controller_look[0]=controller_look[1]=0;controller_vertical=0;round_input_pending=0;return;}
    if(code==SDL_SCANCODE_F8&&down&&!host_keys[code]&&smb_round_controls_ready())round_input_pending=1;
    if(code==SDL_SCANCODE_F7&&down&&!host_keys[code])character_menu_requested=2;
    if(code==SDL_SCANCODE_F10&&down&&!host_keys[code])smb_host_option(0,1);
    if(code==SDL_SCANCODE_F11&&down&&!host_keys[code])smb_host_option(1,1);
    if(code==SDL_SCANCODE_F6&&down&&!host_keys[code])smb_host_change_menu_request();
    if(code==SDL_SCANCODE_F9&&down&&!host_keys[code]) {
        smb_record_toggle();
    }
    if(code<512)host_keys[code]=down!=0;
}
void smb_host_controller_look(float x,float y){controller_look[0]=fabsf(x)>.22f?x:0;controller_look[1]=fabsf(y)>.22f?y:0;}
static void smb_combat_camera_update(struct Camera*);
static float combat_zoom,combat_lift;
static int melee_camera_enabled(void){return getenv("SMB_CAMERA")&&!strcmp(getenv("SMB_CAMERA"),"melee");}
float smb_host_camera_distance(float distance) {
    float preferred=embedded_step&&!melee_camera_enabled()?distance*(1.25f+combat_zoom):distance;
    if(getenv("SMB_SCENARIO")&&distance>=350) {
        static unsigned seen;unsigned bit=distance==800?1:distance==350?2:distance==1000?4:8;
        if(!(seen&bit)){seen|=bit;fprintf(stderr,"SMB CAMERA DISTANCE base=%.1f preferred=%.1f\n",distance,preferred);}
    }
    return preferred;
}
int smb_host_camera(struct Camera* c) {
    if(embedded_step&&!melee_camera_enabled()) {
        /* C-up is a persistent Mario action; native combat bypasses its exit
         * handler. Use a bounded, momentary look instead, including D-pad. */
        gPlayer1Controller->buttonDown&=~(U_CBUTTONS|D_CBUTTONS);
        gPlayer1Controller->buttonPressed&=~(U_CBUTTONS|D_CBUTTONS);
        gCameraMovementFlags&=~(CAM_MOVE_C_UP_MODE|CAM_MOVE_STARTED_EXITING_C_UP);
        if(c->mode==CAMERA_MODE_C_UP)set_camera_mode(c,c->defMode,1);
        float look=controller_look[1]+controller_vertical+host_keys[SDL_SCANCODE_UP]-host_keys[SDL_SCANCODE_DOWN];
        float target=active&&!c->cutscene?fmaxf(-1,fminf(1,look))*.32f:0;
        camera_pitch+=(target-camera_pitch)*.18f;
        /* Retain the ten-star sunlight entrance with momentary look. */
        static unsigned sunlight;
        struct MarioState* m=gMarioState;
        if(active&&!c->cutscene&&m&&m->floor&&m->floor->type==SURFACE_LOOK_UP_WARP&&m->forwardVel<1&&look>.65f&&
           save_file_get_total_star_count(gCurrSaveFileNum-1,0,0x18)>=10) {
            if(++sunlight==30){level_trigger_warp(m,WARP_OP_UNKNOWN_01);fprintf(stderr,"SMB CAMERA SUNLIGHT native_warp=1\n");}
        } else sunlight=0;
    }
    smb_combat_camera_update(c);
    if(embedded_step&&!melee_camera_enabled()&&getenv("SMB_SCENARIO")) {
        static unsigned tick;if(++tick%30==0)fprintf(stderr,"SMB CAMERA provider=sm64 follow_scale=1.25 mode=%d cutscene=%d\n",c->mode,c->cutscene);
    }
    if(!embedded_camera||!melee_camera_enabled()||!active||!camera_ready||c->cutscene)return 0;
    for(int k=0;k<3;k++) {
        c->pos[k]=gLakituState.pos[k]=gLakituState.goalPos[k]=native_camera.eye[k]*10;
        c->focus[k]=gLakituState.focus[k]=gLakituState.goalFocus[k]=native_camera.target[k]*10;
    }
    c->yaw=c->nextYaw=(s16)(native_camera.yaw*(65536.f/6.28318530718f));
    gLakituState.roll=0;return 1;
}
float smb_host_camera_fov(void) {
    return melee_camera_enabled()&&active&&camera_ready&&gCamera&&!gCamera->cutscene?native_camera.fov:0;
}
float smb_host_combat_fov(float base) {
    return base>=40&&base<=50?base+combat_zoom*24:base;
}
void smb_host_combat_focus(struct Camera* c,int add) {
    c->focus[1]+=add?combat_lift:-combat_lift;
    if(!add)gLakituState.goalFocus[1]-=combat_lift;
}
/* SM64's ordinary height solver follows floor height, at 20 units/frame.
 * Native aerial motion can rise much faster or remain above that floor. Keep
 * the stock yaw and obstruction handling, but repair an unreadable low/steep
 * follow pose and its persistent history before adding momentary look. */
static int smb_camera_readable_mode(struct Camera* c) {
    return c->mode==CAMERA_MODE_FREE_ROAM||c->mode==CAMERA_MODE_RADIAL||
        c->mode==CAMERA_MODE_OUTWARD_RADIAL||c->mode==CAMERA_MODE_CLOSE||
        c->mode==CAMERA_MODE_8_DIRECTIONS||c->mode==CAMERA_MODE_BEHIND_MARIO||
        c->mode==CAMERA_MODE_WATER_SURFACE;
}
static void smb_camera_follow_recover(struct Camera* c) {
    if(!active||!gMarioState||gMarioState->health<0x100||!smb_camera_readable_mode(c))return;
    float body=gMarioState->pos[1]+125;
    /* Leave stock horizontal panning and combat focus intact. Bound vertical
     * lag to the visible fighter, including while attacking in mid-air. */
    c->focus[1]=fmaxf(body-100,fminf(body+100,c->focus[1]));
    gLakituState.goalFocus[1]=fmaxf(body-100,fminf(body+100,gLakituState.goalFocus[1]));
    body+=combat_lift;
    gLakituState.focus[1]=gLakituState.curFocus[1]=fmaxf(body-100,fminf(body+100,gLakituState.curFocus[1]));
    Vec3f* eye=&gLakituState.pos;Vec3f* focus=&gLakituState.focus;
    float dx=(*eye)[0]-(*focus)[0],dz=(*eye)[2]-(*focus)[2],flat=hypotf(dx,dz);
    /* SM64 defines its own atan2f with game-axis conventions. */
    float pitch=(float)atan2((double)((*eye)[1]-(*focus)[1]),(double)flat);
    if(flat>=250&&pitch>=.16f&&pitch<=.95f)return;
    if(flat<1){dx=sins(c->yaw)*250;dz=coss(c->yaw)*250;flat=250;}
    else if(flat<250){dx*=250/flat;dz*=250/flat;flat=250;}
    pitch=fmaxf(.16f,fminf(.95f,pitch));
    Vec3f delta={dx,flat*tanf(pitch),dz},hit;
    struct Surface* wall=NULL;int checking=gCheckingSurfaceCollisionsForCamera;
    gCheckingSurfaceCollisionsForCamera=TRUE;find_surface_on_ray(*focus,delta,&wall,hit);
    gCheckingSurfaceCollisionsForCamera=checking;
    for(int k=0;k<3;k++) {
        float safe=(*focus)[k]+(wall?(hit[k]-(*focus)[k])*.95f:delta[k]);
        (*eye)[k]=gLakituState.curPos[k]=gLakituState.goalPos[k]=c->pos[k]=safe;
    }
}
/* Apply momentary pitch only to the final view, never the stock camera's
 * persistent goal/curPos history. Releasing look cannot leave a tilted goal. */
void smb_host_camera_pose(struct Camera* c) {
    if(!embedded_step||melee_camera_enabled()||c->cutscene)return;
    smb_camera_follow_recover(c);
    Vec3f* eye=&gLakituState.pos;Vec3f* focus=&gLakituState.focus;
    if(fabsf(camera_pitch)>.0001f) {
        float dx=(*eye)[0]-(*focus)[0],dy=(*eye)[1]-(*focus)[1],dz=(*eye)[2]-(*focus)[2];
        float flat=hypotf(dx,dz),distance=hypotf(flat,dy);
        float pitch=fmaxf(-1.1f,fminf(1.1f,(float)atan2((double)dy,(double)flat)+camera_pitch));
        float next=distance*cosf(pitch);
        Vec3f delta={flat>1?dx*next/flat:dx,distance*sinf(pitch),flat>1?dz*next/flat:dz},hit;
        struct Surface* wall=NULL;
        find_surface_on_ray(*focus,delta,&wall,hit);
        for(int k=0;k<3;k++)(*eye)[k]=wall?(*focus)[k]+(hit[k]-(*focus)[k])*.95f:(*focus)[k]+delta[k];
    }
    if(getenv("SMB_SCENARIO")&&!strcmp(getenv("SMB_SCENARIO"),"camera-look")&&(frames%180==15||frames%180==49||frames%180==99||frames%180==165)) {
        float dy=(*eye)[1]-(*focus)[1],flat=hypotf((*eye)[0]-(*focus)[0],(*eye)[2]-(*focus)[2]);
        fprintf(stderr,"SMB CAMERA POSE frame=%u offset=%.6f pitch=%.6f\n",frames,camera_pitch,(float)atan2((double)dy,(double)flat));
    }
    if(getenv("SMB_SCENARIO")&&!strcmp(getenv("SMB_SCENARIO"),"camera-follow")&&frames%5==0) {
        float flat=hypotf((*eye)[0]-(*focus)[0],(*eye)[2]-(*focus)[2]);
        fprintf(stderr,"SMB CAMERA FOLLOW frame=%u pitch=%.6f focus_error=%.2f player_y=%.1f eye_y=%.1f mode=%d\n",frames,(float)atan2((double)((*eye)[1]-(*focus)[1]),(double)flat),(*focus)[1]-(gMarioState->pos[1]+125+combat_lift),gMarioState->pos[1],(*eye)[1],c->mode);
    }
    if(getenv("SMB_SCENARIO")&&!strcmp(getenv("SMB_SCENARIO"),"wing-entrance")&&gCurrLevelNum==LEVEL_TOTWC) {
        static int reported;if(!reported++){fprintf(stderr,"SMB CAMERA WING ENTRY complete=1\n");}
    }
}
static void (*embedded_shutdown)(void);
static int (*embedded_render)(const SmbRender*,float*,float*,unsigned char*);
static int (*embedded_world)(const SmbSurface*,unsigned,unsigned);
static void smb_host_apply_hits(void);
static int (*embedded_contact)(unsigned);
static int (*embedded_impact)(unsigned,unsigned,const float*);
static int (*embedded_health)(float,float,float);
static int (*embedded_actor_state)(unsigned,unsigned,SmbFighterState*);
static int (*embedded_actor_environment)(unsigned,unsigned,unsigned,float);
static int (*embedded_actor_world_hit)(unsigned,unsigned,const SmbHit*,unsigned);
static int (*embedded_actor_world_ko)(unsigned,unsigned);
static float (*embedded_actor_damage)(unsigned,unsigned);
/* Air follows NPC encounter authority, independently for Popo and Nana. */
static uint32_t npc_air_valid,npc_air[3][2];
static float npc_air_hurt[3][2];
static int smb_npc_environment_dead(unsigned);
static void smb_npc_environment_kill(unsigned,unsigned,const char*);
static void smb_npc_water_reset(unsigned);
static int (*embedded_celebrate)(int,const float*);
static int (*embedded_hold)(void);
static int (*embedded_npc)(unsigned,const float*,SmbNpc*);
static int (*embedded_npc_wait)(unsigned);
static int (*embedded_encounter)(unsigned,const SmbEncounter*);
static int (*embedded_npc_ko)(unsigned,const float*,unsigned);
static int (*embedded_npc_configure)(unsigned,unsigned,float);
static int (*embedded_ledge)(unsigned,float*,float*);
static int (*embedded_wall)(unsigned,float*,float*);
static int (*embedded_support)(float*);
static int (*embedded_stair)(unsigned,float*,float*,float*,float*);
void smb_host_recover_floor(struct MarioState*);
static int smb_attached(void) {return (snapshot.motion>=252&&snapshot.motion<=260)||snapshot.motion==262;}
static void smb_host_release_session(void) {
    const char* path=getenv("SMB_SESSION_LOCK");const char* token=getenv("SMB_SESSION_INODE");
    if(!path||!token)return;
    FILE* f=fopen(path,"r");if(!f)return;
    long owner=0;struct stat st;int read=fscanf(f,"%ld",&owner)==1;
    int valid=fstat(fileno(f),&st)==0;fclose(f);
    if(read&&valid&&owner==getpid()&&(unsigned long long)st.st_ino==strtoull(token,NULL,10))unlink(path);
}
void smb_host_shutdown(void) {
    smb_record_stop();
    if(embedded_shutdown) {embedded_shutdown();embedded_shutdown=NULL;}
}
static void* smb_native_stack;static size_t smb_native_stack_size;
void smb_host_initialize(void* native_stack,size_t native_stack_size) {
    smb_native_stack=native_stack;smb_native_stack_size=native_stack_size;
    if(getenv("SM64_MELEE_LIBRARY")){atexit(smb_host_release_session);atexit(smb_record_stop);}
}
void smb_host_boot(void) {
    smb_host_options_load();
    if(embedded_step)return;
    const char* library=getenv("SM64_MELEE_LIBRARY");if(!library)return;
    void* handle=dlopen(library,RTLD_NOW|RTLD_LOCAL);
    if(!handle){fprintf(stderr,"SMB HOST FAIL dlopen: %s\n",dlerror());exit(2);}
    int (*init)(uint32_t,const char*,void*,size_t)=dlsym(handle,"smb_embed_init");
    embedded_step=dlsym(handle,"smb_embed_step");
    embedded_switch=dlsym(handle,"smb_embed_switch");
    embedded_costumes=dlsym(handle,"smb_embed_costumes");
    embedded_powerups=dlsym(handle,"smb_embed_powerups");
    embedded_camera=dlsym(handle,"smb_embed_camera");
    embedded_shutdown=dlsym(handle,"smb_embed_shutdown");
    embedded_render=dlsym(handle,"smb_embed_render");
    embedded_world=dlsym(handle,"smb_embed_world");
    embedded_contact=dlsym(handle,"smb_embed_contact");
    embedded_impact=dlsym(handle,"smb_embed_impact");
    embedded_health=dlsym(handle,"smb_embed_health");
    embedded_actor_state=dlsym(handle,"smb_embed_actor_state");
    embedded_actor_environment=dlsym(handle,"smb_embed_actor_environment");
    embedded_actor_world_hit=dlsym(handle,"smb_embed_actor_world_hit");
    embedded_actor_world_ko=dlsym(handle,"smb_embed_actor_world_ko");
    embedded_actor_damage=dlsym(handle,"smb_embed_actor_damage");
    embedded_celebrate=dlsym(handle,"smb_embed_celebrate");
    embedded_hold=dlsym(handle,"smb_embed_hold");
    embedded_npc=dlsym(handle,"smb_embed_npc");
    embedded_npc_wait=dlsym(handle,"smb_embed_npc_wait");
    embedded_encounter=dlsym(handle,"smb_embed_encounter");
    embedded_npc_ko=dlsym(handle,"smb_embed_npc_ko");
    embedded_npc_configure=dlsym(handle,"smb_embed_npc_configure");
    embedded_ledge=dlsym(handle,"smb_embed_ledge");
    embedded_wall=dlsym(handle,"smb_embed_wall");
    embedded_support=dlsym(handle,"smb_embed_support");
    embedded_npc_add=dlsym(handle,"smb_embed_npc_add");
    embedded_remote=dlsym(handle,"smb_embed_remote");
    embedded_stair=dlsym(handle,"smb_embed_stair");
    if(!init||!embedded_step||!embedded_shutdown||!embedded_actor_state||!embedded_actor_environment||!embedded_actor_world_hit||!embedded_actor_world_ko||!embedded_actor_damage){fprintf(stderr,"SMB HOST FAIL embedded ABI\n");exit(2);}
    int result=init(SMB_MAGIC,getenv("SM64_MELEE_DISC"),smb_native_stack,smb_native_stack_size);
    if(result){fprintf(stderr,"SMB HOST FAIL embedded init=%d\n",result);exit(2);}
    void (*audio_sink)(void(*)(const float*,unsigned))=dlsym(handle,"smb_embed_audio_sink");
    if(audio_sink)audio_sink(smb_record_melee_audio);
    fprintf(stderr,"SMB HOST EMBEDDED pid=%d transport=direct-call\n",getpid());
    /* Keep the library loaded for process lifetime: its engine owns workers
     * and its registered shutdown must run before C++ static destructors. */
}
/* Normal launches use their own unlocked test profile. Story saves opt out. */
void smb_host_unlock_save(void) {
    if(!embedded_step)return;
    static int inspected;if(!inspected++)fprintf(stderr,"SMB PROGRESSION mode=%s stars=%d flags=%08x cannon_bob=%d\n",getenv("SMB_SAVE_MODE")?:"unlocked",save_file_get_total_star_count(gCurrSaveFileNum-1,COURSE_MIN-1,COURSE_MAX-1),save_file_get_flags(),save_file_get_cannon_flags(gCurrSaveFileNum-1,0)!=0);
    if(!getenv("SMB_ALL_STARS"))return;
    static int done;if(done++)return;
    int file=gCurrSaveFileNum-1;
    int before=save_file_get_total_star_count(file,COURSE_MIN-1,COURSE_MAX-1);
    for(int c=COURSE_BOB;c<=COURSE_RR;c++) {
        save_file_set_star_flags(file,c-1,0x7f);
        save_file_set_star_flags(file,c,0x80); /* Native cannon indexing. */
    }
    for(int c=COURSE_BITDW;c<=COURSE_SA;c++)save_file_set_star_flags(file,c-1,c==COURSE_PSS?3:1);
    save_file_set_star_flags(file,-1,0x1f); /* Three Toads and two MIPS stars. */
    save_file_set_flags(0x10ffff); /* Caps, keys, doors and moat; no lost-cap bits. */
    save_file_do_save(file);
    int cannons=0;for(int c=COURSE_BOB;c<=COURSE_RR;c++)cannons+=save_file_get_cannon_flags(file,c-1)!=0;
    int stars=save_file_get_total_star_count(file,COURSE_MIN-1,COURSE_MAX-1);
    fprintf(stderr,"SMB PLAYTEST SAVE file=%d previous_stars=%d stars=%d cannons=%d flags=%08x\n",file+1,before,stars,cannons,save_file_get_flags());
    if(stars!=120||cannons!=15){fprintf(stderr,"SMB HOST FAIL unlock save\n");exit(2);}
}
/* Cache is generated from the owner's local disc, outside deliverable output. */
typedef struct {uint32_t joint;float weight,x,y,z;} SkinWeight;
typedef struct {uint8_t color[4],count,bone,object,pad;SkinWeight weights[4];} SkinVertex;
static SkinVertex* mesh;
static unsigned mesh_count,mesh_bones,mesh_ids[SMB_BONES];
static Vtx* render_vertices[2];
static Gfx* render_lists[2];
static unsigned render_buffer,render_frames;

static void load_mesh(void) {
    const char* path=getenv("SM64_MELEE_MODEL");if(!path)return;
    FILE* f=fopen(path,"rb");uint32_t header[4];
    if(!f||fread(header,sizeof header,1,f)!=1||memcmp(header,"SMM1",4)||header[1]!=2||
       !header[2]||header[2]>SMB_BONES||!header[3]||header[3]>60000||header[3]%3||sizeof(SkinVertex)!=88) {
        fprintf(stderr,"SMB HOST FAIL mesh header\n");exit(2);
    }
    mesh_bones=header[2];mesh_count=header[3];
    mesh=calloc(mesh_count,sizeof *mesh);
    if(!mesh||fread(mesh_ids,4,mesh_bones,f)!=mesh_bones||fread(mesh,sizeof *mesh,mesh_count,f)!=mesh_count||fgetc(f)!=EOF)exit(2);
    fclose(f);
    for(unsigned i=0;i<mesh_count;i++) {
        SkinVertex* v=&mesh[i];
        if(!v->count||v->count>4||v->bone>=mesh_bones||v->object>=128)exit(2);
        float total=0;
        for(unsigned j=0;j<v->count;j++) {
            SkinWeight* w=&v->weights[j];
            if(w->joint>=mesh_bones||!isfinite(w->weight)||!isfinite(w->x)||!isfinite(w->y)||!isfinite(w->z)||w->weight<0)exit(2);
            total+=w->weight;
        }
        if(fabsf(total-1)>.001f)exit(2);
    }
    for(int i=0;i<2;i++) {
        render_vertices[i]=calloc(mesh_count,sizeof(Vtx));
        render_lists[i]=calloc(mesh_count+32,sizeof(Gfx));
        if(!render_vertices[i]||!render_lists[i])exit(2);
    }
    fprintf(stderr,"SMB HOST MESH vertices=%u joints=%u\n",mesh_count,mesh_bones);
}

Gfx* smb_host_display_list(void) {
    if(embedded_render && !getenv("SMB_LEGACY_SKIN")) {
        static Gfx native[2];native[0].words.w0=0xC1000000;native[0].words.w1=0;
        gSPEndDisplayList(&native[1]);return native;
    }
    if(!active||!snapshot.bones||!getenv("SM64_MELEE_MODEL"))return NULL;
    if(!mesh)load_mesh();
    int map[SMB_BONES];
    for(unsigned i=0;i<mesh_bones;i++) {
        map[i]=-1;
        for(unsigned j=0;j<snapshot.bones;j++)if(mesh_ids[i]==snapshot.joint_ids[j]){map[i]=j;break;}
        if(map[i]<0){fprintf(stderr,"SMB HOST FAIL unmapped model joint=%u id=%u\n",i,mesh_ids[i]);exit(2);}
    }
    render_buffer^=1;Vtx* output=render_vertices[render_buffer];
    Gfx* result=render_lists[render_buffer];Gfx* dl=result;
    gDPPipeSync(dl++);
    gSPClearGeometryMode(dl++,G_LIGHTING|G_CULL_BACK|G_CULL_FRONT|G_TEXTURE_GEN|G_TEXTURE_GEN_LINEAR|G_FOG);
    gSPSetGeometryMode(dl++,G_ZBUFFER|G_SHADE|G_SHADING_SMOOTH);
    gSPTexture(dl++,0,0,0,0,G_OFF);
    gDPSetCombineMode(dl++,G_CC_SHADE,G_CC_SHADE);
    unsigned count=0;
    for(unsigned i=0;i<mesh_count;i+=3) {
        SkinVertex* first=&mesh[i];
        if(!(snapshot.visible[map[first->bone]][first->object/32]&(1u<<(first->object%32))))continue;
        float points[3][3];
        for(int k=0;k<3;k++) {
            SkinVertex* v=&mesh[i+k];float* p=points[k];p[0]=p[1]=p[2]=0;
            for(unsigned w=0;w<v->count;w++) {
                SkinWeight* weight=&v->weights[w];float* a=snapshot.matrices[map[weight->joint]];
                for(int axis=0;axis<3;axis++)p[axis]+=(a[axis*4]*weight->x+a[axis*4+1]*weight->y+a[axis*4+2]*weight->z+a[axis*4+3])*weight->weight*10.f;
            }
        }
        float ux=points[1][0]-points[0][0],uy=points[1][1]-points[0][1],uz=points[1][2]-points[0][2];
        float vx=points[2][0]-points[0][0],vy=points[2][1]-points[0][1],vz=points[2][2]-points[0][2];
        float nx=uy*vz-uz*vy,ny=uz*vx-ux*vz,nz=ux*vy-uy*vx;
        float length=sqrtf(nx*nx+ny*ny+nz*nz);
        float light=length>.0001f?.55f+.45f*fabsf((nx*.3f+ny*.8f+nz*.5f)/length):.8f;
        for(int k=0;k<3;k++) {
            Vtx* v=&output[count+k];
            for(int axis=0;axis<3;axis++) {
                float value=points[k][axis];if(!isfinite(value)||fabsf(value)>32760)exit(2);
                v->v.ob[axis]=(short)lrintf(value);
            }
            v->v.flag=0;v->v.tc[0]=v->v.tc[1]=0;
            for(int c=0;c<3;c++)v->v.cn[c]=(uint8_t)(mesh[i+k].color[c]*light);
            v->v.cn[3]=255;
        }
        gSPVertex(dl++,&output[count],3,0);gSP1Triangle(dl++,0,1,2,0);count+=3;
    }
    gDPPipeSync(dl++);gSPSetGeometryMode(dl++,G_LIGHTING|G_CULL_BACK);gSPEndDisplayList(dl++);
    if(render_frames++%90==0)fprintf(stderr,"SMB HOST RENDER frame=%u triangles=%u mapped_joints=%u\n",render_frames,count/3,mesh_bones);
    return result;
}
#ifndef _WIN32
static void io_full(void* data,size_t n,int writing) {
    char* p=data;
    while(n) {
        ssize_t size=writing?send(connection,p,n,0):recv(connection,p,n,0);
        if(size<0&&errno==EINTR)continue;
        if(size<=0){fprintf(stderr,"SMB HOST FAIL worker disconnected\n");exit(2);}
        p+=size;n-=size;
    }
}
static void connect_worker(const char* path) {
    if(strlen(path)>=sizeof(((struct sockaddr_un*)0)->sun_path))exit(2);
    struct sockaddr_un addr={0};addr.sun_family=AF_UNIX;strcpy(addr.sun_path,path);
    for(int attempt=0;attempt<800;attempt++) {
        connection=socket(AF_UNIX,SOCK_STREAM,0);
        if(connection>=0&&!connect(connection,(struct sockaddr*)&addr,sizeof addr))break;
        if(connection>=0)close(connection);
        connection=-1;usleep(10000);
    }
    if(connection<0){fprintf(stderr,"SMB HOST FAIL no worker\n");exit(2);}
    struct timeval timeout={10,0};setsockopt(connection,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof timeout);
    int yes=1;setsockopt(connection,SOL_SOCKET,SO_NOSIGPIPE,&yes,sizeof yes);
}
#else
static void io_full(void* data,size_t size,int writing){(void)data;(void)size;(void)writing;fprintf(stderr,"SMB HOST FAIL legacy worker mode unavailable on Windows\n");exit(2);}
static void connect_worker(const char* path){(void)path;fprintf(stderr,"SMB HOST FAIL use embedded library on Windows\n");exit(2);}
#endif
static int publish_world(void) {
    if(!embedded_world||!gCurrentArea||gSurfacesAllocated<=0)return 0;
    enum {CAPACITY=4095};
    unsigned revision=(world_generation<<16)|((unsigned)gCurrLevelNum<<8)|(unsigned)gCurrentArea->index;
    static unsigned previous_revision=UINT32_MAX;
    static SmbSurface surfaces[CAPACITY];
    static struct Object* owner[CAPACITY];static unsigned ordinal[CAPACITY];
    extern s32 gNumStaticSurfaces;
    if(previous_revision!=revision){memset(owner,0,sizeof owner);previous_revision=revision;}
    if(gNumStaticSurfaces>CAPACITY){fprintf(stderr,"SMB HOST FAIL static surface budget\n");exit(2);}
    for(unsigned i=0;i<CAPACITY;i++){memset(&surfaces[i],0,sizeof surfaces[i]);surfaces[i].flags=1;}
    for(unsigned i=0;i<(unsigned)gSurfacesAllocated;i++) {
        struct Surface* a=&sSurfacePool[i];unsigned slot=i;
        if(i>=(unsigned)gNumStaticSurfaces) {
            unsigned n=0;
            for(unsigned j=gNumStaticSurfaces;j<i;j++)if(sSurfacePool[j].object==a->object)n++;
            for(slot=gNumStaticSurfaces;slot<CAPACITY;slot++)if(owner[slot]==a->object&&ordinal[slot]==n)break;
            if(slot==CAPACITY)for(slot=gNumStaticSurfaces;slot<CAPACITY;slot++)if(!owner[slot]||!(owner[slot]->activeFlags&ACTIVE_FLAG_ACTIVE))break;
            if(slot==CAPACITY){fprintf(stderr,"SMB HOST FAIL dynamic surface budget\n");exit(2);}
            owner[slot]=a->object;ordinal[slot]=n;
        }
        SmbSurface* b=&surfaces[slot];
        for(int j=0;j<3;j++){b->vertices[j]=a->vertex1[j]*.1f;b->vertices[3+j]=a->vertex2[j]*.1f;b->vertices[6+j]=a->vertex3[j]*.1f;b->normal[j]=j==0?a->normal.x:j==1?a->normal.y:a->normal.z;}
        b->type=(unsigned short)a->type;b->flags=a->type==SURFACE_DEATH_PLANE?2:a->type==SURFACE_CAMERA_BOUNDARY?1:0;
        /* Wobbling painting faces are sensors. Preserve their warp floors,
         * but never turn the vertical artwork into a solid Melee wall. */
        if(fabsf(a->normal.y)<.01f && a->type>=SURFACE_PAINTING_WOBBLE_A6 && a->type<=SURFACE_PAINTING_WARP_FC)b->flags=1;
    }
    int result=embedded_world(surfaces,CAPACITY,revision);
    if(result<0){fprintf(stderr,"SMB HOST FAIL world update=%d\n",result);exit(2);}
    return result;
}
#include "sm64_npcs.inc"
#include "sm64_npc_environment.inc"
#include "sm64_npc_water.inc"
#include "sm64_stage_starts.inc"
int smb_host_recommended_course(void) {
    const char* stage=getenv("SMB_START_STAGE");
    return embedded_step&&stage&&atoi(stage)>=0&&atoi(stage)<27&&
        gCurrCourseNum>=COURSE_BOB&&gCurrCourseNum<=COURSE_STAGES_MAX;
}
int smb_host_star_exit_destination(s16* level,s16* area,s16* node) {
    if(!smb_host_recommended_course())return 0;
    const SmbStageStart* start=&stage_starts[gCurrCourseNum+2];
    *level=start->level;*area=start->area;*node=start->node;
    /* The castle's landing/save dialog is skipped. Commit the real collected
     * star before unloading, and end the fighter's victory state explicitly. */
    save_file_do_save(gCurrSaveFileNum-1);
    if(embedded_celebrate){float p[3]={snapshot.x,snapshot.y,snapshot.z};embedded_celebrate(0,p);}
    active=0;world_carry=0;native_grab_target=NULL;world_generation++;
    fprintf(stderr,"SMB STAGE STAR EXIT course=%d flags=%02x saved=1 selector=1\n",gCurrCourseNum,save_file_get_star_flags(gCurrSaveFileNum-1,gCurrCourseNum-1));
    return 1;
}
/* Called only between complete SM64 frames; both engines pause in the picker.
 * Cutscenes/carrying/death finish before a queued request is serviced. */
int smb_host_change_menu_ready(void) {
    struct MarioState* m=gMarioState;
    if(getenv("SMB_SMOKE")&&getenv("SMB_SCENARIO")&&!strcmp(getenv("SMB_SCENARIO"),"character-switch")) {
        static unsigned test;
        static const char* names[]={"sonic","bowser","sheik","iceclimbers","gamewatch","link","link","fox"};
        static const char* outfits[]={"6","3","4","2","3","4","0","2"};
        if(test<8&&frames>=80+test*100) {
            SDL_setenv("SMB_SWITCH_TEST_NAME",names[test],1);SDL_setenv("SMB_SWITCH_TEST_COSTUME",outfits[test],1);
            SDL_setenv("SMB_SWITCH_TEST_CANCEL",test==7?"1":"0",1);
            if(test%2){SDL_Event event={0};event.type=SDL_CONTROLLERBUTTONDOWN;event.cbutton.button=SDL_CONTROLLER_BUTTON_BACK;SDL_PushEvent(&event);}
            else smb_host_key(SDL_SCANCODE_F6,1);
            test++;
        }
    }
    if(!character_menu_requested||!embedded_switch||!m||!m->marioObj||!active||m->health<0x100||world_carry||m->heldObj||cannon_pending||
       (m->action&ACT_GROUP_MASK)==ACT_GROUP_CUTSCENE||(m->action&ACT_GROUP_MASK)==ACT_GROUP_AUTOMATIC||
       !m->floor||m->floor->type==SURFACE_DEATH_PLANE||m->floor->normal.y<.05f)return 0;
    int result=character_menu_requested;character_menu_requested=0;character_menu_open=1;round_input_pending=0;return result;
}
void smb_host_change_menu_closed(void) {
    character_menu_open=0;round_input_pending=0;
    memset(host_keys,0,sizeof host_keys);controller_buttons=0;controller_look[0]=controller_look[1]=0;character_input_latch=1;
    host_keys[SDL_SCANCODE_F8]=SDL_GetKeyboardState(NULL)[SDL_SCANCODE_F8]!=0;
    if(gPlayer1Controller){gPlayer1Controller->buttonDown=0;gPlayer1Controller->buttonPressed=0;}
    if(gPlayer3Controller){gPlayer3Controller->buttonDown=0;gPlayer3Controller->buttonPressed=0;}
}
int smb_host_change_character(unsigned character,unsigned costume,const char* name) {
    struct MarioState* m=gMarioState;
    if(!embedded_switch||!embedded_costumes||costume>=embedded_costumes(character)||embedded_switch(character,costume))return -1;
    /* Native replacement is queued until its coroutine resumes. The next
     * command places the new fighter on this surface and starts rebirth. */
    m->pos[1]=m->floorHeight+2;memset(m->vel,0,sizeof m->vel);m->forwardVel=0;
    set_mario_action(m,ACT_FREEFALL,0);m->invincTimer=60;
    active=0;respawn_pending=1;host_dead=0;cannon_pending=0;world_carry=0;native_grab_target=NULL;
    character_changed_at=frames;
    camera_ready=0;last_motion=last_hitboxes=UINT32_MAX;
    char value[16];snprintf(value,sizeof value,"%u",costume);SDL_setenv("SMB_PLAYER_COSTUME",value,1);
    SDL_setenv("MELEE3D_NATIVE_CHARACTER",name,1);
    fprintf(stderr,"SMB SWITCH CONFIRM character=%s external=%u costume=%u level=%d coins=%d stars=%d lives=%d wave=%u defeated=%u free_respawn=1\n",name,character,costume,gCurrLevelNum,m->numCoins,m->numStars,m->numLives,encounter_wave,encounter_defeated);
    return 0;
}
#include "sm64_scenarios.inc"
#include "sm64_interaction_scenarios.inc"
#include "sm64_finish_scenarios.inc"
#include "sm64_swim_control_scenarios.inc"
#include "sm64_air_scenarios.inc"
#include "sm64_round_scenarios.inc"
int smb_host_frame(struct MarioState* m) {
    static int route_checked,route_landing;
    if(!route_checked&&embedded_step) {
        route_checked=1;int stage=getenv("SMB_START_STAGE")?atoi(getenv("SMB_START_STAGE")):-1;
        if(stage>=0&&stage<27) {
            const SmbStageStart* start=&stage_starts[stage];route_landing=stage==23;
            extern void initiate_warp(s16,s16,s16,s32);
            gCurrActNum=gDialogCourseActNum=1;
            if(gCurrLevelNum!=(int)start->level){initiate_warp(start->level,start->area,start->node,0);active=0;return 0;}
        }
    }
    if(route_landing&&gCurrLevelNum==LEVEL_TOTWC) {
        struct Surface* floor=NULL;float y=find_floor(350,20000,0,&floor);
        if(floor&&floor->type!=SURFACE_WARP&&floor->type!=SURFACE_DEATH_PLANE) {
            m->pos[0]=350;m->pos[1]=y+2;m->pos[2]=0;m->floor=floor;m->floorHeight=y;
            memset(m->vel,0,sizeof m->vel);m->forwardVel=0;set_mario_action(m,ACT_FREEFALL,0);active=0;
            route_landing=0;fprintf(stderr,"SMB STAGE PLATFORM level=%d x=350 y=%.1f z=0\n",gCurrLevelNum,y);
        }
    }
    recording_scene_ready=1;
    if(embedded_powerups)embedded_powerups(((m->flags&MARIO_METAL_CAP)?1:0)|((m->flags&MARIO_VANISH_CAP)?2:0));
    const char* path=getenv("SM64_MELEE_SOCKET");
    if(!path&&!embedded_step)return 0;
    if(getenv("SMB_SMOKE")&&getenv("SMB_SCENARIO")) {
        static unsigned dialog_tick;if(++dialog_tick%24==0)gPlayer3Controller->buttonPressed|=A_BUTTON;
    }
    static int previous_lives=-100;
    if(m->health<0x100&&!host_dead) {
        cannon_pending=0;air_remaining=0x880;air_heal_pending=air_hurt_pending=0;water_active=0;host_dead=1;respawn_pending=1;world_carry=0;
        if(embedded_health)embedded_health(-1000,0,0);
        fprintf(stderr,"SMB WORLD DEATH action=%08x lives=%d native_reset=1\n",m->action,m->numLives);
    }
    if(previous_lives!=-100&&m->numLives<previous_lives&&embedded_health){if(!host_dead)embedded_health(-1000,0,0);respawn_pending=1;}
    previous_lives=m->numLives;
    smb_finish_prepare(m);
    smb_swim_control_prepare(m);
    smb_air_test_prepare(m);
    smb_round_test_prepare(m);
    int world_changed=publish_world();
    smb_host_npc_observe(m,world_changed);
    smb_world_interaction_test(m);
    if(world_carry&&!m->heldObj&&(m->action&ACT_GROUP_MASK)!=ACT_GROUP_OBJECT)world_carry=0;
    if(embedded_celebrate) {
        int star=m->action==ACT_STAR_DANCE_EXIT||m->action==ACT_STAR_DANCE_NO_EXIT||m->action==ACT_STAR_DANCE_WATER;
        float p[3]={m->pos[0]*.1f,m->pos[1]*.1f,m->pos[2]*.1f};embedded_celebrate(star,p);
    }
    if(world_carry||m->heldObj) {
        /* Native grab is also a natural release button during host carrying. */
        static int held_grab=1;int down=host_keys[SDL_SCANCODE_LSHIFT]||(controller_buttons&0x10);
        if(down&&!held_grab)m->input|=INPUT_B_PRESSED;
        held_grab=down;
    }
    if(m->action==ACT_IN_CANNON) {
        static unsigned fire_down;unsigned fire=host_keys[SDL_SCANCODE_L]||host_keys[SDL_SCANCODE_COMMA]||(controller_buttons&0x500);
        if(m->actionState==2&&fire&&!fire_down)m->input|=INPUT_A_PRESSED;
        fire_down=fire;
    }
    /* SM64 owns the hold/aim sequence. On release, transfer a launch vector
     * once; native Melee then owns tumble, gravity, collision and recovery. */
    int cannon=cannon_pending||m->action==ACT_SHOT_FROM_CANNON;
    int launch=m->health>=0x100&&(cannon||m->action==ACT_THROWN_FORWARD||m->action==ACT_THROWN_BACKWARD);
    float release_velocity[3]={0};
    if(launch) {
        release_velocity[0]=m->forwardVel*sins(m->faceAngle[1])*.05f;
        release_velocity[1]=m->vel[1]*.05f;
        release_velocity[2]=m->forwardVel*coss(m->faceAngle[1])*.05f;
        if(cannon_pending){memcpy(m->pos,cannon_position,sizeof cannon_position);memcpy(release_velocity,cannon_velocity,sizeof release_velocity);cannon_pending=0;}
        fprintf(stderr,"SMB WORLD RELEASE source=%s x=%.2f y=%.2f z=%.2f velocity=%.4f,%.4f,%.4f\n",cannon?"cannon":"enemy",m->pos[0],m->pos[1],m->pos[2],release_velocity[0],release_velocity[1],release_velocity[2]);
        set_mario_action(m,ACT_FREEFALL,0);m->invincTimer=30;
        m->faceAngle[0]=m->marioObj->header.gfx.angle[0]=0;
        if(cannon)set_camera_mode(m->area->camera,m->area->camera->defMode,1);
        active=0;
    }
    if(getenv("SMB_SAVE_MODE")&&strcmp(getenv("SMB_SAVE_MODE"),"unlocked")&&m->action==ACT_INTRO_CUTSCENE) {
        static unsigned opening_ticks;
        if(++opening_ticks%120==1)fprintf(stderr,"SMB PLAYTHROUGH OPENING state=%u timer=%u cutscene=%d\n",m->actionState,m->actionTimer,m->area->camera->cutscene);
    }
    int round_press=controller_round_button&&!controller_round_latched;
    controller_round_latched=controller_round_button;
    const char* round_test=getenv("SMB_SCENARIO");
    if(round_press&&smb_round_controls_ready()&&(!getenv("SMB_SMOKE")||(round_test&&!strcmp(round_test,"thematic-rounds"))))round_input_pending=1;
    if(!smb_round_controls_ready())round_input_pending=0;
    if(world_carry||m->heldObj||m->health<0x100||(m->action&ACT_GROUP_MASK)==ACT_GROUP_CUTSCENE ||
       (m->action&ACT_GROUP_MASK)==ACT_GROUP_AUTOMATIC) {
        round_input_pending=0;
        if(active)fprintf(stderr,"SMB WORLD HANDOFF action=%08x x=%.2f y=%.2f z=%.2f\n",m->action,m->pos[0],m->pos[1],m->pos[2]);
        active=0;return 0;}
    if(round_input_pending) {
        round_input_pending=0;
        int result=smb_host_spawn_round();
        fprintf(stderr,"SMB ROUND INPUT frame=%u result=%d source=%s\n",frames,result,round_press?"L3":"F8");
        if(result<0)play_sound(SOUND_MENU_CAMERA_BUZZ,gDefaultSoundArgs);
    }
    smb_host_npcs(m);
    if(!frames&&embedded_health)embedded_health(-1000,0,0);
    if(!embedded_step&&connection<0)connect_worker(path);
    /* The control regression drives the same event-backed keys and SM64
     * button mapping used by live play, rather than injecting native bits. */
    int pad_test=getenv("SMB_SCENARIO")&&!strcmp(getenv("SMB_SCENARIO"),"controller");
    int controls_test=pad_test||(getenv("SMB_SCENARIO")&&!strcmp(getenv("SMB_SCENARIO"),"controls"));
    unsigned controls_phase=frames/120,controls_tick=frames%120;
    if(controls_test) {
        if(!controls_tick){m->pos[0]=-1328;m->pos[1]=270;m->pos[2]=4664;active=0;}
        smb_host_key(SDL_SCANCODE_Q,!pad_test&&controls_phase==1&&controls_tick>=34&&controls_tick<80);
        smb_host_key(SDL_SCANCODE_E,!pad_test&&controls_phase==2&&controls_tick>=34&&controls_tick<80);
        smb_host_key(SDL_SCANCODE_RIGHT,!pad_test&&controls_phase==0&&controls_tick>=85&&controls_tick<101);
        smb_host_key(SDL_SCANCODE_UP,!pad_test&&controls_phase==0&&controls_tick>=105&&controls_tick<115);
        if(pad_test) {
            unsigned buttons=0;
            if(controls_tick>=30&&controls_tick<32)buttons|=1u<<SDL_CONTROLLER_BUTTON_X;
            if(controls_tick>=40&&controls_tick<44)buttons|=1u<<SDL_CONTROLLER_BUTTON_B;
            if(controls_phase==0&&controls_tick>=75&&controls_tick<78)buttons|=1u<<SDL_CONTROLLER_BUTTON_A;
            if(controls_phase==1&&controls_tick>=34&&controls_tick<80)buttons|=1u<<SDL_CONTROLLER_BUTTON_LEFTSHOULDER;
            if(controls_phase==3&&controls_tick>=34&&controls_tick<44)buttons|=1u<<SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
            if(controls_phase==5&&controls_tick>=34&&controls_tick<44)buttons|=1u<<SDL_CONTROLLER_BUTTON_DPAD_UP;
            smb_host_controller_state(buttons,controls_phase==2&&controls_tick>=34&&controls_tick<80?1:0,controls_phase==4&&controls_tick>=34&&controls_tick<44?1:0,1);
            smb_host_controller_look(controls_phase==0&&controls_tick>=85&&controls_tick<101?1:0,
                controls_phase==0&&controls_tick>=105&&controls_tick<115?1:0);
            if(controls_tick==36) {
                unsigned host=smb_host_camera_pad(buttons);
                fprintf(stderr,"SMB CONTROL SEPARATION phase=%u physical=%x combat=%x host_camera=%x host=%x\n",controls_phase,buttons,controller_buttons,host&(R_TRIG|L_TRIG|U_CBUTTONS|D_CBUTTONS|L_CBUTTONS|R_CBUTTONS),host);
            }
        }
    }
    unsigned wing_input=controller_active?controller_buttons:((host_keys[SDL_SCANCODE_Q]?8:0)|(host_keys[SDL_SCANCODE_L]?0x400:0));
    if((m->flags&MARIO_WING_CAP)&&active&&!snapshot.grounded&&(wing_input&0x408)==0x408&&m->action!=ACT_FLYING) {
        m->forwardVel=fmaxf(32,hypotf(m->vel[0],m->vel[2]));m->faceAngle[0]=0;
        set_mario_action(m,ACT_FLYING,0);
    }
    unsigned hosted_action=m->action;
    unsigned hosted=m->action==ACT_FLYING?SMB_ENV_WING:(m->action&ACT_FLAG_RIDING_SHELL)?SMB_ENV_SHELL:0;
    /* Enter below the normal SM64 swim line; keep an existing swimmer wet
     * through the surface so a water jump can carry its full launch velocity. */
    int was_swimming=water_active&&active&&world_changed!=2;
    water_active=!hosted&&m->waterLevel>-10000&&m->pos[1]<m->waterLevel+(was_swimming?20:-70);
    SmbInput cmd={0};cmd.magic=SMB_MAGIC;
    if(m->waterLevel>-10000)cmd.water_level=m->waterLevel*.1f;
    if(water_active)cmd.flags|=SMB_ENV_WATER;
    if(hosted){cmd.flags|=hosted|1;cmd.x=m->pos[0]*.1f;cmd.y=m->pos[1]*.1f;cmd.z=m->pos[2]*.1f;}

    /* SM64 already applies its deadzone and caps this vector at radius 64.
     * Normalize that processed range to Melee's full [-1,1] stick. */
    cmd.stick_x=m->controller->stickX/64.f;cmd.stick_y=m->controller->stickY/64.f;
    cmd.camera_yaw=m->area->camera->yaw*(6.28318530718f/65536.f);
    /* SM64 A=jump, B=normal attack, Z=special, R=shield, L=grab.
     * Melee button bits are explicit protocol values, not SM64's N64 bits. */
    unsigned held=m->controller->buttonDown;
    if(controller_active&&!getenv("SMB_SMOKE"))held=smb_host_keyboard_buttons();
    if(controls_test)held=pad_test?0:(controls_tick>=30&&controls_tick<32?A_BUTTON:0)|
        (controls_tick>=40&&controls_tick<44?Z_TRIG:0)|
        (controls_phase==0&&controls_tick>=75&&controls_tick<78?B_BUTTON:0);
    if(held&A_BUTTON)cmd.buttons|=0x400;
    if(held&B_BUTTON)cmd.buttons|=0x100;
    if(held&Z_TRIG)cmd.buttons|=0x200;
    if(held&R_TRIG)cmd.buttons|=0x20;
    if(held&L_TRIG)cmd.buttons|=0x10;
    const unsigned char* keys=host_keys;
    /* Arrow/right-stick look is camera input, independent of Q/E attacks. */
    if(keys[SDL_SCANCODE_Q])cmd.buttons|=0x8;
    if(keys[SDL_SCANCODE_E])cmd.buttons|=0x40;
    if(controller_active&&(!getenv("SMB_SMOKE")||pad_test))cmd.buttons|=controller_buttons;
    cmd.look_x=controller_look[0]+keys[SDL_SCANCODE_RIGHT]-keys[SDL_SCANCODE_LEFT];
    cmd.look_y=controller_look[1]+keys[SDL_SCANCODE_UP]-keys[SDL_SCANCODE_DOWN];
    cmd.look_x=fmaxf(-1,fminf(1,cmd.look_x));cmd.look_y=fmaxf(-1,fminf(1,cmd.look_y));
    if(getenv("SMB_INPUT_TRACE")&&(cmd.buttons||cmd.look_x||cmd.look_y))fprintf(stderr,"SMB HOST INPUT seq=%u buttons=%x look=%.2f,%.2f q=%u e=%u\n",sequence+1,cmd.buttons,cmd.look_x,cmd.look_y,keys[SDL_SCANCODE_Q],keys[SDL_SCANCODE_E]);
    if(getenv("SMB_SMOKE")&&!controls_test) {
        cmd.stick_x=0;cmd.buttons=0;
        cmd.stick_y=frames<120?.75f:0;
        if(frames>=30&&frames<35)cmd.buttons|=0x400;
        if(frames>=70&&frames<74)cmd.buttons|=0x100;
        if(getenv("SMB_ROSTER_TEST")) {
            if(frames>=100&&frames<104)cmd.buttons|=0x100; /* After slow-fall landing lag. */
            if(frames>=145&&frames<155)cmd.buttons|=0x200;
        }
    }
    smb_scenario(m,&cmd);smb_interaction_input(m,&cmd);smb_finish_input(m,&cmd);smb_swim_control_input(m,&cmd);
    smb_air_test_input(&cmd);
    smb_round_test_input(&cmd);
    if(water_active) {
        /* L1/L2 (Q/E) retain attack modifiers and independently choose swim
         * depth. These private bits are never injected into the native pad. */
        if(cmd.buttons&0x8)cmd.buttons|=SMB_SWIM_UP;
        if(cmd.buttons&0x40)cmd.buttons|=SMB_SWIM_DOWN;
    }
    if(hosted)cmd.buttons=cmd.stick_x=cmd.stick_y=0;
    if(character_input_latch) {
        if(!cmd.buttons&&fabsf(cmd.stick_x)<.15f&&fabsf(cmd.stick_y)<.15f)character_input_latch=0;
        cmd.buttons=0;cmd.stick_x=cmd.stick_y=cmd.look_x=cmd.look_y=0;
    }
    /* Reuse SM64's surface-specific downhill acceleration, steering and drag. */
    static int sliding;static float slide_x,slide_z;
    if(!active||world_changed==2){sliding=0;slide_x=slide_z=0;}
    int grounded=active&&world_changed!=2&&snapshot.grounded&&m->floor&&fabsf(m->pos[1]-m->floorHeight)<120;
    if(!hosted&&!water_active&&grounded&&(sliding||mario_floor_is_slippery(m))) {
        extern s32 update_sliding(struct MarioState*,f32);
        if(!sliding){sliding=1;slide_x=m->vel[0];slide_z=m->vel[2];fprintf(stderr,"SMB SLIDE BEGIN frame=%u normal_y=%.5f type=%x\n",frames,m->floor->normal.y,m->floor->type);}
        m->slideVelX=slide_x;m->slideVelZ=slide_z;
        m->slideYaw=hypotf(slide_x,slide_z)>1?atan2s(slide_z,slide_x):atan2s(m->floor->normal.z,m->floor->normal.x);
        m->forwardVel=hypotf(slide_x,slide_z);
        float steer=hypotf(cmd.stick_x,cmd.stick_y);
        m->intendedMag=fminf(1,steer)*32;
        if(steer>.1f)m->intendedYaw=atan2s(-cmd.stick_y,cmd.stick_x)+(s16)(cmd.camera_yaw*(65536.f/6.28318530718f));
        int stopped=update_sliding(m,4.f);slide_x=m->slideVelX;slide_z=m->slideVelZ;
        if(!stopped){
            if(hypotf(slide_x,slide_z)>10){play_sound(SOUND_MOVING_TERRAIN_SLIDE+m->terrainSoundAddend,m->marioObj->header.gfx.cameraToObject);m->particleFlags|=PARTICLE_DUST;}
            cmd.flags|=32;cmd.slope_velocity[0]=slide_x*.05f;cmd.slope_velocity[2]=slide_z*.05f;}
        else sliding=0;
        if(getenv("SMB_SCENARIO")&&frames%10==0)fprintf(stderr,"SMB SLIDE VELOCITY frame=%u x=%.4f z=%.4f normal_y=%.5f\n",frames,slide_x,slide_z,m->floor->normal.y);
    } else {sliding=0;slide_x=slide_z=0;}
    if(launch){cmd.flags&=~32u;cmd.flags|=128|(cannon?256:0);memcpy(cmd.slope_velocity,release_velocity,sizeof release_velocity);}
    if(respawn_pending){cmd.flags|=64;cmd.spawn_headroom=m->ceil?fmaxf(0,(m->ceilHeight-m->pos[1])*.1f):80.f;respawn_pending=0;host_dead=0;fprintf(stderr,"SMB WORLD REBIRTH lives=%d\n",m->numLives);}

    if(!active||world_changed==2) {
        pound_air=pound_land=0;
        fprintf(stderr,"SMB WORLD RESUME action=%08x x=%.2f y=%.2f z=%.2f\n",m->action,m->pos[0],m->pos[1],m->pos[2]);
        cmd.flags|=1|4;cmd.x=m->pos[0]*.1f;cmd.y=m->pos[1]*.1f;cmd.z=m->pos[2]*.1f;
        active=1;
    } else if(snapshot.grounded||smb_attached()) {
        float delta[3]={m->pos[0]-snapshot.x*10,m->pos[1]-snapshot.y*10,m->pos[2]-snapshot.z*10};
        if(fabsf(delta[0])+fabsf(delta[1])+fabsf(delta[2])>.01f) {
            cmd.flags|=2;cmd.x=delta[0]*.1f;cmd.y=delta[1]*.1f;cmd.z=delta[2]*.1f;
        }
        struct Object* platform=m->marioObj->platform;
        if(platform&&platform->oAngleVelYaw) {cmd.flags|=16;cmd.platform_yaw=platform->oAngleVelYaw*(6.28318530718f/65536.f);}
    }
    smb_host_npc_environment(m);
    smb_host_actor_water(m,&cmd);
    int was_grounded=active&&snapshot.grounded;float fall_speed=snapshot.vy;
    native_grab_target=NULL;
    for(int step=0;step<2;step++) {
        cmd.sequence=++sequence;
        if(embedded_step) {
            if(embedded_step(&cmd,&snapshot)){fprintf(stderr,"SMB HOST FAIL embedded step\n");exit(2);}
        } else {io_full(&cmd,sizeof cmd,1);io_full(&snapshot,sizeof snapshot,0);}
        if(snapshot.magic!=SMB_MAGIC||snapshot.sequence!=sequence||snapshot.bones>SMB_BONES)exit(2);
        if(embedded_step&&snapshot.reserved!=sequence) {
            fprintf(stderr,"SMB HOST FAIL tick count expected=%u actual=%u\n",sequence,snapshot.reserved);exit(2);
        }
        smb_host_apply_hits();
        cmd.flags&=32|SMB_ENV_WATER|SMB_ENV_HOSTED;
        if(hosted)cmd.flags|=1;
    }
    if(hosted) {
        m->action=hosted_action;frames++;
        if(embedded_step&&getenv("SMB_EMBED_TEST_PAUSE")&&frames==90){
            fprintf(stderr,"SMB HOST PAUSE begin native_ticks=%u\n",snapshot.reserved);usleep(1000000);
            fprintf(stderr,"SMB HOST PAUSE end native_ticks=%u\n",snapshot.reserved);
        }
        if(getenv("SMB_SMOKE")&&frames>= (unsigned)atoi(getenv("SMB_SMOKE_FRAMES")?:"180")) {
            fprintf(stderr,"SMB HOST SMOKE COMPLETE frames=%u\nSMB HOST EMBED COMPLETE pid=%d host_frames=%u native_ticks=%u\n",frames,getpid(),frames,snapshot.reserved);exit(0);
        }
        return 0; /* SM64 executes flight/shell movement and its world objects. */
    }
    float* p=m->pos;
    p[0]=snapshot.x*10;p[1]=snapshot.y*10;p[2]=snapshot.z*10;
    for(int k=0;k<3;k++)if(!isfinite(p[k]))exit(2);
    m->vel[0]=snapshot.vx*20;m->vel[1]=snapshot.vy*20;m->vel[2]=snapshot.vz*20;
    m->forwardVel=hypotf(m->vel[0],m->vel[2]);
    if(!was_grounded&&snapshot.grounded&&fall_speed<-.5f) {
        play_mario_landing_sound(m,fall_speed< -3?SOUND_ACTION_TERRAIN_HEAVY_LANDING:SOUND_ACTION_TERRAIN_LANDING);
    }
    m->floorHeight=find_floor(p[0],p[1]+100,p[2],&m->floor);
    smb_host_recover_floor(m);
    if(snapshot.grounded){m->marioObj->platform=m->floor?m->floor->object:NULL;gMarioPlatform=m->marioObj->platform;}
    m->action=(snapshot.hitboxes||snapshot.grab_active)?ACT_PUNCHING:snapshot.grounded?(sliding?ACT_BUTT_SLIDE:m->forwardVel>1?ACT_WALKING:ACT_IDLE):ACT_FREEFALL;
    if(water_active)m->action=ACT_WATER_IDLE;
    smb_swim_control_after(m);
    if(!was_grounded&&(snapshot.abilities&SMB_DOWN_ATTACK))pound_air=1;
    /* Only a real downward landing or an active grounded down hit may pound
     * a floor. Preserve Melee's animation, velocity and attack hitboxes. */
    int down_hit=0;for(unsigned h=0;h<snapshot.hit_count;h++)if((snapshot.hit[h].flags&32)&&snapshot.hit[h].damage>0)down_hit=1;
    if(snapshot.grounded&&((!was_grounded&&pound_air)||down_hit)) {
        pound_land=3;pound_air=0;
        fprintf(stderr,"SMB GROUND POUND frame=%u kind=%u platform=%d native_motion=%u\n",frames,snapshot.kind,m->marioObj->platform!=NULL,snapshot.motion);
    }
    if(snapshot.grounded&&pound_land){m->action=ACT_GROUND_POUND_LAND;m->actionState=0;m->vel[1]=fminf(-1,m->vel[1]);pound_land--;}
    else if(pound_air&&snapshot.vy<0)m->action=ACT_GROUND_POUND;
    if(!snapshot.grounded&&!(snapshot.abilities&SMB_DOWN_ATTACK)&&!(snapshot.motion>=29&&snapshot.motion<=40))pound_air=0;
    if(getenv("SMB_SCENARIO")&&!strcmp(getenv("SMB_SCENARIO"),"no-fast-fall"))fprintf(stderr,"SMB NO FASTFALL phase=%u tick=%u y=%.6f vy=%.6f fast=%u\n",frames/100,frames%100,snapshot.y,snapshot.vy,!!(snapshot.abilities&4));
    m->faceAngle[1]=atan2s(snapshot.facing[2],snapshot.facing[0]);
    m->marioObj->header.gfx.angle[1]=m->faceAngle[1];
    if(snapshot.grab_active)m->actionArg=0;
    m->flags&=~(MARIO_PUNCHING|MARIO_KICKING);
    /* Native capsule intersections already apply world attack messages. */
    m->marioObj->header.gfx.pos[0]=p[0];m->marioObj->header.gfx.pos[1]=p[1];m->marioObj->header.gfx.pos[2]=p[2];
    if(frames++%30==0||(getenv("SMB_SCENARIO")&&(!strcmp(getenv("SMB_SCENARIO"),"ledges")||!strcmp(getenv("SMB_SCENARIO"),"walls")))||last_motion!=snapshot.motion||last_hitboxes!=snapshot.hitboxes) {
        fprintf(stderr,"SMB HOST FRAME frame=%u kind=%u motion=%u bones=%u x=%.2f y=%.2f z=%.2f hitboxes=%u grounded=%u coins=%d stars=%d level=%d\n",
                frames,snapshot.kind,snapshot.motion,snapshot.bones,p[0],p[1],p[2],snapshot.hitboxes,snapshot.grounded,m->numCoins,m->numStars,gCurrLevelNum);
        fflush(stderr);
    }
    last_motion=snapshot.motion;last_hitboxes=snapshot.hitboxes;
    if(embedded_camera&&melee_camera_enabled()) {
        if(!embedded_camera(4.f/3,&native_camera))camera_ready=1;
        if(camera_ready&&frames%30==1)fprintf(stderr,"SMB CAMERA provider=melee eye=%.2f,%.2f,%.2f target=%.2f,%.2f,%.2f fov=%.2f yaw=%.5f\n",native_camera.eye[0],native_camera.eye[1],native_camera.eye[2],native_camera.target[0],native_camera.target[1],native_camera.target[2],native_camera.fov,native_camera.yaw);
    }
    if(embedded_step&&getenv("SMB_EMBED_TEST_PAUSE")&&frames==90) {
        fprintf(stderr,"SMB HOST PAUSE begin native_ticks=%u\n",snapshot.reserved);
        usleep(1000000);
        fprintf(stderr,"SMB HOST PAUSE end native_ticks=%u\n",snapshot.reserved);
    }
    const char* smoke_frames=getenv("SMB_SMOKE_FRAMES");
    unsigned smoke_limit=smoke_frames?(unsigned)atoi(smoke_frames):180;
    if(getenv("SMB_SMOKE")&&frames>=smoke_limit) {fprintf(stderr,"SMB HOST SMOKE COMPLETE frames=%u\n",frames);
        if(embedded_step)fprintf(stderr,"SMB HOST EMBED COMPLETE pid=%d host_frames=%u native_ticks=%u\n",getpid(),frames,snapshot.reserved);
        exit(0);}
    return 1;
}

extern void smb_gl_composite(const float*,const float*,const unsigned char*,unsigned,unsigned,int);
/* Menu camera is independent of the adventure camera and physics. */
static unsigned smb_title_frames;
Gfx* smb_host_title_display_list(void) {
    if(!embedded_render)return NULL;
    static Gfx list[2];list[0].words.w0=0xC2000000;list[0].words.w1=0;gSPEndDisplayList(&list[1]);return list;
}
int smb_host_intro_finished(void) {
    if(getenv("SMB_TITLE_PREVIEW"))return 0;
    if(getenv("SMB_SMOKE"))return 1;
    return getenv("SMB_QUICK_START")&&smb_title_frames>=90;
}
unsigned smb_host_title_frames(void){return smb_title_frames;}
void smb_host_render_title(unsigned w,unsigned h) {
    if(!smb_title_frames)fprintf(stderr,"SMB TITLE OPEN fighter=%s native_3d=1 original_sm64_scene=1 stock_mario_head=0\n",getenv("MELEE3D_NATIVE_CHARACTER"));
    smb_title_frames++;
    float yaw=.4f+sinf(smb_title_frames*.018f)*.35f;
    extern void smb_host_title_frame(unsigned,unsigned,float);
    smb_host_title_frame(w,h,yaw);
}
void smb_host_title_frame(unsigned w,unsigned h,float yaw) {
    if(!embedded_render||!w||!h)return;
    static unsigned capacity;static float *color,*depth;static unsigned char* transmission;
    if(w*h>capacity){capacity=w*h;color=realloc(color,capacity*16);depth=realloc(depth,capacity*4);transmission=realloc(transmission,capacity*4);if(!color||!depth||!transmission)exit(2);}
    SmbRender request={0};request.width=w;request.height=h;request.flags=5;request.yaw=yaw;
    request.view[0]=request.view[5]=request.view[10]=1;request.view[7]=-14;request.view[11]=-80;
    float f=1.f/tanf(32.f*.00872664626f),clipNear=1,clipFar=2000;
    request.projection[0]=f*h/w;request.projection[2]=0;request.projection[5]=f;
    /* GX eye-space depth spans -1..0; Aurora performs its device conversion. */
    request.projection[10]=-clipNear/(clipFar-clipNear);request.projection[11]=-clipFar*clipNear/(clipFar-clipNear);request.projection[14]=-1;
    if(embedded_render(&request,color,depth,transmission)){fprintf(stderr,"SMB HOST FAIL title render\n");exit(2);}
    static unsigned title_frame;if(++title_frame==21){unsigned pixels=0;for(unsigned i=0;i<w*h;i++)if(fabsf(color[i*4])+fabsf(color[i*4+1])+fabsf(color[i*4+2])>.05f)pixels++;fprintf(stderr,"SMB TITLE FIGHTER pixels=%u\n",pixels);}
    smb_gl_composite(color,depth,transmission,w,h,1);
}
static SmbRender smb_hud_request;
static int smb_hud_pending;
void smb_host_render_native(const float* view,const float* projection,unsigned width,unsigned height,float aspect) {
    if(!embedded_render||!gMarioState||host_dead)return;
    /* Match the actual SM64 framebuffer, including Retina pixels. */
    unsigned w=width,h=height;
    static unsigned capacity;static float* rgba;static float* depth;static unsigned char* transmission;
    if(w*h>capacity){capacity=w*h;rgba=realloc(rgba,capacity*16);depth=realloc(depth,capacity*4);transmission=realloc(transmission,capacity*4);if(!rgba||!depth||!transmission)exit(2);}
    SmbRender request={0};request.width=w;request.height=h;
    request.flags=active?((gMarioState->action==ACT_FLYING||(gMarioState->action&ACT_FLAG_RIDING_SHELL))?1:0):5;request.yaw=gMarioState->faceAngle[1]*(6.28318530718f/65536.f);
    for(int axis=0;axis<3;axis++)request.position[axis]=gMarioState->pos[axis]*.1f;
    for(int row=0;row<3;row++)for(int col=0;col<4;col++)request.view[row*4+col]=view[col*4+row]*(col==3?.1f:1);
    for(int row=0;row<4;row++)for(int col=0;col<4;col++)request.projection[row*4+col]=projection[col*4+row];
    for(int col=0;col<4;col++) {
        request.projection[col]*=aspect;
        request.projection[8+col]=.5f*projection[col*4+2]-.5f*projection[col*4+3];
    }
    request.projection[11]*=.1f;
    static int first=1;
    if(first) {first=0;float eye[4]={0},clip[4]={0};float point[4]={snapshot.x,snapshot.y+10,snapshot.z,1};
        for(int row=0;row<3;row++)for(int col=0;col<4;col++)eye[row]+=request.view[row*4+col]*point[col];
        eye[3]=1;
        for(int row=0;row<4;row++)for(int col=0;col<4;col++)clip[row]+=request.projection[row*4+col]*eye[col];
        fprintf(stderr,"SMB RENDER CAMERA eye=%.3f,%.3f,%.3f ndc=%.5f,%.5f,%.5f p=%.5f,%.5f\n",eye[0],eye[1],eye[2],clip[0]/clip[3],clip[1]/clip[3],clip[2]/clip[3],request.projection[10],request.projection[11]);
    }
    if(embedded_render(&request,rgba,depth,transmission)){fprintf(stderr,"SMB HOST FAIL native render\n");exit(2);}
    smb_gl_composite(rgba,depth,transmission,w,h,0);
    smb_hud_request=request;smb_hud_request.flags=2|(request.flags&4);smb_hud_pending=1;
    static unsigned portrait_frames;
    if(0&&++portrait_frames==90) {
        unsigned count=0;
        for(unsigned y=h*30/480;y<h*62/480;y++)for(unsigned x=h*44/480;x<h*76/480;x++)if(x<w&&rgba[(y*w+x)*4+3]>.01f)count++;
        fprintf(stderr,"SMB LIVES PIXELS count=%u\n",count);
    }
    static unsigned frames;
    if(getenv("SMB_LIGHTING_PROBE")&&frames%30==0) {
        double luma=0;unsigned count=0;
        for(unsigned y=h/4;y<h*3/4;y++)for(unsigned x=w/3;x<w*2/3;x++) {
            unsigned i=y*w+x;
            if(rgba[i*4+3]>.5f&&depth[i]>.01f&&depth[i]<.9999f) {
                luma+=255*(.2126*rgba[i*4]+.7152*rgba[i*4+1]+.0722*rgba[i*4+2]);count++;
            }
        }
        fprintf(stderr,"SMB LIGHTING LUMA frame=%u pixels=%u mean=%.4f\n",frames,count,count?luma/count:0);
        if(frames==90) {
            FILE* capture=fopen(getenv("SMB_LIGHTING_PROBE"),"wb");
            if(capture){fwrite(&w,4,1,capture);fwrite(&h,4,1,capture);for(unsigned i=0;i<w*h*4;i++){unsigned char c=(unsigned char)(fmaxf(0,fminf(1,rgba[i]))*255);fwrite(&c,1,1,capture);}fclose(capture);}
        }
    }
    if(frames++%90==0) {
        unsigned pixels=0,rgb_pixels=0,fractional=0,additive=0;float minimum=1,maximum=0;
        for(unsigned i=0;i<w*h;i++){if(rgba[i*4]||rgba[i*4+1]||rgba[i*4+2])rgb_pixels++;}
        for(unsigned i=0;i<w*h;i++)if(rgba[i*4+3]>.001f||rgba[i*4]||rgba[i*4+1]||rgba[i*4+2]){pixels++;if(rgba[i*4+3]>.001f&&rgba[i*4+3]<.999f)fractional++;if(rgba[i*4+3]<.001f&&(rgba[i*4]||rgba[i*4+1]||rgba[i*4+2]))additive++;minimum=fminf(minimum,depth[i]);maximum=fmaxf(maximum,depth[i]);}
        fprintf(stderr,"SMB HOST NATIVE RENDER frame=%u pixels=%u width=%u height=%u depth_min=%.6f depth_max=%.6f rgb_pixels=%u translucent=%u additive=%u framebuffer=%ux%u\n",frames,pixels,w,h,minimum,maximum,rgb_pixels,fractional,additive,width,height);
    }
}

/* Called after every SM64 world/HUD display list, including translucent scenery. */
void smb_host_render_hud(void) {
    if(!smb_hud_pending||!embedded_render)return;smb_hud_pending=0;
    unsigned w=smb_hud_request.width,h=smb_hud_request.height;
    static unsigned capacity;static float *color,*depth;static unsigned char* transmission;
    if(w*h>capacity){capacity=w*h;color=realloc(color,capacity*16);depth=realloc(depth,capacity*4);transmission=realloc(transmission,capacity*4);if(!color||!depth||!transmission)exit(2);}
    if(embedded_render(&smb_hud_request,color,depth,transmission)){fprintf(stderr,"SMB HOST FAIL HUD render\n");exit(2);}
    int probe=getenv("SMB_HUD_OCCLUSION_PROBE")&&frames==90;
    float* background=NULL;float depth_before=0,depth_after=0;
    if(probe) {
        glPushAttrib(GL_ALL_ATTRIB_BITS);GLint program;glGetIntegerv(GL_CURRENT_PROGRAM,&program);glUseProgram(0);
        glDisable(GL_TEXTURE_2D);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);glEnable(GL_DEPTH_TEST);glDepthFunc(GL_ALWAYS);glDepthMask(GL_TRUE);
        glMatrixMode(GL_PROJECTION);glPushMatrix();glLoadIdentity();glMatrixMode(GL_MODELVIEW);glPushMatrix();glLoadIdentity();
        glColor4f(.28f,.32f,.36f,1);glBegin(GL_QUADS);glVertex3f(-1,-1,-1);glVertex3f(1,-1,-1);glVertex3f(1,-.3f,-1);glVertex3f(-1,-.3f,-1);glEnd();
        glPopMatrix();glMatrixMode(GL_PROJECTION);glPopMatrix();glMatrixMode(GL_MODELVIEW);glUseProgram(program);glPopAttrib();
        background=malloc((size_t)w*h*12);glReadPixels(0,0,w,h,GL_RGB,GL_FLOAT,background);
        glReadPixels(w/2,h/10,1,1,GL_DEPTH_COMPONENT,GL_FLOAT,&depth_before);
    }
    smb_gl_composite(color,depth,transmission,w,h,1);
    if(probe) {
        float* result=malloc((size_t)w*h*12);glReadPixels(0,0,w,h,GL_RGB,GL_FLOAT,result);
        glReadPixels(w/2,h/10,1,1,GL_DEPTH_COMPONENT,GL_FLOAT,&depth_after);
        float error=0;unsigned samples=0;
        for(unsigned y=h*3/4;y<h;y++)for(unsigned x=0;x<w;x++) {
            unsigned at=y*w+x,out=(h-1-y)*w+x;
            if(transmission[at*4]<240||transmission[at*4+1]<240||transmission[at*4+2]<240) {
                for(unsigned k=0;k<3;k++)error=fmaxf(error,fabsf(result[out*3+k]-fmaxf(0,fminf(1,color[at*4+k]+background[out*3+k]*transmission[at*4+k]/255.f))));
                samples++;
            }
        }
        fprintf(stderr,"SMB HUD OCCLUSION samples=%u error=%.6f depth_delta=%.6f pass=%d\n",samples,error,fabsf(depth_before-depth_after),samples>100&&error<.02f&&depth_before==depth_after);
        free(background);free(result);
    }
    static unsigned portrait_frames;
    if(++portrait_frames==90) {
        unsigned count=0;
        for(unsigned y=h*30/480;y<h*62/480;y++)for(unsigned x=h*44/480;x<h*76/480;x++)if(x<w&&color[(y*w+x)*4+3]>.01f)count++;
        fprintf(stderr,"SMB LIVES PIXELS count=%u\n",count);
    }
    static unsigned ticks;if(++ticks%90==0)fprintf(stderr,"SMB HUD OVERLAY frame=%u final_pass=1 depth_test=0\n",frames);
}

#include "sm64_combat.inc"

void smb_host_post_interactions(struct MarioState* m) {
    if(embedded_step&&embedded_health&&m->health>=0x100) {
        if(m->healCounter){if(water_active)air_heal_pending+=m->healCounter*64.f;embedded_health(-m->healCounter*1.25f,0,0);m->healCounter=0;}
        if(m->hurtCounter) {
            float x=0,z=-1;if(m->interactObj){x=m->pos[0]-m->interactObj->oPosX;z=m->pos[2]-m->interactObj->oPosZ;}
            embedded_health(m->hurtCounter*2.f,x,z);m->hurtCounter=0;m->health=air_remaining;m->invincTimer=30;
        }
        if(active)m->health=air_remaining;
    }
    if(active&&native_grab_target) {
        m->interactObj=native_grab_target;m->input|=INPUT_INTERACT_OBJ_GRABBABLE;
    }
    if(active&&(snapshot.grab_active||native_grab_target)&&mario_check_object_grab(m)) {
        if(m->action==ACT_PICKING_UP) {
            /* Native grabs have no preceding Mario punch animation. */
            mario_grab_used_object(m);m->actionState=1;
            m->marioBodyState->grabPos=(m->heldObj->oInteractionSubtype&INT_SUBTYPE_GRABS_MARIO)?GRAB_POS_HEAVY_OBJ:GRAB_POS_LIGHT_OBJ;
            set_mario_animation(m,m->marioBodyState->grabPos==GRAB_POS_HEAVY_OBJ?MARIO_ANIM_GRAB_HEAVY_OBJECT:MARIO_ANIM_PICK_UP_LIGHT_OBJ);
        }
        if(embedded_hold)embedded_hold();
        world_carry=1;active=0;fprintf(stderr,"SMB WORLD GRAB action=%08x\n",m->action);
    }
}

int smb_host_shell_mount(void){return embedded_step&&active&&snapshot.grounded;}

int smb_host_shielded(void) {return embedded_step&&active&&snapshot.shielding;}

int smb_host_update_health(struct MarioState* m) {
    if(!embedded_step||!active||m->health<0x100)return 0;
    smb_air_test_before_health(m);
    /* Capture SM64 healing once before its counters are consumed. Damage
     * comes from native applied-hit events, including world hits, so blocked
     * hits cost no air and world damage cannot be counted twice. */
    smb_host_post_interactions(m);
    float damage=embedded_actor_damage(0,0);
    if(water_active&&damage>0)air_hurt_pending+=damage*32.f;
    int cold=(m->area->terrainType&TERRAIN_MASK)==TERRAIN_SNOW;
    extern s8 gDebugLevelSelect;
    air_remaining=smb_air_step(air_remaining,&air_heal_pending,&air_hurt_pending,
        water_active,!!(m->flags&MARIO_METAL_CAP),cold,m->pos[1]>=m->waterLevel-140,
        !!(m->action&ACT_FLAG_INTANGIBLE),gDebugLevelSelect);
    if(air_remaining<0x100){air_remaining=0xff;m->health=0xff;set_mario_action(m,ACT_DROWNING,0);fprintf(stderr,"SMB WATER DROWN native_action=1\n");}
    else m->health=air_remaining;
    if(water_active&&air_remaining<0x300)play_sound(SOUND_MOVING_ALMOST_DROWNING,gDefaultSoundArgs);
    if(getenv("SMB_SCENARIO")&&water_active&&frames%30==0)fprintf(stderr,"SMB WATER AIR remaining=%d y=%.2f surface=%d metal=%d\n",air_remaining,m->pos[1],m->waterLevel,!!(m->flags&MARIO_METAL_CAP));
    smb_air_test_after_health(m);
    return 1;
}

int smb_host_air_meter(void){return embedded_step&&water_active;}

int smb_host_damage_percent(void) {
    return embedded_step?(int)fmaxf(0,fminf(999,snapshot.percent)):-1;
}

float smb_host_death_clearance(void) {
    /* Native ledge hangs and recoveries extend below the walkable floor.
     * Trigger SM64's normal death warp near its death plane, after recovery. */
    return embedded_step&&active?100.f:2048.f;
}

void smb_host_recover_floor(struct MarioState* m) {
    if(embedded_step&&active&&!m->floor) {
        struct Surface* overhead=NULL;
        float height=find_floor(m->pos[0],m->pos[1]+400,m->pos[2],&overhead);
        if(!overhead&&embedded_support) {
            float point[3];if(!embedded_support(point))height=find_floor(point[0]*10,point[1]*10,point[2]*10,&overhead);
        }
        /* Above a nearby real edge, a native jump can safely have no floor
         * directly beneath its capsule. This is bookkeeping, never a snap. */
        if(overhead&&height-m->pos[1]<400) {
            m->floor=overhead;m->floorHeight=height;
        }
    }
}

void smb_host_warp_requested(struct MarioState* m,int op) {
    if(embedded_step&&(getenv("SMB_SCENARIO")||getenv("SMB_SMOKE"))) {
        static unsigned logged;if(logged++<30)fprintf(stderr,"SMB WORLD WARP op=%d action=%08x active=%d motion=%u x=%.2f y=%.2f z=%.2f floor=%.2f type=%x ticks=%u\n",op,m->action,active,snapshot.motion,m->pos[0],m->pos[1],m->pos[2],m->floorHeight,m->floor?m->floor->type:0xffff,sequence);
    }
}

int smb_host_owns_collision(void) { return embedded_step&&active; }
struct Object* smb_host_hanging_platform(void) {
    if(!embedded_step||!active||!smb_attached()||!embedded_support)return NULL;
    float point[3];struct Surface* floor=NULL;
    if(!embedded_support(point))find_floor(point[0]*10,point[1]*10,point[2]*10,&floor);
    return floor?floor->object:NULL;
}

int smb_host_sound(unsigned sound) {
    if(!embedded_step)return 0;
    /* Native fighters own their voices, including world damage. Keep the
     * host's terrain, enemy, UI and adventure sounds, without Mario vocals. */
    if((sound>>28)==2) {
        if(getenv("SMB_SOUND_TRACE"))fprintf(stderr,"SMB WORLD SOUND suppressed=mario_voice sound=%08x\n",sound);
        return 1;
    }
    if(!getenv("SMB_SOUND_TRACE"))return 0;
    const char* event=NULL;
    if(sound==SOUND_MARIO_ATTACKED)event="damage";
    if(sound==SOUND_MARIO_HRMM)event="grab";
    if(sound==SOUND_MARIO_SO_LONGA_BOWSER||sound==SOUND_MARIO_HERE_WE_GO)event="throw_or_star";
    if((sound&0xffff0000u)==(SOUND_ACTION_TERRAIN_LANDING&0xffff0000u)||
       (sound&0xffff0000u)==(SOUND_ACTION_TERRAIN_HEAVY_LANDING&0xffff0000u))event="landing";
    if(event)fprintf(stderr,"SMB WORLD SOUND event=%s sound=%08x engine=sm64\n",event,sound);
    return 0;
}

void smb_host_thrown(struct MarioState* m) {
    if(!embedded_step||!m->heldObj)return;
    fprintf(stderr,"SMB WORLD THROW state=%d x=%.2f y=%.2f z=%.2f holp=%.2f,%.2f,%.2f\n",m->heldObj->oHeldState,m->heldObj->oPosX,m->heldObj->oPosY,m->heldObj->oPosZ,m->marioBodyState->heldObjLastPosition[0],m->marioBodyState->heldObjLastPosition[1],m->marioBodyState->heldObjLastPosition[2]);
}

void smb_host_coin(struct MarioState* m,int value) {
    if(!embedded_health||value<=0)return;
    if(water_active)air_heal_pending+=value*256.f;
    embedded_health(-10.f*value,0,0);m->healCounter=0;
    fprintf(stderr,"SMB COIN HEAL value=%d removed=%.1f\n",value,10.f*value);
}

void smb_host_camera_buttons(struct Camera* camera) {
    (void)camera;

}

int smb_host_camera_r(unsigned buttons){return embedded_step?0:(buttons&R_TRIG);}
