/* Host traversal runs inside the native lane-local physics callback. Damage,
 * attacks and specials still use their real Melee state machines. */
#include <melee/ft/fighter.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/ftmetal.h>
#include <melee/ft/kinds/ftCommon/ftCo_Jump.h>
#include <melee/ft/kinds/ftKirby/forward.h>
static struct {HSD_GObj* object;unsigned active,ticks;float horizontal,vertical;} cannon_flights[2];
static unsigned hosted_poses[2];
#ifndef SMB_ENV_METAL
#define SMB_ENV_METAL 4096u
#endif
#ifndef SMB_ENV_CAP_MANAGED
#define SMB_ENV_CAP_MANAGED 8192u
#endif
#ifndef SMB_SWIM_UP
#define SMB_SWIM_UP 0x10000u
#define SMB_SWIM_DOWN 0x20000u
#endif
extern int smb_native_remote_controlled(Fighter*);
static struct {HSD_GObj* object;unsigned flags;float water_level;} actor_environment[GM_MAX_PLAYERS][2];
static struct {HSD_GObj* object;unsigned wet,escaping,driving,cutter_active;float horizontal,cutter_frame,cutter_elapsed;} swim_state[GM_MAX_PLAYERS][2];
int smb_embed_actor_environment(unsigned seat,unsigned sub,unsigned flags,float water_level) {
    if(seat>=19||sub>1||(flags&~(SMB_ENV_WATER|SMB_ENV_METAL|SMB_ENV_CAP_MANAGED))||!isfinite(water_level)||fabsf(water_level)>10000)return -1;
    HSD_GObj* object=Player_GetEntityAtIndex(seat,sub);
    if(!object){memset(&actor_environment[seat][sub],0,sizeof actor_environment[seat][sub]);return -2;}
    actor_environment[seat][sub].object=object;actor_environment[seat][sub].flags=flags;
    actor_environment[seat][sub].water_level=water_level;return 0;
}
static int smb_native_actor_water(Fighter* fp,float* level) {
    if(fp->player_idx>=19||fp->is_sleeping||smb_native_remote_controlled(fp))return 0;
    unsigned seat=fp->player_idx,sub=fp->is_sub_fighter?1:0;
    if(actor_environment[seat][sub].object==fp->gobj) {
        *level=actor_environment[seat][sub].water_level;
        return (actor_environment[seat][sub].flags&SMB_ENV_WATER)!=0;
    }
    /* The primary's existing command remains valid for older hosts. Nana
     * shares the surface, but her own position decides whether she is wet. */
    if(!seat&&(host_environment.flags&SMB_ENV_WATER)) {
        *level=host_environment.water_level;
        return !sub||fp->cur_pos.y<*level-7;
    }
    return 0;
}
static float smb_native_water_stick(Fighter* fp,float* along,int* depth) {
    unsigned buttons=host_environment.buttons;
    *depth=!fp->player_idx?(!!(buttons&SMB_SWIM_UP)-!!(buttons&SMB_SWIM_DOWN)):
        fp->input.lstick[0].y>.3f?1:fp->input.lstick[0].y<-.3f?-1:0;
    if(!fp->player_idx&&!fp->is_sub_fighter) {
        float yaw=host_environment.camera_yaw,c=cosf(yaw),s=sinf(yaw),lx,lz;
        float dx=host_environment.stick_x*c-host_environment.stick_y*s;
        float dz=-host_environment.stick_x*s-host_environment.stick_y*c;
        m3d_GetLane(fp,&lx,&lz);*along=(dx*lx+dz*lz)*.8f;
        return fminf(1,hypotf(host_environment.stick_x,host_environment.stick_y));
    }
    /* CPU and partner controls have already been projected into this
     * fighter's lane by the native 3D input path. */
    *along=fp->input.lstick[0].x*.8f;
    return fminf(1,fabsf(fp->input.lstick[0].x));
}
static void smb_native_swim_resources(Fighter* fp) {
    /* The resource portion of a landing, without grounding against a floor
     * that does not exist at the water surface. */
    fp->x1968_jumpsUsed=1;fp->x1969_walljumpUsed=0;fp->x2227_b0=false;fp->fall_fast=false;
}
static int smb_native_swim_prepare(Fighter* fp,float* level) {
    if(fp->player_idx>=19)return 0;
    unsigned seat=fp->player_idx,sub=fp->is_sub_fighter?1:0;
    __typeof__(swim_state[0][0])* state=&swim_state[seat][sub];
    if(state->object!=fp->gobj){memset(state,0,sizeof *state);state->object=fp->gobj;}
    int wet=host_world&&smb_native_actor_water(fp,level)&&!fp->is_metal&&
        (seat||!(host_environment.flags&SMB_ENV_HOSTED));
    if(!wet){state->wet=state->escaping=state->driving=state->cutter_active=0;return 0;}
    if(!state->wet){smb_native_swim_resources(fp);state->wet=1;}
    return 1;
}
static int smb_native_swim_neutral(Fighter* fp) {
    return fp->motion_id>=ftCo_MS_Wait&&fp->motion_id<=ftCo_MS_FallSpecialB&&
        !fp->stamina_dead&&!fp->x221C_b6&&fp->dmg.x195c_hitlag_frames<=0&&
        !fp->input_disabled&&!fp->victim_gobj&&!fp->x1A5C;
}
int smb_native_water_before_input(Fighter* fp) {
    float level;if(!smb_native_swim_prepare(fp,&level))return 0;
    /* Final Cutter's downward hold has no animation/IASA exit; retail waits
     * for a real floor. Water instead plays its existing aerial end after
     * the authored downward animation finishes, preserving the full ascent
     * and dive before restoring control through the native end callback. */
    __typeof__(swim_state[0][0])* state=&swim_state[fp->player_idx][fp->is_sub_fighter?1:0];
    if(fp->kind==Ft_Kind_Kirby&&fp->motion_id==ftKb_MS_SpecialAirHi3&&
       fp->ground_or_air==GA_Air&&!fp->stamina_dead&&!fp->x221C_b6&&
       fp->dmg.x195c_hitlag_frames<=0&&!fp->input_disabled&&!fp->victim_gobj&&!fp->x1A5C) {
        float frame=fp->cur_anim_frame,end=ftAnim_8006F484(fp->gobj);
        int remaining=ftAnim_IsFramesRemaining(fp->gobj),complete=!remaining;
        if(!state->cutter_active){state->cutter_active=1;state->cutter_elapsed=0;}
        else {
            state->cutter_elapsed+=fabsf(fp->frame_speed_mul);
            /* The native downward hold can loop a one-frame clip forever.
             * Honor one authored cycle, even if its sampled frame stays1. */
            if(frame+.25f<state->cutter_frame||
               (isfinite(end)&&end>0&&end<=20000&&state->cutter_elapsed>=fmaxf(1,end)))complete=1;
        }
        state->cutter_frame=frame;
        static unsigned traces;
        if(getenv("SMB_SCENARIO")&&traces++<12)fprintf(stderr,"SMB WATER CUTTER frame=%.3f end=%.3f elapsed=%.3f remaining=%d complete=%d\n",frame,end,state->cutter_elapsed,remaining,complete);
        if(complete) {
            Fighter_ChangeMotionState(fp->gobj,ftKb_MS_SpecialAirHi4,Ft_MF_None,0,1,0,NULL);
            fp->self_vel.y=fp->x74_self_accel.y=0;ftPartSetRotX(fp,0,0);state->cutter_active=0;
            if(getenv("SMB_SCENARIO"))fprintf(stderr,"SMB WATER RECOVERY seat=%u sub=%u native_end=kirby_cutter\n",fp->player_idx,fp->is_sub_fighter?1:0);
        }
    } else state->cutter_active=0;
    if(!smb_native_swim_neutral(fp))return 0;
    if(fp->motion_id>=ftCo_MS_FallSpecial&&fp->motion_id<=ftCo_MS_FallSpecialB) {
        /* Helpless fall is the completed recovery, rather than an authored
         * special still in progress. Touching water restores control. */
        ftCo_Fall_Enter(fp->gobj);smb_native_swim_resources(fp);
        if(getenv("SMB_SCENARIO"))fprintf(stderr,"SMB WATER RECOVERY seat=%u sub=%u native_fall=1\n",fp->player_idx,fp->is_sub_fighter?1:0);
    }
    if(fp->input.pressed_buttons&HSD_PAD_XY) {
        smb_native_swim_resources(fp);
        if(fp->cur_pos.y>=level-14) {
            ftCo_Jump_Enter(fp->gobj);fp->self_vel.y=3.1f;
            swim_state[fp->player_idx][fp->is_sub_fighter?1:0].escaping=1;
            if(getenv("SMB_SCENARIO"))fprintf(stderr,"SMB WATER JUMP seat=%u sub=%u y=%.3f surface=%.3f vy=%.3f\n",fp->player_idx,fp->is_sub_fighter?1:0,fp->cur_pos.y,level,fp->self_vel.y);
            return 1;
        }
    }
    return 0;
}
static void smb_native_actor_environments(void) {
    for(unsigned seat=0;seat<19;seat++)for(unsigned sub=0;sub<2;sub++) {
        HSD_GObj* object=Player_GetEntityAtIndex(seat,sub);if(!object)continue;
        Fighter* fp=object->user_data;
        if(fp->is_sleeping||smb_native_remote_controlled(fp)) {
            /* A later ownership/rejoin starts from the newly authored pose,
             * rather than an old local swim impulse cached before the lease. */
            memset(&swim_state[seat][sub],0,sizeof swim_state[seat][sub]);continue;
        }
        if(actor_environment[seat][sub].object==object&&(actor_environment[seat][sub].flags&SMB_ENV_CAP_MANAGED)) {
            int metal=(actor_environment[seat][sub].flags&SMB_ENV_METAL)!=0;
            if(metal)ftCo_800C8348(object,4,1000000);
            else if(fp->is_metal)ftCo_800C8540(object);
        }
        float level,along;int depth;
        if(!smb_native_swim_prepare(fp,&level))continue;
        smb_native_water_stick(fp,&along,&depth);
        if(fp->ground_or_air==GA_Ground&&depth>0&&smb_native_swim_neutral(fp)) {
            ftCommon_8007D5D4(fp);ftCo_Fall_Enter(object);fp->self_vel.y=0;
        }
    }
}
static void smb_native_cannon_start(Fighter* fp,float horizontal,float vertical) {
#define cannon_flight cannon_flights[fp->is_sub_fighter?1:0]
    cannon_flight.object=fp->gobj;cannon_flight.active=1;cannon_flight.ticks=0;
    cannon_flight.horizontal=horizontal;cannon_flight.vertical=vertical;
}
int smb_native_travel_physics(Fighter* fp) {
    if(!host_world||fp->player_idx!=0)return 0;
    if(host_environment.flags&SMB_ENV_HOSTED) {
        fp->self_vel=fp->x8c_kb_vel=fp->xD4_unk_vel=(Vec3){0};fp->gr_vel=0;return 1;
    }
    if(cannon_flight.active&&cannon_flight.object==fp->gobj) {
        if(fp->ground_or_air==GA_Ground || (fp->coll_data.env_flags&(Collide_WallMask|Collide_CeilingHug)) ||
           !(fp->motion_id==ftCo_MS_DamageFall||(fp->motion_id>=ftCo_MS_DamageFlyHi&&fp->motion_id<=ftCo_MS_DamageFlyRoll)||
             (fp->motion_id>=ftCo_MS_Fall&&fp->motion_id<=ftCo_MS_FallB))) {
            cannon_flight.active=0;
        } else {
            /* SM64's shot loses .05 forward units / host frame, with gravity
             * 4 and terminal fall 75. Half-sized 60 Hz velocities preserve
             * that arc; tumble, collision and player interruptions are Melee. */
            unsigned t=cannon_flight.ticks++;
            if(t%2==0)cannon_flight.horizontal=fmaxf(.5f,cannon_flight.horizontal-.0025f);
            fp->self_vel=(Vec3){cannon_flight.horizontal,cannon_flight.vertical,0};
            fp->x8c_kb_vel=(Vec3){0};fp->fall_fast=false;
            if(t%2==1)cannon_flight.vertical=fmaxf(-3.75f,cannon_flight.vertical-.2f);
            if(getenv("SMB_SCENARIO")&&t%30==0)fprintf(stderr,"SMB CANNON ARC tick=%u vx=%.4f vy=%.4f\n",t,fp->self_vel.x,fp->self_vel.y);
            return 1;
        }
    }
    return smb_native_wall_physics(fp);
}
/* Apply buoyancy after the authored callback, before velocity integration.
 * No terminal-speed clamp is applied to specials: their launch stays useful. */
