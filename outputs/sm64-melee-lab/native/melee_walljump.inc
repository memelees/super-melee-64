#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysdolphin/baselib/controller.h>
#include <melee/ft/kinds/ftCommon/ftCo_Jump.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/ftcommon.h>
static struct {HSD_GObj* object;unsigned active,ticks;float horizontal,vertical;} smb_wall_flight[2];
extern int smb_embed_active(void);
int smb_native_wall_flight(Fighter* fp) {
    unsigned sub=fp->is_sub_fighter?1:0;
    return fp->player_idx==0&&smb_wall_flight[sub].object==fp->gobj&&smb_wall_flight[sub].active;
}
/* Called in the same lane-local frame as every normal native physics callback.
 * SM64 is 30 Hz; holding each velocity for two native ticks reproduces its
 * 62-up / 24-out wall kick and 4/frame gravity, independently of fighter weight. */
int smb_native_wall_physics(Fighter* fp) {
    if(!smb_embed_active()||!smb_native_wall_flight(fp))return 0;
    unsigned sub=fp->is_sub_fighter?1:0;
    if(fp->ground_or_air==GA_Ground||!((fp->motion_id==ftCo_MS_JumpF||fp->motion_id==ftCo_MS_JumpB)||(fp->motion_id>=ftCo_MS_Fall&&fp->motion_id<=ftCo_MS_FallB))) {
        smb_wall_flight[sub].active=0;return 0;
    }
    float* horizontal=&smb_wall_flight[sub].horizontal;
    float* vertical=&smb_wall_flight[sub].vertical;
    unsigned t=smb_wall_flight[sub].ticks++;
    if(t%2==0)*horizontal=fmaxf(0,*horizontal-.0175f);
    fp->self_vel=(Vec3){*horizontal,*vertical,0};fp->fall_fast=false;
    if(t%2==1) {
        *vertical=fmaxf(-3.75f,*vertical-.2f);
        /* Stock SM64 neutral air drag: 0.35 units per 30 Hz frame. Melee
         * still supplies camera-relative heading and interruptible moves. */
    }
    if(getenv("SMB_SCENARIO")&&t<12)fprintf(stderr,"SMB WALL TRAJECTORY kind=%d tick=%u vx=%.4f vy=%.4f motion=%d\n",fp->kind,t,fp->self_vel.x,fp->self_vel.y,fp->motion_id);
    return 1;
}
/* Wall contact and approach speed use the same lane as native collision. */
bool ftWallJump_8008169C(Fighter_GObj* object) {
    Fighter* fp=GET_FIGHTER(object);float lx,lz;
    bool native=m3d_GetLane(fp,&lx,&lz);
    unsigned wall=fp->coll_data.env_flags&Collide_WallMask;
    if((getenv("SMB_UNIVERSAL_WALL_JUMP")&&atoi(getenv("SMB_UNIVERSAL_WALL_JUMP")))&&smb_embed_active()&&fp->player_idx==0&&native&&wall&&fp->ground_or_air==GA_Air) {
        float away=(wall&Collide_RightWallMask)?1.f:-1.f;
        HSD_PadStatus* pad=&HSD_PadGameStatus[fp->pad_port];
        int jump=(pad->trigger&(HSD_PAD_X|HSD_PAD_Y))!=0;
        int flick=fp->input.lstick[0].x*away>.7f&&fp->active_timer.lstick.x<4;
        unsigned sub=fp->is_sub_fighter?1:0;
        if((jump||flick)&&(!smb_native_wall_flight(fp)||smb_wall_flight[sub].ticks>8)) {
            int local=m3d_MapIsActive(fp);m3d_ExitLocal(fp);
            m3d_SetLane(fp,lx*away,lz*away);fp->facing_dir=1;
            float prior=fp->input.lstick[0].x;fp->input.lstick[0].x=0;
            ftCo_Jump_Enter(object);fp->input.lstick[0].x=prior;
            fp->self_vel=(Vec3){1.2f,3.1f,0};fp->x8c_kb_vel=(Vec3){0};fp->fall_fast=false;
            fp->cur_pos.x+=lx*away;fp->cur_pos.z+=lz*away;
            fp->prev_pos=fp->coll_data.cur_pos=fp->coll_data.prev_pos=fp->coll_data.last_pos=fp->cur_pos;
            fp->coll_data.env_flags&=~(Collide_RightWallHug|Collide_LeftWallHug);
            smb_wall_flight[sub].object=object;smb_wall_flight[sub].active=1;smb_wall_flight[sub].ticks=0;
            smb_wall_flight[sub].horizontal=1.2f;smb_wall_flight[sub].vertical=3.1f;
            if(local)m3d_EnterLocal(fp);
            fprintf(stderr,"SMB WALL KICK kind=%d animation=%d away=%.3f,%.3f sm64_vy=62 sm64_speed=24 all_characters=1\n",fp->kind,fp->motion_id,lx*away,lz*away);
            return true;
        }
        return false;
    }
    Vec3 delta=fp->pos_delta;
    bool began=native&&m3d_LocalBegin(fp);
    if(native){fp->pos_delta.x=delta.x*lx+delta.z*lz;fp->pos_delta.z=0;}
    bool result=ftWallJump_8008169C_2D(object);
    if(native)fp->pos_delta=delta;
    m3d_LocalEnd(fp,began);
    return result;
}

int smb_native_wall_before_input(Fighter* fp) {
    if(!getenv("SMB_UNIVERSAL_WALL_JUMP")||!atoi(getenv("SMB_UNIVERSAL_WALL_JUMP")))return 0;
    if(!smb_embed_active()||fp->player_idx!=0||fp->ground_or_air!=GA_Air||fp->motion_id<ftCo_MS_JumpF||fp->motion_id>ftCo_MS_FallSpecialB)return 0;
    if(getenv("SMB_SCENARIO")&&(!strcmp(getenv("SMB_SCENARIO"),"walls")||!strcmp(getenv("SMB_SCENARIO"),"walls-all"))) {
        static unsigned count;if(count++<60)fprintf(stderr,"SMB WALL CONTACT kind=%d env=%x motion=%d pressed=%x stick=%.2f\n",fp->kind,fp->coll_data.env_flags,fp->motion_id,fp->input.pressed_buttons,fp->input.lstick[0].x);
    }
    if(!(fp->coll_data.env_flags&Collide_WallMask))return 0;
    return ftWallJump_8008169C(fp->gobj);
}

void smb_native_wall_reset(Fighter* fp){if(fp->player_idx==0)smb_wall_flight[fp->is_sub_fighter?1:0].active=0;}
