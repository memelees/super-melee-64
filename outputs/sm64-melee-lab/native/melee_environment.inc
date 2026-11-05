/* GCC-compiled world/attribute boundary for host slopes and native respawns. */
#include <melee/ft/ft_0D4D.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/kinds/ftCommon/ftCo_SquatWait.h>
#include <melee/ft/ftlib.h>
#include <melee/ft/ft_0881.h>
#include <melee/ft/kinds/ftCommon/ftCo_DamageFall.h>
#include <melee/ef/efsync.h>
void smb_native_trails(void) {
    static unsigned tick;
    if(++tick%4)return;
    for(unsigned seat=0;seat<GM_MAX_PLAYERS;seat++)for(unsigned sub=0;sub<2;sub++) {
        HSD_GObj* g=Player_GetEntityAtIndex(seat,sub);if(!g)continue;
        Fighter* fp=g->user_data;
        if(fp->is_sleeping||fp->ground_or_air==GA_Ground)continue;
        float kb=hypotf(fp->x8c_kb_vel.x,fp->x8c_kb_vel.y);
        int tumble=fp->motion_id==ftCo_MS_DamageFall||(fp->motion_id>=ftCo_MS_DamageFlyHi&&fp->motion_id<=ftCo_MS_DamageFlyRoll);
        if(!tumble||fmaxf(kb,hypotf(fp->self_vel.x,fp->self_vel.y))<1.5f)continue;
        Vec3 point=fp->cur_pos;point.y+=8;
        float angle=atan2f(-(fp->self_vel.x+fp->x8c_kb_vel.x),fp->self_vel.y+fp->x8c_kb_vel.y);
        /* Retail DamageFly's smoke generator, detached in world space so
         * the smoke stays behind when the fighter changes lane. */
        void* effect=efSync_Spawn(0x408,NULL,&point,&angle);
        if(getenv("SMB_SCENARIO")&&effect)fprintf(stderr,"SMB FLIGHT TRAIL seat=%u native=408\n",seat);
    }
}
void smb_native_damage_voice(Fighter* fp) {
    /* Disc tables must be read in this GCC scalar-byte-order boundary. */
    FtSFX* sounds=DP(FtSFX,fp->ft_data->x4C_sfx);
    FtSFXArr* voice=fp->dmg.x190C;
    if(!voice&&sounds)voice=DP(FtSFXArr,sounds->x1C);
    int count=voice?voice->num:0;
    if(count>0&&DP(DiscS32,voice->sfx_ids))ft_800889F4(fp,voice);
    fp->dmg.x190C=NULL;
    fprintf(stderr,"SMB DAMAGE VOICE kind=%d samples=%d channel=%d\n",fp->kind,count,fp->x2144);
}
static void smb_native_cannon_start(Fighter*,float,float);
void smb_native_begin_launch(HSD_GObj* object,const float velocity[3],int cannon) {
    Fighter* fp=object->user_data;
    float speed=hypotf(velocity[0],velocity[2]);
    if(speed>.001f)m3d_SetLane(fp,velocity[0]/speed,velocity[2]/speed);
    if(fp->ground_or_air==GA_Ground)ftCommon_8007D5D4(fp);
    ftCo_80090780(object); /* Actual Melee tumble, with aerial recovery inputs. */
    fp->self_vel=(Vec3){0};fp->xD4_unk_vel=(Vec3){0};fp->gr_vel=0;
    /* A cannon supplies travel velocity rather than damage knockback. Native
     * gravity/air friction and real collisions still apply, without decaying
     * the shot as though it were a weak attack. */
    if(cannon){smb_native_cannon_start(fp,speed,velocity[1]);fp->self_vel=(Vec3){speed,velocity[1],0};fp->x8c_kb_vel=(Vec3){0};}
    else fp->x8c_kb_vel=(Vec3){speed,velocity[1],0};
    fp->xF0_ground_kb_vel=0;
    fp->dmg.x1908=-1;fp->dmg.x190C=NULL;
    if(!cannon)smb_native_damage_voice(fp);
    fprintf(stderr,"SMB NATIVE LAUNCH source=%s motion=%d speed=%.4f vy=%.4f percent=%.2f\n",cannon?"cannon":"enemy",fp->motion_id,speed,velocity[1],fp->dmg.x1830_percent);
}
static SmbInput host_environment;
static Vec3 host_tick_start,host_respawn_landing;
static float host_respawn_headroom;
extern void smb_native_wall_reset(Fighter*);
extern int smb_native_wall_physics(Fighter*);
#include "melee_traversal.inc"
void smb_native_environment(const SmbInput* command,Fighter* fp) {
    static int arrow_interrupted;
    if(!arrow_interrupted&&(fp->kind==Ft_Kind_Link||fp->kind==Ft_Kind_CLink)&&fp->u.lk.arrow_gobj&&getenv("SMB_SMOKE")&&getenv("SMB_SCENARIO")&&!strcmp(getenv("SMB_SCENARIO"),"projectiles")) {
        arrow_interrupted=1;
        extern void smb_native_reset_world_items(void);smb_native_reset_world_items();
        fprintf(stderr,"SMB PROJECTILE WORLD RESET held_bow_interruption=1\n");
    }
    if(command->flags&1)smb_native_wall_reset(fp);
    host_environment=*command;host_tick_start=fp->cur_pos;
    smb_native_hosted_pose(fp,command->flags);
    smb_native_actor_environments();
    if((command->flags&32)&&fp->ground_or_air==GA_Ground) {
        float speed=hypotf(command->slope_velocity[0],command->slope_velocity[2]);
        if(speed>.01f){m3d_SetLane(fp,command->slope_velocity[0]/speed,command->slope_velocity[2]/speed);fp->facing_dir=1;}
    }
    /* The host supplies downhill travel; neutral locomotion contributes no
     * extra walking. Use the fighter's real crouch, never a draining guard. */
    if((command->flags&32)&&!(command->buttons&0x400)&&fp->ground_or_air==GA_Ground&&
       ((fp->motion_id>=14&&fp->motion_id<24)||(fp->motion_id>=39&&fp->motion_id<=43))) {
        if(fp->motion_id!=ftCo_MS_SquatWait)ftCo_800D638C(fp->gobj);
        fp->self_vel.x=fp->self_vel.z=fp->gr_vel=0;
        if(getenv("SMB_SCENARIO"))fprintf(stderr,"SMB SLIDE POSE seq=%u motion=%d shield=%d\n",command->sequence,fp->motion_id,ftLib_IsShielding(fp->gobj));
    }
}
void smb_native_slide_velocity(Fighter* fp) {
    if(!host_world||!(host_environment.flags&32)||fp->gobj!=Player_GetEntity(0)||fp->ground_or_air!=GA_Ground)return;
    int edge=fp->motion_id==245||fp->motion_id==246;
    if(!edge&&(fp->motion_id<14||fp->motion_id>=87))return;
    /* The normal physics callback borrows a lane-local collision frame.
     * Convert the replacement travel back into that same frame. */
    Vec3 target=fp->cur_pos;int local=m3d_MapIsActive(fp);
    if(local)m3d_MapToWorld(&target);
    target.x=host_tick_start.x+host_environment.slope_velocity[0];
    target.z=host_tick_start.z+host_environment.slope_velocity[2];
    if(local)m3d_MapToLocal(&target);
    fp->cur_pos=target;
    if(edge) {
        float lx,lz;m3d_GetLane(fp,&lx,&lz);
        float along=host_environment.slope_velocity[0]*lx+host_environment.slope_velocity[2]*lz;
        ftCommon_8007D5D4(fp);ftCo_Fall_Enter(fp->gobj);
        fp->self_vel.x=along;fp->self_vel.y=0;
        fprintf(stderr,"SMB SLIDE EDGE speed=%.4f native_fall=1\n",along);
    }
}
static void smb_native_rebirth_position(Fighter* fp) {
    m3d_ExitLocal(fp);
    float room=fmaxf(0,fminf(80,host_respawn_headroom-30));
    Vec3 target=host_respawn_landing;target.y+=fminf(12,room*.3f);
    m3d_SetLane(fp,1,0);
    Player_SetSpawnPlatformPos(fp->player_idx,&target);
    Player_80032FA4(fp->player_idx,-1);fp->smash_attrs.x2135=-1;
    fp->mv.co.common.x4=target;fp->cur_pos=target;
    fp->cur_pos.y+=fminf(40,room*.7f);fp->prev_pos=fp->cur_pos;
    fp->self_vel=(Vec3){0};fp->xD4_unk_vel=(Vec3){0};
    mpColl_80043680(&fp->coll_data,&fp->cur_pos);
    HSD_JObjSetTranslate(fp->gobj->hsd_obj,&fp->cur_pos);
    fprintf(stderr,"SMB RESPAWN PLATFORM slot=%u sub=%u landing_y=%.3f target_y=%.3f start_y=%.3f accessory=%d\n",fp->player_idx,fp->is_sub_fighter,host_respawn_landing.y,target.y,fp->cur_pos.y,fp->x20A0_accessory!=NULL);
}
void smb_native_begin_respawn(HSD_GObj* object,float headroom) {
    Fighter* fp=object->user_data;host_respawn_landing=fp->cur_pos;
    host_respawn_headroom=headroom;
    ftCo_800D4FF4(object);
}
