/* A network grab temporarily borrows the captured actor on the catcher's
 * machine. Melee then runs its real paired callbacks, joint attachment,
 * pummels, escape timer and authored throw. No native pointer crosses the wire. */
#include <melee/ft/kinds/ftCommon/ftCo_Attack100.h>
#include <melee/ft/kinds/ftCommon/ftCo_CaptureCut.h>
#include <melee/ft/kinds/ftCommon/ftCo_Throw.h>
#include <melee/ft/kinds/ftCommon/ftCo_Damage.h>
#include <sysdolphin/baselib/controller.h>
extern int smb_native_constrain(Fighter*,const Vec3*);
enum {SMB_GRAB_ACTIVE=1,SMB_GRAB_THROWN=2,SMB_GRAB_ESCAPED=3,SMB_GRAB_ABORTED=4};
typedef struct {
    unsigned id,simulating,presenting,restored,at,catcher,catcher_sub,victim,victim_sub;
    HSD_GObj* catcher_object;HSD_GObj* victim_object;
    unsigned catcher_spawn,victim_spawn,buttons,previous_buttons;
    float stick[2];Vec3 origin;unsigned origin_valid;
    unsigned char hurt_state[15],grabbable[15],hurt_saved;unsigned invulnerable,team;
    double damage_begin;float damage_imported;
    SmbGrabState state;
} SmbNativeGrab;
static SmbNativeGrab smb_grabs[32];
static unsigned smb_grab_clock,smb_grab_raw_buttons;static float smb_grab_raw_stick[2];
static SmbNativeGrab* smb_grab_find(unsigned id) {
    if(!id)return NULL;
    for(unsigned i=0;i<32;i++)if(smb_grabs[i].id==id)return &smb_grabs[i];return NULL;
}
static HSD_GObj* smb_grab_actor(unsigned seat,unsigned sub) {
    return seat<19&&sub<2?Player_GetEntityAtIndex(seat,sub):NULL;
}
static Fighter* smb_grab_victim(SmbNativeGrab* lease) {
    HSD_GObj* object=smb_grab_actor(lease->victim,lease->victim_sub);
    if(!object||object!=lease->victim_object)return NULL;
    Fighter* fp=object->user_data;return fp&&fp->x8_spawnNum==lease->victim_spawn?fp:NULL;
}
static Fighter* smb_grab_catcher(SmbNativeGrab* lease) {
    HSD_GObj* object=smb_grab_actor(lease->catcher,lease->catcher_sub);
    if(!object||object!=lease->catcher_object)return NULL;
    Fighter* fp=object->user_data;return fp&&fp->x8_spawnNum==lease->catcher_spawn?fp:NULL;
}
int smb_native_grab_remote_controlled(Fighter* fp) {
    if(!fp)return 0;
    for(unsigned i=0;i<32;i++)if(smb_grabs[i].id&&smb_grabs[i].presenting&&!smb_grabs[i].restored&&
        smb_grabs[i].victim==fp->player_idx&&smb_grabs[i].victim_sub==(unsigned)fp->is_sub_fighter)return 1;
    return 0;
}
int smb_native_grab_borrowed(unsigned seat,unsigned sub) {
    for(unsigned i=0;i<32;i++)if(smb_grabs[i].id&&smb_grabs[i].simulating&&smb_grabs[i].victim==seat&&smb_grabs[i].victim_sub==sub)return 1;
    return 0;
}
static SmbNativeGrab* smb_grab_allocate(unsigned id,unsigned seat,unsigned sub) {
    if(!id||seat>=19||sub>1)return NULL;
    SmbNativeGrab* existing=smb_grab_find(id);if(existing)return existing;
    SmbNativeGrab* empty=NULL;
    for(unsigned i=0;i<32;i++) {
        if(smb_grabs[i].id&&smb_grabs[i].victim==seat&&smb_grabs[i].victim_sub==sub)return NULL;
        if(!smb_grabs[i].id)empty=&smb_grabs[i];
    }
    if(!empty)return NULL;
    memset(empty,0,sizeof *empty);empty->id=id;empty->victim=seat;empty->victim_sub=sub;empty->at=smb_grab_clock;
    return empty;
}
static void smb_grab_save_hurt(SmbNativeGrab* lease,Fighter* fp) {
    if(lease->hurt_saved)return;
    lease->invulnerable=fp->x221A_b5;lease->team=fp->team;
    for(unsigned h=0;h<fp->hurt_capsules_len&&h<15;h++) {
        lease->hurt_state[h]=fp->hurt_capsules[h].capsule.state;
        lease->grabbable[h]=fp->hurt_capsules[h].is_grabbable;
        /* A borrowed presentation proxy must regain its original hurt state. */
        if(fp->player_idx&&remote_hurt_saved[fp->player_idx][fp->is_sub_fighter]) {
            lease->hurt_state[h]=HurtCapsule_Enabled;
            lease->grabbable[h]=remote_grabbable[fp->player_idx][fp->is_sub_fighter][h];
        }
    }
    lease->hurt_saved=1;
}
static void smb_grab_restore_hurt(SmbNativeGrab* lease,Fighter* fp) {
    for(unsigned h=0;h<fp->hurt_capsules_len&&h<15;h++) {
        fp->hurt_capsules[h].capsule.state=lease->hurt_state[h];fp->hurt_capsules[h].is_grabbable=lease->grabbable[h];
    }
    fp->x221A_b5=lease->invulnerable;
}
static void smb_grab_vectors(Fighter* fp,float self[3],float knockback[3]) {
    float lx=1,lz=0;m3d_GetLane(fp,&lx,&lz);
    self[0]=fp->self_vel.x*lx;self[1]=fp->self_vel.y;self[2]=fp->self_vel.x*lz+fp->self_vel.z;
    knockback[0]=fp->x8c_kb_vel.x*lx;knockback[1]=fp->x8c_kb_vel.y;knockback[2]=fp->x8c_kb_vel.x*lz+fp->x8c_kb_vel.z;
}
static void smb_grab_snapshot(SmbNativeGrab* lease) {
    SmbGrabState* out=&lease->state;out->version=SMB_MAGIC;out->id=lease->id;
    out->catcher=lease->catcher;out->catcher_sub=lease->catcher_sub;out->victim=lease->victim;out->victim_sub=lease->victim_sub;
    Fighter* catcher=smb_grab_catcher(lease);Fighter* victim=smb_grab_victim(lease);
    if(catcher)smb_native_remote_state(catcher,&out->catcher_state);
    if(victim) {
        out->damage_total=(float)fmax(0,smb_native_damage_total(victim)-lease->damage_begin);
        smb_native_remote_state(victim,&out->victim_state);smb_grab_vectors(victim,out->self_velocity,out->knockback_velocity);
        int damaged=victim->motion_id>=ftCo_MS_DamageHi1&&victim->motion_id<=ftCo_MS_DamageFlyRoll;
        out->hitstun=damaged?victim->mv.co.damage.x0:0;out->grab_timer=victim->grab_timer;out->damage_motion=victim->motion_id;
        out->kb_applied=damaged?victim->dmg.kb_applied:0;out->hitlag=damaged?victim->dmg.x195c_hitlag_frames:0;
        out->kb_angle=damaged?victim->dmg.x1848_kb_angle:0;out->element=damaged?victim->dmg.x1860_element:0;
    }
}
static void smb_grab_detach(SmbNativeGrab* lease) {
    Fighter* catcher=smb_grab_catcher(lease);Fighter* victim=smb_grab_victim(lease);
    if(catcher&&victim&&(catcher->victim_gobj==victim->gobj||victim->victim_gobj==catcher->gobj)) {
        m3d_ExitLocal(catcher);m3d_ExitLocal(victim);ftCo_800DC920(catcher->gobj,victim->gobj);
    }
    /* A disconnect or world unload may already have destroyed one actor. */
    if(victim&&!catcher){victim->victim_gobj=victim->x1A5C=NULL;victim->x221B.x221B_b5=victim->x221B.x221B_b7=false;}
    if(catcher&&!victim){catcher->victim_gobj=catcher->x1A5C=NULL;catcher->x221B.x221B_b5=catcher->x221B.x221B_b7=false;}
}
void smb_native_grab_interrupt(Fighter* damaged) {
    if(!damaged)return;
    for(unsigned i=0;i<32;i++) {
        SmbNativeGrab* lease=&smb_grabs[i];if(!lease->id||!lease->simulating||lease->state.phase!=SMB_GRAB_ACTIVE)continue;
        Fighter* catcher=smb_grab_catcher(lease);Fighter* victim=smb_grab_victim(lease);
        if(damaged!=catcher&&damaged!=victim)continue;
        smb_grab_detach(lease);
        /* The caller applies the new impact immediately after this hook. The
         * next after_tick exports that real hit/flight if the victim was hit,
         * or an ordinary escape if the catcher was hit. */
        if(catcher&&catcher!=damaged)ftCo_Fall_Enter(catcher->gobj);
        if(victim&&victim!=damaged)ftCo_Fall_Enter(victim->gobj);
        fprintf(stderr,"SMB NET GRAB INTERRUPT id=%u damaged=%u/%u victim_hit=%d\n",lease->id,damaged->player_idx,damaged->is_sub_fighter,damaged==victim);
    }
}
int smb_native_grab_pair(Fighter* attacker,Fighter* victim) {
    if(!attacker||!victim)return 0;
    for(unsigned i=0;i<32;i++) {
        SmbNativeGrab* lease=&smb_grabs[i];
        if(lease->id&&lease->simulating&&lease->state.phase==SMB_GRAB_ACTIVE&&
           smb_grab_catcher(lease)==attacker&&smb_grab_victim(lease)==victim)return 1;
    }
    return 0;
}
static int smb_grab_state_valid(const SmbGrabState* state) {
    if(!state||state->version!=SMB_MAGIC||!state->id||state->phase<1||state->phase>4||
       !smb_native_remote_valid(&state->victim_state)||!isfinite(state->hitstun)||state->hitstun<0||state->hitstun>2000||
       !isfinite(state->grab_timer)||fabsf(state->grab_timer)>10000||!isfinite(state->kb_applied)||state->kb_applied<0||state->kb_applied>2000||
       !isfinite(state->hitlag)||state->hitlag<0||state->hitlag>1000||state->kb_angle<0||state->kb_angle>367||state->element>31||
       !isfinite(state->damage_total)||state->damage_total<0||state->damage_total>1000000)return 0;
    for(unsigned k=0;k<3;k++)if(!isfinite(state->self_velocity[k])||fabsf(state->self_velocity[k])>1000||
        !isfinite(state->knockback_velocity[k])||fabsf(state->knockback_velocity[k])>1000)return 0;
    return 1;
}
static int smb_grab_damage_import(SmbNativeGrab* lease,Fighter* fp,float total) {
    if(total<lease->damage_imported)return 0;
    unsigned seat=lease->victim;
    if(!seat||(seat<4&&!remote_active[seat]))
        smb_native_damage_applied(fp,total-lease->damage_imported);
    lease->damage_imported=total;return 1;
}
static void smb_grab_pose(Fighter* fp,const SmbFighterState* state) {
    m3d_ExitLocal(fp);float length=hypotf(state->facing[0],state->facing[2]);fp->facing_dir=1;
    if(length>.01f)m3d_SetLane(fp,state->facing[0]/length,state->facing[2]/length);
    if(fp->motion_id!=state->motion||fabsf(fp->cur_anim_frame-state->frame)>1.25f) {
        smb_native_presentation_begin();
        Fighter_ChangeMotionState(fp->gobj,state->motion,0,state->frame,1,0,NULL);ftAnim_8006EBA4(fp->gobj);
        smb_native_presentation_end();
    }
    smb_native_remote_model_apply(fp,&state->model);
    memcpy(&fp->cur_pos,state->position,sizeof fp->cur_pos);
    fp->prev_pos=fp->coll_data.cur_pos=fp->coll_data.prev_pos=fp->coll_data.last_pos=fp->cur_pos;
    fp->ground_or_air=GA_Air;fp->dmg.x1830_percent=state->percent;Player_SetHPByIndex(fp->player_idx,fp->is_sub_fighter,(int)state->percent);
    fp->self_vel=fp->x8c_kb_vel=fp->xD4_unk_vel=(Vec3){0};fp->gr_vel=0;
    fp->input_disabled=true;fp->input_cb=fp->anim_cb=fp->phys_cb=fp->coll_cb=NULL;
    fp->accessory1_cb=fp->accessory2_cb=fp->accessory3_cb=fp->accessory4_cb=NULL;
    fp->x221E_b6=false;fp->x221A_b5=true;
    for(unsigned h=0;h<4;h++)fp->x914[h].state=HitCapsule_Disabled;
    for(unsigned h=0;h<fp->hurt_capsules_len&&h<15;h++){fp->hurt_capsules[h].capsule.state=HurtCapsule_Disabled;fp->hurt_capsules[h].is_grabbable=false;}
    HSD_JObjSetTranslate(fp->gobj->hsd_obj,&fp->cur_pos);m3d_PostMap(fp);
}
static void smb_grab_resume(SmbNativeGrab* lease,const SmbGrabState* state) {
    Fighter* fp=smb_grab_victim(lease);if(!fp)return;
    smb_grab_detach(lease);lease->restored=1;smb_grab_restore_hurt(lease,fp);
    fp->input_disabled=false;ftCommon_8007D5D4(fp);ftCo_Fall_Enter(fp->gobj);
    if(state&&state->phase==SMB_GRAB_THROWN) {
        fp->dmg.x1830_percent=state->victim_state.percent;fp->dmg.x1838_percentTemp=0;
        fp->dmg.kb_applied=state->kb_applied;fp->dmg.x1848_kb_angle=state->kb_angle;
        fp->dmg.x1860_element=state->element;fp->dmg.x184c_damaged_hurtbox=1;
        fp->dmg.facing_dir_1=1;fp->dmg.x1854_collpos=fp->cur_pos;
        unsigned motion=state->damage_motion;
        if(motion<ftCo_MS_DamageHi1||motion>ftCo_MS_DamageFlyRoll)motion=ftCo_MS_DamageFlyTop;
        ftCo_8008DCE0(fp->gobj,motion,1);
        fp->mv.co.damage.x0=state->hitstun;fp->dmg.x195c_hitlag_frames=state->hitlag;
        float lx=1,lz=0;m3d_GetLane(fp,&lx,&lz);
        fp->self_vel=(Vec3){state->self_velocity[0]*lx+state->self_velocity[2]*lz,state->self_velocity[1],0};
        fp->x8c_kb_vel=(Vec3){state->knockback_velocity[0]*lx+state->knockback_velocity[2]*lz,state->knockback_velocity[1],0};
        fp->xD4_unk_vel=(Vec3){0};fp->x221C_b6=state->hitstun>0;
    }
}
int smb_embed_grab_begin(unsigned id,unsigned catcher_seat,unsigned catcher_sub,unsigned victim_seat,unsigned victim_sub) {
    SmbNativeGrab* existing=smb_grab_find(id);
    if(existing)return existing->simulating&&existing->catcher==catcher_seat&&existing->catcher_sub==catcher_sub&&
        existing->victim==victim_seat&&existing->victim_sub==victim_sub?0:-7;
    if(catcher_seat>=19||victim_seat>=19||catcher_sub>1||victim_sub>1||
       (catcher_seat==victim_seat&&catcher_sub==victim_sub))return -1;
    HSD_GObj* catch_object=smb_grab_actor(catcher_seat,catcher_sub);HSD_GObj* victim_object=smb_grab_actor(victim_seat,victim_sub);
    if(!catch_object||!victim_object)return -2;
    Fighter* catcher=catch_object->user_data;Fighter* victim=victim_object->user_data;
    if(catcher->is_sleeping||victim->is_sleeping||!catcher->grab_cb||!catcher->grabbed_cb||!catcher->x221E_b6)return -3;
    if(catcher->victim_gobj||victim->victim_gobj)return -4;
    if((catcher->kind==Ft_Kind_Link||catcher->kind==Ft_Kind_CLink)&&!catcher->u.lk.xC)return -5;
    if(catcher->kind==Ft_Kind_Samus&&!catcher->u.ss.x223C)return -5;
    SmbNativeGrab* lease=smb_grab_allocate(id,victim_seat,victim_sub);if(!lease)return -6;
    lease->catcher=catcher_seat;lease->catcher_sub=catcher_sub;lease->simulating=1;
    lease->catcher_object=catch_object;lease->victim_object=victim_object;
    lease->catcher_spawn=catcher->x8_spawnNum;lease->victim_spawn=victim->x8_spawnNum;
    lease->damage_begin=smb_native_damage_total(victim);
    smb_native_remote_shield(victim_seat,victim_sub,0);
    smb_grab_save_hurt(lease,victim);smb_grab_restore_hurt(lease,victim);
    /* Human proxies share the co-op team, while PvP damage is routed by
     * owners. The borrowed pair must pass retail friendly-fire filtering
     * for native pummels; restore the presentation team when the lease ends. */
    if(victim->team==catcher->team)victim->team=catcher->team==2?3:2;
    victim->x221A_b5=false;victim->input_disabled=false;m3d_ExitLocal(catcher);m3d_ExitLocal(victim);
    /* A dormant presentation CPU becomes the real escape AI while borrowed.
     * Its received level is already configured; Nana retains companion AI. */
    if(victim_seat>0&&victim_seat<4&&!victim_sub)victim->cpu.kind=4;
    ftCo_Fall_Enter(victim_object);catcher->victim_gobj=catcher->x1A5C=victim_object;
    catcher->x221B.x221B_b5=true;catcher->unk_grab_val=0;
    ftColl_80078754(catch_object,victim_object,0);
    catcher->grab_cb(catch_object);catcher->grabbed_cb(victim_object,catch_object);
    lease->state.phase=SMB_GRAB_ACTIVE;smb_grab_snapshot(lease);
    fprintf(stderr,"SMB NET GRAB BEGIN id=%u catcher=%u/%u victim=%u/%u kind=%u/%u teams=%u/%u original_victim_team=%u\n",id,catcher_seat,catcher_sub,victim_seat,victim_sub,catcher->kind,victim->kind,catcher->team,victim->team,lease->team);
    return 0;
}
int smb_embed_grab_present(unsigned id,unsigned seat,unsigned sub,const SmbGrabState* state) {
    if(!smb_grab_state_valid(state)||state->id!=id)return -1;
    SmbNativeGrab* existing=smb_grab_find(id);
    if(existing&&existing->presenting&&existing->state.phase!=SMB_GRAB_ACTIVE&&state->phase==SMB_GRAB_ACTIVE)return 0;
    if(existing&&existing->presenting&&existing->restored&&state->phase!=SMB_GRAB_ACTIVE)return 0;
    HSD_GObj* object=smb_grab_actor(seat,sub);if(!object)return -2;
    Fighter* fp=object->user_data;if(fp->kind!=state->victim_state.kind)return -3;
    if(existing&&existing->presenting&&(existing->victim_object!=object||existing->victim_spawn!=fp->x8_spawnNum))return -5;
    SmbNativeGrab* lease=smb_grab_allocate(id,seat,sub);if(!lease||lease->simulating)return -4;
    /* Old/repeated cumulative updates do not create another air penalty. An
     * observer only presents the actor; its real owner alone consumes events.
     * Use the explicit ownership predicate, since a locally captured actor is
     * deliberately classified as remote-controlled while its pose is shown. */
    if(!smb_grab_damage_import(lease,fp,state->damage_total))return 0;
    lease->presenting=1;lease->at=smb_grab_clock;lease->state=*state;
    lease->victim_object=object;lease->victim_spawn=fp->x8_spawnNum;smb_grab_save_hurt(lease,fp);
    smb_grab_pose(fp,&state->victim_state);fp->grab_timer=state->grab_timer;
    if(state->phase!=SMB_GRAB_ACTIVE&&(seat==0||(seat<4&&!remote_active[seat])))smb_grab_resume(lease,state);
    return 0;
}
int smb_embed_grab_snapshot(unsigned id,SmbGrabState* state) {
    SmbNativeGrab* lease=smb_grab_find(id);if(!lease||!state)return -1;
    if(lease->simulating&&lease->state.phase==SMB_GRAB_ACTIVE)smb_grab_snapshot(lease);
    *state=lease->state;return 0;
}
int smb_embed_grab_mash(unsigned id,unsigned buttons,float x,float y) {
    SmbNativeGrab* lease=smb_grab_find(id);
    if(!lease||!lease->simulating||!isfinite(x)||!isfinite(y)||fabsf(x)>1.01f||fabsf(y)>1.01f)return -1;
    lease->buttons=buttons&0x1fff;lease->stick[0]=x;lease->stick[1]=y;lease->at=smb_grab_clock;return 0;
}
void smb_native_grab_raw_input(const SmbInput* command) {
    if(!command)return;smb_grab_raw_buttons=command->buttons;smb_grab_raw_stick[0]=command->stick_x;smb_grab_raw_stick[1]=command->stick_y;
}
int smb_embed_grab_input(unsigned id,uint32_t* buttons,float stick[2]) {
    SmbNativeGrab* lease=smb_grab_find(id);if(!lease||!lease->presenting||lease->restored||!buttons||!stick)return -1;
    if(!lease->victim){*buttons=smb_grab_raw_buttons;memcpy(stick,smb_grab_raw_stick,sizeof smb_grab_raw_stick);}
    else {Fighter* fp=smb_grab_victim(lease);if(!fp)return -2;*buttons=fp->input.held_buttons[0];stick[0]=fp->input.lstick[0].x;stick[1]=fp->input.lstick[0].y;}
    return 0;
}
int smb_embed_grab_end(unsigned id,unsigned restore_owned) {
    SmbNativeGrab* lease=smb_grab_find(id);if(!lease)return 0;
    smb_grab_detach(lease);Fighter* victim=smb_grab_victim(lease);Fighter* catcher=smb_grab_catcher(lease);
    if(victim&&lease->hurt_saved)victim->team=lease->team;
    if(restore_owned&&!lease->restored)smb_grab_resume(lease,lease->state.phase==SMB_GRAB_THROWN?&lease->state:NULL);
    else if(victim&&lease->simulating&&remote_active[lease->victim]) {
        /* It returns to an ordinary proxy until its owner publishes flight. */
        victim->input_disabled=true;victim->input_cb=victim->anim_cb=victim->phys_cb=victim->coll_cb=NULL;
        victim->accessory1_cb=victim->accessory2_cb=victim->accessory3_cb=victim->accessory4_cb=NULL;
        for(unsigned h=0;h<victim->hurt_capsules_len&&h<15;h++){victim->hurt_capsules[h].capsule.state=HurtCapsule_Disabled;victim->hurt_capsules[h].is_grabbable=false;}
        victim->x221A_b5=true;
    }
    if(catcher&&lease->simulating&&(lease->state.phase==SMB_GRAB_ACTIVE||lease->state.phase==SMB_GRAB_ABORTED))ftCo_Fall_Enter(catcher->gobj);
    fprintf(stderr,"SMB NET GRAB END id=%u phase=%u restore=%u\n",id,lease->state.phase,restore_owned);memset(lease,0,sizeof *lease);return 0;
}
void smb_embed_grab_cancel_all(void) {
    for(unsigned i=0;i<32;i++)if(smb_grabs[i].id) {
        unsigned restore=smb_grabs[i].presenting&&(smb_grabs[i].victim==0||(smb_grabs[i].victim<4&&!remote_active[smb_grabs[i].victim]));
        smb_embed_grab_end(smb_grabs[i].id,restore);
    }
}
void smb_native_grab_before_tick(void) {
    smb_grab_clock++;
    for(unsigned i=0;i<32;i++) {
        SmbNativeGrab* lease=&smb_grabs[i];if(!lease->id)continue;
        Fighter* victim=smb_grab_victim(lease);Fighter* catcher=lease->simulating?smb_grab_catcher(lease):NULL;
        if(!victim||(lease->simulating&&!catcher)||smb_grab_clock-lease->at>180) {
            if(lease->simulating){lease->state.phase=SMB_GRAB_ABORTED;smb_grab_detach(lease);
                if(catcher)ftCo_Fall_Enter(catcher->gobj);if(victim)ftCo_Fall_Enter(victim->gobj);smb_grab_snapshot(lease);}
            else smb_embed_grab_end(lease->id,lease->victim==0||(lease->victim<4&&!remote_active[lease->victim]));
            continue;
        }
        if(lease->presenting&&!lease->restored) {
            smb_grab_pose(victim,&lease->state.victim_state);victim->grab_timer=lease->state.grab_timer;
        }
        if(!lease->simulating||lease->state.phase!=SMB_GRAB_ACTIVE)continue;
        lease->origin=victim->cur_pos;lease->origin_valid=1;
        if(victim->player_idx>=4) {
            HSD_PadStatus pad={0};pad.button=lease->buttons;pad.last_button=lease->previous_buttons;
            pad.trigger=pad.button&~pad.last_button;pad.release=pad.last_button&~pad.button;
            pad.nml_stickX=lease->stick[0];pad.nml_stickY=lease->stick[1];
            pad.stickX=lease->stick[0]*80;pad.stickY=lease->stick[1]*80;
            HSD_PadGameStatus[victim->pad_port]=pad;lease->previous_buttons=lease->buttons;
        }
    }
}
static void smb_native_grab_test(void);
void smb_native_grab_after_tick(void) {
    for(unsigned i=0;i<32;i++) {
        SmbNativeGrab* lease=&smb_grabs[i];if(!lease->id||!lease->simulating||lease->state.phase!=SMB_GRAB_ACTIVE)continue;
        Fighter* catcher=smb_grab_catcher(lease);Fighter* victim=smb_grab_victim(lease);
        if(!catcher||!victim){lease->state.phase=SMB_GRAB_ABORTED;smb_grab_detach(lease);
            if(catcher)ftCo_Fall_Enter(catcher->gobj);if(victim)ftCo_Fall_Enter(victim->gobj);smb_grab_snapshot(lease);continue;}
        if(catcher->victim_gobj!=victim->gobj||victim->victim_gobj!=catcher->gobj) {
            int launched=victim->motion_id>=ftCo_MS_DamageHi1&&victim->motion_id<=ftCo_MS_DamageFlyRoll&&hypotf(victim->x8c_kb_vel.x,victim->x8c_kb_vel.y)>.01f;
            lease->state.phase=launched?SMB_GRAB_THROWN:SMB_GRAB_ESCAPED;
            if(lease->origin_valid&&victim->player_idx>=4)smb_native_constrain(victim,&lease->origin);
            smb_grab_snapshot(lease);
            fprintf(stderr,"SMB NET GRAB RELEASE id=%u phase=%u percent=%.2f hitstun=%.2f velocity=%.3f,%.3f,%.3f\n",
                lease->id,lease->state.phase,lease->state.victim_state.percent,lease->state.hitstun,lease->state.knockback_velocity[0],lease->state.knockback_velocity[1],lease->state.knockback_velocity[2]);
        }
    }
    smb_native_grab_test();
}
static int smb_grab_damage_test_import(Fighter* owner,Fighter* observer) {
    SmbDamageEvents saved=*smb_native_damage_slot(owner),remote_saved=*smb_native_damage_slot(observer);
    smb_native_damage_reset(owner);smb_native_damage_reset(observer);
    SmbNativeGrab local={0},remote={0};local.victim=owner->player_idx;remote.victim=observer->player_idx;
    const float totals[]={3,7,7,4,9},expected[]={3,4,0,0,2};int okay=1;
    for(unsigned i=0;i<5;i++) {
        smb_grab_damage_import(&local,owner,totals[i]);
        if(smb_embed_actor_damage(owner->player_idx,owner->is_sub_fighter)!=expected[i])okay=0;
    }
    smb_grab_damage_import(&remote,observer,9);
    if(smb_embed_actor_damage(observer->player_idx,observer->is_sub_fighter)!=0)okay=0;
    *smb_native_damage_slot(owner)=saved;*smb_native_damage_slot(observer)=remote_saved;
    fprintf(stderr,"SMB GRAB DAMAGE IMPORT %s duplicate=1 reordered=1 healing_independent=1 observer_silent=1\n",okay?"PASS":"FAIL");return okay;
}
static int smb_grab_test_owned_query(Fighter* owner,Fighter* proxy) {
    SmbGrabState state={0};state.version=SMB_MAGIC;state.id=0x61697271;state.phase=SMB_GRAB_ACTIVE;
    state.victim=owner->player_idx;state.victim_sub=owner->is_sub_fighter;
    smb_native_remote_state(owner,&state.victim_state);
    int result=smb_embed_grab_present(state.id,state.victim,state.victim_sub,&state);
    if(result)return 0;
    Vec3 position=owner->cur_pos,velocity=owner->self_vel;
    SmbFighterState pose;
    int owned=smb_native_remote_controlled(owner)&&smb_embed_actor_state(state.victim,state.victim_sub,&pose)==0&&
        pose.kind==(unsigned)owner->kind&&pose.percent==owner->dmg.x1830_percent;
    int frozen=owner->input_disabled&&!owner->phys_cb&&!owner->coll_cb&&
        !memcmp(&position,&owner->cur_pos,sizeof position)&&!memcmp(&velocity,&owner->self_vel,sizeof velocity);
    int excluded=smb_embed_actor_state(proxy->player_idx,proxy->is_sub_fighter,&pose)== -3;
    smb_embed_grab_end(state.id,1);
    fprintf(stderr,"SMB GRAB OWNED QUERY %s owned_pose=%d physics_frozen=%d proxy_excluded=%d\n",
        owned&&frozen&&excluded?"PASS":"FAIL",owned,frozen,excluded);return owned&&frozen&&excluded;
}
/* Exercise the retail Link tether, pummel and up throw against a native remote
 * Fox. This fixture is opt-in and never runs in ordinary gameplay. */
