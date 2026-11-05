/* Remote native actors are presentation proxies. Their owning client alone
 * advances input, collision and damage; every partner has its own authored pose. */
static unsigned remote_active[GM_MAX_PLAYERS],remote_character[GM_MAX_PLAYERS],remote_costume[GM_MAX_PLAYERS],remote_launch_pose[GM_MAX_PLAYERS][2];
static float remote_launch_angle[GM_MAX_PLAYERS][2];
static unsigned char remote_hurt_saved[GM_MAX_PLAYERS][2],remote_grabbable[GM_MAX_PLAYERS][2][15];
static SmbFighterModel remote_models[GM_MAX_PLAYERS][2];
static unsigned smb_presentation_depth;
void smb_native_presentation_begin(void){smb_presentation_depth++;}
void smb_native_presentation_end(void){if(smb_presentation_depth)smb_presentation_depth--;}
#include "melee_remote_shield.inc"
extern unsigned smb_native_motion_count(unsigned);
static void smb_native_remote_articles(unsigned,const SmbItem*,unsigned);
extern int smb_native_grab_borrowed(unsigned,unsigned);
extern int smb_native_grab_remote_controlled(Fighter*);
int smb_native_remote_controlled(Fighter* fp) {
    if(smb_native_grab_remote_controlled(fp))return 1;
    if(smb_native_grab_borrowed(fp->player_idx,fp->is_sub_fighter?1:0))return 0;
    return fp->player_idx>0&&fp->player_idx<19&&remote_active[fp->player_idx];
}
int smb_native_presentation_only(Fighter* fp) {
    return smb_presentation_depth||(fp&&smb_native_remote_controlled(fp));
}
int smb_native_presentation_command(unsigned index) {
    /* These action-script opcodes change only model selection, visibility,
     * textures and part animation. Every gameplay opcode is skipped using
     * the interpreter's native command-size table. */
    switch(index){case 21:case 22:case 23:case 26:case 27:case 30:case 31:return 1;default:return 0;}
}
void smb_native_remote_model(Fighter* fp,SmbFighterModel* model) {
    memset(model,0,sizeof *model);
    if(!fp||fp->x5AC.model_num>12||fp->dobj_list.count>256)return;
    /* Model selectors become DObj flags lazily during a native draw. Resolve
     * the normal body group before exporting, including between draw frames. */
    ftParts_80074D7C(&fp->x5AC,0,&fp->dobj_list);
    ftParts_80074B6C(fp,&fp->x5AC,0,&fp->dobj_list);
    model->model_count=fp->x5AC.model_num;
    for(unsigned n=0;n<model->model_count;n++)model->part[n]=fp->x5F4_arr[n].idx;
    for(unsigned n=0;n<fp->dobj_list.count;n++)if(fp->dobj_list.data[n]->flags&DOBJ_HIDDEN)model->dobj_hidden[n/32]|=1u<<(n%32);
}
static int smb_native_remote_model_valid(Fighter* fp,const SmbFighterModel* model) {
    if(!model->model_count)return 1; /* Older synthetic diagnostic poses. */
    if(model->model_count>12||model->model_count!=fp->x5AC.model_num||fp->dobj_list.count>256)return 0;
    for(unsigned n=0;n<model->model_count;n++) {
        int part=model->part[n],limit=0;if(part< -1||part>127)return 0;
        for(unsigned group=0;group<5;group++)if(fp->x5AC.xC[group]) {
            int count=fp->x5AC.xC[group][n].x0;if(count<0||count>128)return 0;
            if(count>limit)limit=count;
        }
        if(part>=limit&&(limit||part>0))return 0;
    }
    return 1;
}
int smb_native_remote_model_apply(Fighter* fp,const SmbFighterModel* model) {
    if(!fp||!model||fp->player_idx>=19||!smb_native_remote_model_valid(fp,model))return -1;
    remote_models[fp->player_idx][fp->is_sub_fighter?1:0]=*model;
    if(!model->model_count)return 0;
    for(unsigned n=0;n<model->model_count;n++)fp->x5F4_arr[n].idx=model->part[n];
    for(unsigned n=0;n<fp->dobj_list.count;n++) {
        if(model->dobj_hidden[n/32]&(1u<<(n%32)))HSD_DObjSetFlags(fp->dobj_list.data[n],DOBJ_HIDDEN);
        else HSD_DObjClearFlags(fp->dobj_list.data[n],DOBJ_HIDDEN);
    }
    /* Native state entry must reset an imported override if this actor later
     * becomes locally authoritative (for example an NPC handoff). */
    fp->x221D_b2=true;fp->x5AC.cleared[0]=true;return 0;
}
void smb_native_remote_anim(HSD_GObj* object) {
    Fighter* fp=object->user_data;if(fp->is_sleeping)return;
    /* The regular animation proc also owns poison, item, hitstun and death
     * timers. A proxy needs only its authored skeleton and visual scripts. */
    fp->prev_pos=fp->cur_pos;
    ftAnim_8006EBA4(object);ftCo_800C0408(object);
    if(fp->player_idx<19) {
        unsigned seat=fp->player_idx,sub=fp->is_sub_fighter?1:0;
        smb_native_remote_model_apply(fp,&remote_models[seat][sub]);
        /* Sampling a native skeleton also animates XRotN. Restore the
         * owner-authored flight orientation after that sampling. */
        if(remote_launch_pose[seat][sub])ftPartSetRotX(fp,ftParts_GetBoneIndex(fp,FtPart_XRotN),remote_launch_angle[seat][sub]);
    }
}
void smb_native_remote_state(Fighter* fp,SmbFighterState* state) {
    memset(state,0,sizeof *state);if(!fp)return;
    smb_native_remote_model(fp,&state->model);if(fp->is_sleeping)return;
    m3d_ExitLocal(fp);
    state->kind=fp->kind;state->motion=fp->motion_id;state->grounded=fp->ground_or_air==GA_Ground;
    state->caps=(fp->is_metal?1:0)|(fp->x2226_b4?2:0)|((ftLib_IsShielding(fp->gobj)||fp->motion_id==ftCo_MS_GuardReflect)?8:0);
    /* Motions without skeletal animation use -1 natively. Wire poses use
     * frame zero so a presentation receiver never seeks before the start. */
    state->frame=fmaxf(0,fp->cur_anim_frame);state->percent=fp->dmg.x1830_percent;
    memcpy(state->position,&fp->cur_pos,sizeof state->position);
    float lx=1,lz=0;m3d_GetLane(fp,&lx,&lz);
    state->facing[0]=lx*fp->facing_dir;state->facing[2]=lz*fp->facing_dir;
    float along=fp->self_vel.x+fp->x8c_kb_vel.x+fp->xD4_unk_vel.x;
    state->velocity[0]=along*lx;state->velocity[1]=fp->self_vel.y+fp->x8c_kb_vel.y+fp->xD4_unk_vel.y;state->velocity[2]=along*lz+fp->self_vel.z;
}
int smb_embed_actor_state(unsigned seat,unsigned sub,SmbFighterState* state) {
    if(seat>=19||sub>1||!state)return -1;
    memset(state,0,sizeof *state);HSD_GObj* object=Player_GetEntityAtIndex(seat,sub);
    if(!object)return -2;Fighter* fp=object->user_data;
    int owned_presented=smb_native_grab_remote_controlled(fp)&&(!seat||(seat<4&&!remote_active[seat]));
    /* A borrowed grab freezes the real owner's pose, but its own underwater
     * air and pending damage still belong to this machine. Querying that pose
     * must remain possible; ordinary remote presentation stays excluded. */
    if(fp->is_sleeping||(smb_native_remote_controlled(fp)&&!owned_presented))return -3;
    smb_native_remote_state(fp,state);return 0;
}
static int smb_native_remote_valid(const SmbFighterState* state) {
    if(state->kind>=40||state->motion>=smb_native_motion_count(state->kind)||
       !isfinite(state->frame)||(state->frame<0&&!(state->frame==-1.f&&(state->caps&8)))||state->frame>20000||
       !isfinite(state->percent)||state->percent<0||state->percent>999)return 0;
    if(state->model.model_count>12)return 0;
    for(unsigned n=0;n<state->model.model_count;n++)if(state->model.part[n]<-1||state->model.part[n]>127)return 0;
    for(unsigned k=0;k<3;k++)if(!isfinite(state->position[k])||fabsf(state->position[k])>10000||
       !isfinite(state->facing[k])||fabsf(state->facing[k])>2||!isfinite(state->velocity[k])||fabsf(state->velocity[k])>1000)return 0;
    return 1;
}
static SmbFighterState smb_native_remote_primary(const SmbRemote* state) {
    SmbFighterState result={0};result.kind=state->kind;result.motion=state->motion;result.grounded=state->grounded;result.caps=state->caps;
    result.frame=state->frame;result.percent=state->percent;
    memcpy(result.position,state->position,sizeof result.position);memcpy(result.facing,state->facing,sizeof result.facing);memcpy(result.velocity,state->velocity,sizeof result.velocity);
    result.model=state->model;
    return result;
}
int smb_native_remote_release(unsigned seat) {
    if(!seat||seat>=19)return -1;
    remote_active[seat]=0;smb_native_remote_articles(seat,NULL,0);
    memset(remote_models[seat],0,sizeof remote_models[seat]);
    for(unsigned sub=0;sub<2;sub++)smb_native_remote_shield(seat,sub,0);
    Player_SetSlottype(seat,seat<4?Gm_PKind_Cpu:Gm_PKind_Human);
    for(unsigned sub=0;sub<2;sub++) {
        HSD_GObj* object=Player_GetEntityAtIndex(seat,sub);if(!object)continue;
        Fighter* fp=object->user_data;if(fp->is_sleeping)continue;
        m3d_ExitLocal(fp);fp->input_disabled=false;
        if(remote_launch_pose[seat][sub])ftPartSetRotX(fp,ftParts_GetBoneIndex(fp,FtPart_XRotN),0);
        remote_launch_pose[seat][sub]=0;remote_launch_angle[seat][sub]=0;ftCo_Fall_Enter(object);
        for(unsigned h=0;h<fp->hurt_capsules_len&&h<15;h++) {
            fp->hurt_capsules[h].capsule.state=HurtCapsule_Enabled;
            if(remote_hurt_saved[seat][sub])fp->hurt_capsules[h].is_grabbable=remote_grabbable[seat][sub][h];
        }
        fp->x221A_b5=false;remote_hurt_saved[seat][sub]=0;
    }
    if(seat<4)Player_SetPlayerAndEntityCpuType(seat,4);
    return 0;
}
static int smb_native_remote_apply_impl(unsigned seat,const SmbRemote* state,unsigned team) {
    if(!seat||seat>=19)return -1;
    if(!state||!state->version) {
        for(unsigned sub=0;sub<2;sub++)smb_native_remote_shield(seat,sub,0);
        smb_native_remote_articles(seat,NULL,0);
        if(remote_active[seat]) {
            for(unsigned sub=0;sub<2;sub++){HSD_GObj* object=Player_GetEntityAtIndex(seat,sub);if(object)m3d_ExitLocal(object->user_data);}
            smb_native_clear_npc_items(seat);ifStatus_802F6788(seat);
            if(Player_GetEntity(seat))Player_80031EBC(seat);
            Player_InitOrResetPlayer(seat);remote_active[seat]=0;
            memset(remote_launch_pose[seat],0,sizeof remote_launch_pose[seat]);memset(remote_hurt_saved[seat],0,sizeof remote_hurt_saved[seat]);
            memset(remote_launch_angle[seat],0,sizeof remote_launch_angle[seat]);
            memset(remote_models[seat],0,sizeof remote_models[seat]);
        }
        return 0;
    }
    SmbFighterState primary=smb_native_remote_primary(state);
    if(state->version!=SMB_MAGIC||!smb_embed_costumes(state->character)||!smb_native_remote_valid(&primary)||
       (state->partner_active&&!smb_native_remote_valid(&state->partner)))return -2;
    if(!remote_active[seat]||remote_character[seat]!=state->character||remote_costume[seat]!=state->costume) {
        for(unsigned sub=0;sub<2;sub++)smb_native_remote_shield(seat,sub,0);
        smb_native_remote_articles(seat,NULL,0);smb_native_clear_npc_items(seat);ifStatus_802F6788(seat);
        if(Player_GetEntity(seat))Player_80031EBC(seat);
        Player_InitOrResetPlayer(seat);
        Player_SetPlayerCharacter(seat,state->character);
        Player_SetCostumeId(seat,state->costume%smb_embed_costumes(state->character));
        Player_SetSlottype(seat,seat<4?Gm_PKind_Cpu:Gm_PKind_Human);Player_SetTeam(seat,team);Player_SetPadPort(seat,seat);
        Player_SetStocks(seat,1);Player_SetHandicap(seat,9);Player_SetFacingDirection(seat,1);Player_SetNametagSlotID(seat,0x78);
        smb_native_expect_costume(seat,Player_GetCostumeId(seat));Player_80031AD0(seat);Player_80031848(seat);ifStatus_802F6508(seat);
        remote_character[seat]=state->character;remote_costume[seat]=state->costume;remote_active[seat]=1;
        memset(remote_launch_pose[seat],0,sizeof remote_launch_pose[seat]);memset(remote_hurt_saved[seat],0,sizeof remote_hurt_saved[seat]);lbAudioAx_8002785C();
        memset(remote_launch_angle[seat],0,sizeof remote_launch_angle[seat]);
        memset(remote_models[seat],0,sizeof remote_models[seat]);
        fprintf(stderr,"SMB COOP FIGHTER seat=%u external=%u costume=%u partner=%u\n",seat,state->character,state->costume,state->partner_active);
    }
    for(unsigned sub=0;sub<2;sub++) {
        HSD_GObj* object=Player_GetEntityAtIndex(seat,sub);if(!object)continue;
        Fighter* fp=object->user_data;if(smb_native_grab_borrowed(seat,sub))continue;
        /* A sleeping transform is not a second playable fighter. Only Nana
         * is revived by a partner snapshot; Zelda/Sheik keep native ownership. */
        if(sub&&!state->partner_active){smb_native_remote_shield(seat,sub,0);if(fp->kind==Ft_Kind_Nana)fp->is_sleeping=true;continue;}
        const SmbFighterState* pose=sub?&state->partner:&primary;
        if(fp->kind!=pose->kind)return -4;
        if(!smb_native_remote_model_valid(fp,&pose->model))return -5;
        fp->is_sleeping=false;m3d_ExitLocal(fp);
        /* Presentation proxies have no owned collision slice. Keep native
         * housekeeping airborne so foot dust cannot query native line -1. */
        fp->ground_or_air=GA_Air;fp->facing_dir=1;
        float length=hypotf(pose->facing[0],pose->facing[2]);
        if(length>.01f)m3d_SetLane(fp,pose->facing[0]/length,pose->facing[2]/length);
        /* Guard may export the native -1 sentinel before its first animation
         * tick. The proxy presents the first frame rather than seeking -1. */
        float frame=pose->frame<0?0:pose->frame;
        if(fp->motion_id!=pose->motion||fabsf(fp->cur_anim_frame-frame)>1.25f) {
            Fighter_ChangeMotionState(object,pose->motion,0,frame,1,0,NULL);ftAnim_8006EBA4(object);
        }
        int launched=(pose->caps&4)||(pose->motion>=ftCo_MS_DamageFlyHi&&pose->motion<=ftCo_MS_DamageFlyRoll);
        if(launched||remote_launch_pose[seat][sub]) {
            float along=pose->velocity[0]*pose->facing[0]+pose->velocity[2]*pose->facing[2];
            remote_launch_angle[seat][sub]=(pose->caps&4)?1.5707963268f:launched?atan2f(along,pose->velocity[1]):0;
            ftPartSetRotX(fp,ftParts_GetBoneIndex(fp,FtPart_XRotN),remote_launch_angle[seat][sub]);
        }
        remote_launch_pose[seat][sub]=launched;
        fp->input_disabled=true;fp->input_cb=NULL;fp->anim_cb=NULL;fp->phys_cb=NULL;fp->coll_cb=NULL;fp->cam_cb=NULL;
        fp->accessory1_cb=fp->accessory2_cb=fp->accessory3_cb=fp->accessory4_cb=NULL;fp->x221E_b6=false;
        for(unsigned h=0;h<4;h++)fp->x914[h].state=HitCapsule_Disabled;
        for(unsigned h=0;h<fp->hurt_capsules_len&&h<15;h++) {
            if(!remote_hurt_saved[seat][sub])remote_grabbable[seat][sub][h]=fp->hurt_capsules[h].is_grabbable;
            fp->hurt_capsules[h].capsule.state=HurtCapsule_Disabled;fp->hurt_capsules[h].is_grabbable=false;
        }
        fp->x221A_b5=true;remote_hurt_saved[seat][sub]=1;
        fp->x8c_kb_vel=fp->xD4_unk_vel=(Vec3){0};fp->gr_vel=0;
        fp->self_vel=(Vec3){pose->velocity[0]*pose->facing[0]+pose->velocity[2]*pose->facing[2],pose->velocity[1],0};
        memcpy(&fp->cur_pos,pose->position,sizeof fp->cur_pos);
        fp->prev_pos=fp->coll_data.cur_pos=fp->coll_data.prev_pos=fp->coll_data.last_pos=fp->cur_pos;
        fp->dmg.x1830_percent=pose->percent;Player_SetHPByIndex(seat,sub,(int)pose->percent);
        HSD_JObjSetTranslate(object->hsd_obj,&fp->cur_pos);m3d_PostMap(fp);
        if(pose->caps&1){if(!fp->is_metal)ftCo_800C8348(object,4,1000000);}else if(fp->is_metal)ftCo_800C8540(object);
        if(pose->caps&2){if(!fp->x2226_b4)ftCo_800C88D4(object,4,false);}else if(fp->x2226_b4)ftCo_800C8A64(object);
        smb_native_remote_shield(seat,sub,!!(pose->caps&8));
        if(smb_native_remote_model_apply(fp,&pose->model))return -5;
    }
    return 0;
}
int smb_native_remote_apply(unsigned seat,const SmbRemote* state,unsigned team) {
    smb_native_presentation_begin();
    int result=smb_native_remote_apply_impl(seat,state,team);
    smb_native_presentation_end();return result;
}
int smb_embed_remote(unsigned peer,const SmbRemote* state) {
    if(peer<1||peer>15)return -1;return smb_native_remote_apply(peer+3,state,0);
}
#include "melee_remote_items.inc"

#include "melee_remote_test.inc"