void smb_native_water_physics(Fighter* fp) {
    float level;
    if(!host_world||!smb_native_actor_water(fp,&level)||fp->is_metal||
       (!fp->player_idx&&(host_environment.flags&SMB_ENV_HOSTED)))return;
    float along;int depth;float mag=smb_native_water_stick(fp,&along,&depth);
    int neutral=smb_native_swim_neutral(fp);
    unsigned seat=fp->player_idx,sub=fp->is_sub_fighter?1:0;
    int jump_motion=fp->motion_id>=ftCo_MS_JumpF&&fp->motion_id<=ftCo_MS_JumpAerialB;
    if(swim_state[seat][sub].escaping&&(!jump_motion||fp->self_vel.y<=0))swim_state[seat][sub].escaping=0;
    int rising_jump=jump_motion&&fp->self_vel.y>.8f;
    if(fp->ground_or_air==GA_Air&&neutral&&!rising_jump&&!swim_state[seat][sub].escaping) {
        /* Ordinary travel holds depth; L1/L2 explicitly rise or dive. Idle
         * sinking and actual native jump impulses remain separate controls. */
        int depth_held=depth||(!seat&&(host_environment.buttons&(SMB_SWIM_UP|SMB_SWIM_DOWN)));
        float target=depth?depth*.8f:(mag>.05f||depth_held)?0:-.5f;
        fp->self_vel.y+=fp->co_attrs.gravity; /* Cancel native gravity before buoyancy. */
        fp->self_vel.y+=(target-fp->self_vel.y)*.12f;
        fp->self_vel.y=fmaxf(-.9f,fminf(.9f,fp->self_vel.y));
        /* Ordinary native air acceleration has already run. Keep water's
         * own momentum so that callback cannot add another character-specific
         * acceleration on top of Mario's baseline swimming speed. */
        if(!swim_state[seat][sub].driving)swim_state[seat][sub].horizontal=fp->self_vel.x;
        swim_state[seat][sub].horizontal+=(along-swim_state[seat][sub].horizontal)*.12f;
        fp->self_vel.x=swim_state[seat][sub].horizontal;swim_state[seat][sub].driving=1;
        fp->x74_self_accel.x=0;
        fp->fall_fast=false;
        if(fp->cur_pos.y>=level-4&&fp->self_vel.y>0)fp->self_vel.y=0;
    } else if(fp->ground_or_air==GA_Air&&!rising_jump&&!swim_state[seat][sub].escaping) {
        swim_state[seat][sub].driving=0;
        /* Specials own their vertical physics. Some hold a constant dive
         * rather than applying gravity, so generic gravity compensation
         * would turn that dive into an unbounded upward acceleration. */
        fp->self_vel.x*=.994f;fp->self_vel.y*=.994f;
    } else if(fp->ground_or_air==GA_Ground&&neutral) {
        if(!swim_state[seat][sub].driving)swim_state[seat][sub].horizontal=fp->gr_vel;
        swim_state[seat][sub].horizontal+=(along-swim_state[seat][sub].horizontal)*.12f;
        fp->gr_vel=swim_state[seat][sub].horizontal;swim_state[seat][sub].driving=1;
        fp->xE4_ground_accel_1=fp->xE8_ground_accel_2=0;fp->x74_self_accel.x=fp->x74_self_accel.y=0;
        fp->self_vel.x=fp->coll_data.floor.normal.y*fp->gr_vel;fp->self_vel.y=-fp->coll_data.floor.normal.x*fp->gr_vel;
    } else swim_state[seat][sub].driving=0;
}
static void smb_native_hosted_pose(Fighter* fp,unsigned flags) {
    unsigned pose=flags&SMB_ENV_HOSTED;
#define hosted_pose hosted_poses[fp->is_sub_fighter?1:0]
    unsigned previous=hosted_pose;
    hosted_pose=pose;
    if(pose) {
        cannon_flight.active=0;
        int motion=pose&SMB_ENV_WING?ftCo_MS_DamageFlyTop:ftCo_MS_SquatWait;
        if(previous!=pose||fp->motion_id!=motion) {
            if(pose&SMB_ENV_WING)ftCommon_8007D5D4(fp);
            Fighter_ChangeMotionState(fp->gobj,motion,0,4,0,0,NULL);
            ftAnim_8006EBA4(fp->gobj);
            fprintf(stderr,"SMB HOSTED POSE mode=%s kind=%d motion=%d\n",pose&SMB_ENV_WING?"wing":"shell",fp->kind,fp->motion_id);
        }
        /* Host travel owns input and collision. Keep the chosen retail pose
         * from returning to walk/fall when its own IASA sees SM64 controls. */
        if(pose&SMB_ENV_WING)ftPartSetRotX(fp,ftParts_GetBoneIndex(fp,FtPart_XRotN),1.5707963268f);
        fp->input_cb=NULL;fp->anim_cb=NULL;fp->coll_cb=NULL;
    } else if(previous) {ftCommon_8007D5D4(fp);ftCo_Fall_Enter(fp->gobj);}
}