static void smb_native_grab_test(void) {
    if(!getenv("SMB_SMOKE")||!getenv("SMB_TEST_NETWORK_GRAB"))return;
    static unsigned phase,start,pummel_frame;static float pummel_damage;const unsigned id=0x74657374;
    HSD_GObj* player=Player_GetEntity(0);if(!player)return;Fighter* fp=player->user_data;
    if(!phase) {
        if(!host_world||smb_grab_clock<60)return;
        if(fp->kind!=Ft_Kind_Link){fprintf(stderr,"SMB NET GRAB TEST FAIL reason=requires-link\n");phase=99;return;}
        float lx=1,lz=0;m3d_GetLane(fp,&lx,&lz);
        SmbRemote fox={0};fox.version=SMB_MAGIC;fox.character=2;fox.kind=Ft_Kind_Fox;fox.motion=ftCo_MS_Wait;
        fox.position[0]=fp->cur_pos.x+lx*12;fox.position[1]=fp->cur_pos.y;fox.position[2]=fp->cur_pos.z+lz*12;fox.facing[0]=-lx;fox.facing[2]=-lz;
        int result=smb_native_remote_apply(4,&fox,0);if(result){fprintf(stderr,"SMB NET GRAB TEST FAIL reason=create-fox code=%d\n",result);smb_native_remote_apply(4,NULL,0);phase=99;return;}
        smb_embed_actor_damage(4,0);
        ftCo_800D8C54(player,ftCo_MS_Catch);phase=1;start=smb_grab_clock;
        fprintf(stderr,"SMB NET GRAB TEST READY kind=%u x=%.2f y=%.2f z=%.2f\n",fp->kind,fp->cur_pos.x,fp->cur_pos.y,fp->cur_pos.z);return;
    }
    if(phase==99||phase==98)return;
    SmbNativeGrab* heartbeat=smb_grab_find(id);if(heartbeat)heartbeat->at=smb_grab_clock;
    if(smb_grab_clock-start>300){fprintf(stderr,"SMB NET GRAB TEST FAIL reason=timeout phase=%u motion=%u tether=%d\n",phase,fp->motion_id,fp->u.lk.xC!=NULL);phase=99;smb_embed_grab_cancel_all();return;}
    if(phase==1&&fp->u.lk.xC) {
        int result=smb_embed_grab_begin(id,0,0,4,0);
        if(result){fprintf(stderr,"SMB NET GRAB TEST FAIL reason=begin code=%d\n",result);phase=99;return;}
        phase=2;
    } else if(phase==2&&fp->motion_id==ftCo_MS_CatchWait) {
        fn_800DA4FC(player);pummel_frame=smb_grab_clock;phase=3;
    } else if(phase==3&&smb_grab_clock>pummel_frame+2&&fp->motion_id==ftCo_MS_CatchWait) {
        Fighter* victim=heartbeat?smb_grab_victim(heartbeat):NULL;
        if(!victim||victim->dmg.x1830_percent<=0){fprintf(stderr,"SMB NET GRAB TEST FAIL reason=pummel-damage\n");phase=99;smb_embed_grab_cancel_all();return;}
        SmbGrabState state;
        pummel_damage=smb_embed_actor_damage(4,0);
        if(smb_embed_grab_snapshot(id,&state)||pummel_damage<=0||fabsf(state.damage_total-pummel_damage)>.001f||smb_embed_actor_damage(4,0)!=0) {
            fprintf(stderr,"SMB NET GRAB TEST FAIL reason=pummel-events\n");phase=99;smb_embed_grab_cancel_all();return;
        }
        fprintf(stderr,"SMB NET GRAB TEST PUMMEL percent=%.2f\n",victim->dmg.x1830_percent);
        ftCo_800DD4B0(player,ftCo_MS_ThrowHi);phase=4;
    } else if(phase==4) {
        SmbGrabState state;if(!smb_embed_grab_snapshot(id,&state)&&state.phase!=SMB_GRAB_ACTIVE) {
            float speed=sqrtf(state.knockback_velocity[0]*state.knockback_velocity[0]+state.knockback_velocity[1]*state.knockback_velocity[1]+state.knockback_velocity[2]*state.knockback_velocity[2]);
            Fighter* victim=heartbeat?smb_grab_victim(heartbeat):NULL;
            float throw_damage=smb_embed_actor_damage(4,0);
            int damage=throw_damage>0&&fabsf(state.damage_total-pummel_damage-throw_damage)<.001f&&smb_embed_actor_damage(4,0)==0;
            int imported=victim&&smb_grab_damage_test_import(fp,victim);
            smb_embed_grab_end(id,0);
            int owned_query=victim&&smb_grab_test_owned_query(fp,victim);
            int passed=state.phase==SMB_GRAB_THROWN&&state.victim_state.percent>0&&speed>.01f&&state.hitstun>0&&damage&&imported&&owned_query;
            fprintf(stderr,"SMB NET GRAB TEST %s phase=%u percent=%.2f speed=%.4f hitstun=%.1f motion=%u actual_damage=%.3f damage_events=%d\n",passed?"PASS":"FAIL",state.phase,state.victim_state.percent,speed,state.hitstun,state.damage_motion,state.damage_total,damage&&imported);
            smb_native_remote_apply(4,NULL,0);phase=98;
        }
    }
}
