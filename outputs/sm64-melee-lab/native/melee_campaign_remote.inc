/* A course encounter is simulated once, by its Coop scene owner. Other
 * machines use the same native fighter presentation as remote humans. */
static Fighter* smb_campaign_target(Fighter* cpu) {
    Fighter* best=NULL;float best_distance=1.e30f;
    for(unsigned seat=0;seat<19;seat++) {
        if(seat>0&&seat<4)continue;
        if(Player_GetPlayerSlotType(seat)!=Gm_PKind_Human)continue;
        for(unsigned sub=0;sub<2;sub++) {
            HSD_GObj* object=Player_GetEntityAtIndex(seat,sub);if(!object)continue;
            Fighter* candidate=object->user_data;
            if(!candidate||candidate->is_sleeping)continue;
            float x=candidate->cur_pos.x-cpu->cur_pos.x;
            float y=candidate->cur_pos.y-cpu->cur_pos.y;
            float z=candidate->cur_pos.z-cpu->cur_pos.z;
            float distance=x*x+y*y+z*z;
            /* Avoid switching targets back and forth between nearby allies. */
            if(candidate==cpu->cpu.x44)distance*=.75f;
            if(distance<best_distance){best=candidate;best_distance=distance;}
        }
    }
    return best;
}
Fighter* smb_native_campaign_target(Fighter* cpu) {return smb_campaign_target(cpu);}
static void smb_campaign_fighter_state(Fighter* fp,SmbFighterState* state) {
    smb_native_remote_state(fp,state);
    if(fp->is_sleeping) {
        /* A pending encounter still needs its identity and damage during an
         * ownership handoff, although presentation is intentionally asleep. */
        state->kind=fp->kind;state->motion=fp->motion_id;state->frame=fmaxf(0,fp->cur_anim_frame);
        state->percent=fp->dmg.x1830_percent;state->grounded=fp->ground_or_air==GA_Ground;
        state->caps=(fp->is_metal?1:0)|(fp->x2226_b4?2:0);memcpy(state->position,&fp->cur_pos,sizeof state->position);
        float lx=1,lz=0;m3d_GetLane(fp,&lx,&lz);state->facing[0]=lx*fp->facing_dir;state->facing[2]=lz*fp->facing_dir;
    }
}
int smb_embed_campaign_snapshot(unsigned seat,SmbRemote* out) {
    if(seat<1||seat>3||!out)return -1;
    memset(out,0,sizeof *out);
    HSD_GObj* object=Player_GetEntity(seat);if(!object)return -2;
    Fighter* fp=object->user_data;if(!fp)return 0;
    SmbFighterState state;memset(&state,0,sizeof state);smb_campaign_fighter_state(fp,&state);
    out->version=SMB_MAGIC;out->character=Player_GetPlayerCharacter(seat);
    if(fp->kind==7)out->character=19;else if(fp->kind==19)out->character=18;
    out->costume=Player_GetCostumeId(seat);
    out->model=state.model;
    out->kind=state.kind;out->motion=state.motion;out->grounded=state.grounded;out->caps=state.caps;
    out->frame=state.frame;out->percent=state.percent;
    memcpy(out->position,state.position,sizeof out->position);
    memcpy(out->facing,state.facing,sizeof out->facing);
    memcpy(out->velocity,state.velocity,sizeof out->velocity);
    HSD_GObj* partner=Player_GetEntityAtIndex(seat,1);
    if(partner&&(!((Fighter*)partner->user_data)->is_sleeping||npc_sleeping[seat][1]==partner)) {
        out->partner_active=1;smb_campaign_fighter_state(partner->user_data,&out->partner);
    }
    return 0;
}
int smb_embed_campaign_remote(unsigned seat,const SmbRemote* state) {
    if(seat<1||seat>3)return -1;
    if((!state||!state->version)&&!remote_active[seat]&&Player_GetEntity(seat)) {
        /* Ownership can move away while this machine still owns a genuine
         * CPU. The generic proxy remover only retires proxy-owned seats. */
        for(unsigned sub=0;sub<2;sub++) {HSD_GObj* object=Player_GetEntityAtIndex(seat,sub);if(object)m3d_ExitLocal(object->user_data);}
        smb_native_clear_npc_items(seat);ifStatus_802F6788(seat);
        if(Player_GetEntity(seat))Player_80031EBC(seat);
        Player_InitOrResetPlayer(seat);
    }
    int result=smb_native_remote_apply(seat,state,1);
    if(!result) {
        if(state&&state->version){encounter_visible|=1u<<seat;memset(npc_sleeping[seat],0,sizeof npc_sleeping[seat]);}
        else {encounter_visible&=~(1u<<seat);memset(npc_sleeping[seat],0,sizeof npc_sleeping[seat]);}
    }
    return result;
}
int smb_embed_campaign_release(unsigned seat) {
    if(seat<1||seat>3)return -1;
    if(!remote_active[seat])return 0;
    return smb_native_remote_release(seat);
}
int smb_native_actor_retire_partner(unsigned seat,unsigned sub) {
    if(seat<1||seat>3||sub!=1)return -1;
    HSD_GObj* object=Player_GetEntityAtIndex(seat,sub);if(!object)return 0;
    Fighter* fp=object->user_data;if(fp->is_sleeping||smb_native_remote_controlled(fp))return 0;
    smb_native_grab_interrupt(fp);m3d_ExitLocal(fp);ftCo_800BFD04(object);
    npc_sleeping[seat][sub]=NULL;return 1;
}
