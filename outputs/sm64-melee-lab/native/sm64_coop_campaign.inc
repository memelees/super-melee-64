/* One encounter roster belongs to the whole level/act. The lowest global
 * player ID owns its simulation; other areas never create duplicate NPCs. */
static void smb_npc_water_capture(void);
static void smb_coop_campaign_promote(const SmbCampaign*);
static void smb_coop_campaign_promote_stage(const SmbCampaign*);
static struct {unsigned used,touched;SmbCampaign state;} coop_campaign_cache[64];
/* Poses belong to loaded areas; defeated opponents belong to the whole star
 * attempt. Sharing this ledger prevents a door or a split party resurrecting
 * an opponent that another player has already knocked out. */
static struct {unsigned used,key,act;SmbCampaign state;} coop_campaign_progress[64];
static unsigned coop_campaign_clock,coop_campaign_sequence;
static unsigned coop_campaign_level=~0u,coop_campaign_area=~0u,coop_campaign_act=~0u;
static unsigned coop_campaign_owner=~0u,coop_campaign_applied=~0u;
static unsigned coop_campaign_role,coop_campaign_spawn_pending,coop_campaign_spawn_request[3];
static unsigned coop_campaign_spawn_level,coop_campaign_spawn_area,coop_campaign_spawn_act;
static unsigned coop_campaign_spawn_last[16];
static int smb_coop_campaign_spawn_scene(void) {
    return coop_campaign_spawn_level==(unsigned)gCurrLevelNum&&coop_campaign_spawn_area==(unsigned)gCurrAreaIndex&&
        coop_campaign_spawn_act==(unsigned)gCurrActStarNum;
}
static int smb_coop_campaign_same_scene(const struct NetworkPlayer* peer) {
    return peer&&peer->connected&&gNetworkPlayerLocal&&
        peer->currLevelNum==gNetworkPlayerLocal->currLevelNum&&
        peer->currAreaIndex==gNetworkPlayerLocal->currAreaIndex&&
        peer->currActNum==gNetworkPlayerLocal->currActNum;
}
static unsigned smb_coop_campaign_owner_for(unsigned level,unsigned area,unsigned act) {
    (void)area;
    unsigned owner=~0u;
    for(unsigned i=0;i<16;i++) {
        const struct NetworkPlayer* peer=&gNetworkPlayers[i];
        if(peer->connected&&(unsigned)peer->currLevelNum==level&&
           (unsigned)peer->currActNum==act&&peer->globalIndex<owner)
            owner=peer->globalIndex;
    }
    return owner;
}
static int smb_coop_campaign_authoritative(void) {
    if(gNetworkType==NT_NONE)return 1;
    if(!gNetworkPlayerLocal)return 0;
    return smb_coop_campaign_owner_for(gCurrLevelNum,gCurrAreaIndex,gCurrActStarNum)==gNetworkPlayerLocal->globalIndex;
}
static unsigned smb_coop_campaign_stage_key(const SmbCampaign* state) {
    return state->course?state->course:1000u+state->level;
}
static SmbCampaign* smb_coop_campaign_stage(const SmbCampaign* state,int create) {
    unsigned key=smb_coop_campaign_stage_key(state),free_slot=64;
    for(unsigned i=0;i<64;i++) {
        if(coop_campaign_progress[i].used&&coop_campaign_progress[i].key==key&&coop_campaign_progress[i].act==state->act)
            return &coop_campaign_progress[i].state;
        if(!coop_campaign_progress[i].used)free_slot=i;
    }
    if(!create||free_slot==64)return NULL;
    coop_campaign_progress[free_slot].used=1;coop_campaign_progress[free_slot].key=key;coop_campaign_progress[free_slot].act=state->act;
    return &coop_campaign_progress[free_slot].state;
}
/* A manual round may wrap wave 2 back to wave 0. Its epoch outranks every KO
 * and transport sequence from the previous round. Story waves remain ordered
 * within epoch zero; a manual epoch has exactly one immutable wave identity. */
static int smb_coop_campaign_order(const SmbCampaign* a,const SmbCampaign* b) {
    if(a->round_serial!=b->round_serial)return a->round_serial>b->round_serial?1:-1;
    if(a->wave==b->wave)return 0;
    if(a->round_serial)return -1;
    return a->wave>b->wave?1:-1;
}
static int smb_coop_campaign_progress_merge(SmbCampaign* state) {
    SmbCampaign* progress=smb_coop_campaign_stage(state,1);if(!progress)return 0;
    int order=progress->version==SMB_MAGIC?smb_coop_campaign_order(state,progress):1;
    if(order>0){*progress=*state;return 1;}
    if(order<0)return 0;
    unsigned count=state->count>progress->count?state->count:progress->count;
    unsigned incoming_defeated=state->defeated_mask,retained_defeated=progress->defeated_mask;
    for(unsigned i=0;i<3;i++) {
        unsigned bit=1u<<i;
        /* The serial identifies both KOs and deliberate manual replacements.
         * Only a newer life may clear a KO; late packets from the previous
         * life cannot retire its replacement or resurrect the old fighter. */
        if(state->ko_serial[i]>progress->ko_serial[i]&&!(state->defeated_mask&bit))retained_defeated&=~bit;
        else if(state->ko_serial[i]<progress->ko_serial[i]) {
            incoming_defeated=(incoming_defeated&~bit)|(retained_defeated&bit);
            state->fighter[i]=progress->fighter[i];state->cpu_level[i]=progress->cpu_level[i];
            memcpy(state->npc_air[i],progress->npc_air[i],sizeof state->npc_air[i]);
            memcpy(state->npc_air_hurt[i],progress->npc_air_hurt[i],sizeof state->npc_air_hurt[i]);
            unsigned airbits=3u<<(i*2);state->npc_air_valid=(state->npc_air_valid&~airbits)|(progress->npc_air_valid&airbits);
            state->active_mask=(state->active_mask&~bit)|(progress->active_mask&bit);
        }
    }
    unsigned defeated=(incoming_defeated|retained_defeated)&((1u<<count)-1);
    /* Keep the latest surviving percentages and identities for an authority
     * transfer into a different area. Their old world-space poses are not
     * valid in the new geometry, so promotion projects only persistent data. */
    if(state->authority!=progress->authority||(int32_t)(state->sequence-progress->sequence)>0) {
        progress->authority=state->authority;progress->sequence=state->sequence;
        progress->level=state->level;progress->area=state->area;progress->mode=state->mode;
        memcpy(progress->fighter,state->fighter,sizeof progress->fighter);memcpy(progress->cpu_level,state->cpu_level,sizeof progress->cpu_level);
        memcpy(progress->npc_air,state->npc_air,sizeof progress->npc_air);progress->npc_air_valid=state->npc_air_valid;
        memcpy(progress->npc_air_hurt,state->npc_air_hurt,sizeof progress->npc_air_hurt);
    }
    for(unsigned i=0;i<3;i++) {
        if(state->ko_serial[i]>progress->ko_serial[i]) {
            progress->ko_serial[i]=state->ko_serial[i];memcpy(progress->ko_position[i],state->ko_position[i],sizeof progress->ko_position[i]);
            progress->ko_star_mask=(progress->ko_star_mask&~(1u<<i))|(state->ko_star_mask&(1u<<i));
        }
        state->ko_serial[i]=progress->ko_serial[i];memcpy(state->ko_position[i],progress->ko_position[i],sizeof state->ko_position[i]);
    }
    progress->count=count;progress->defeated_mask=defeated;progress->active_mask=state->active_mask&~defeated;
    if(state->waves>progress->waves)progress->waves=state->waves;
    state->defeated_mask=defeated&((1u<<state->count)-1);state->active_mask&=~state->defeated_mask;state->ko_star_mask=progress->ko_star_mask;
    return 1;
}
static void smb_coop_campaign_round_base(unsigned* serial,unsigned* wave) {
    SmbCampaign identity={0};identity.course=gCurrCourseNum;identity.level=gCurrLevelNum;identity.act=gCurrActStarNum;
    SmbCampaign* progress=smb_coop_campaign_stage(&identity,0);
    if(progress&&progress->version==SMB_MAGIC&&progress->round_serial>*serial) {
        *serial=progress->round_serial;*wave=progress->wave;
    }
}
static void smb_coop_campaign_progress_apply(void) {
    SmbCampaign identity={0};identity.course=gCurrCourseNum;identity.level=gCurrLevelNum;identity.act=gCurrActStarNum;
    SmbCampaign* progress=smb_coop_campaign_stage(&identity,0);
    if(!progress||progress->version!=SMB_MAGIC||!smb_coop_campaign_authoritative())return;
    if(encounter_course!=gCurrCourseNum||(!gCurrCourseNum&&encounter_key!=1000u+(unsigned)gCurrLevelNum))return;
    if(progress->round_serial>encounter_round_serial) {
        if(progress->level==(unsigned)gCurrLevelNum&&progress->area==(unsigned)gCurrAreaIndex)smb_coop_campaign_promote(progress);
        else smb_coop_campaign_promote_stage(progress);
        return;
    }
    if(progress->round_serial!=encounter_round_serial)return;
    if(!encounter_round_serial&&progress->wave>encounter_wave&&progress->wave<encounter_waves) {
        encounter_wave=progress->wave;smb_encounter_wave();
    }
    if(progress->wave!=encounter_wave)return;
    unsigned defeated=progress->defeated_mask&((1u<<encounter_count)-1),fresh=defeated&~encounter_defeated;
    for(unsigned i=0;i<3;i++)if(fresh&(1u<<i)) {
        embedded_campaign_remote(i+1,NULL);npc_ready[i]=0;
        fprintf(stderr,"SMB COOP STAGE DEFEATED course=%d act=%d slot=%u source_area=%u\n",gCurrCourseNum,gCurrActStarNum,i+1,progress->area);
    }
    encounter_defeated|=defeated;
    /* Local KOs happen after the transport pass. Never roll their newly
     * incremented effect revision back to the last exported snapshot. */
    for(unsigned i=0;i<3;i++)if(progress->ko_serial[i]>npc_ko_serial[i]) {
        npc_ko_serial[i]=progress->ko_serial[i];memcpy(npc_ko_position[i],progress->ko_position[i],sizeof npc_ko_position[i]);
        npc_ko_star_mask=(npc_ko_star_mask&~(1u<<i))|(progress->ko_star_mask&(1u<<i));
    }
    if(!encounter_round_serial&&encounter_count&&encounter_defeated==((1u<<encounter_count)-1)&&encounter_wave+1<encounter_waves&&!encounter_next_wave)
        encounter_next_wave=frames+90;
}
static SmbCampaign* smb_coop_campaign_cached(unsigned level,unsigned area,unsigned act,int create) {
    unsigned selected=64,oldest=~0u;
    for(unsigned i=0;i<64;i++) {
        if(coop_campaign_cache[i].used&&coop_campaign_cache[i].state.level==level&&
           coop_campaign_cache[i].state.area==area&&coop_campaign_cache[i].state.act==act) {
            coop_campaign_cache[i].touched=++coop_campaign_clock;return &coop_campaign_cache[i].state;
        }
        if(!coop_campaign_cache[i].used){selected=i;oldest=0;}
        else if(oldest&&coop_campaign_cache[i].touched<oldest){selected=i;oldest=coop_campaign_cache[i].touched;}
    }
    if(!create||selected==64)return NULL;
    memset(&coop_campaign_cache[selected],0,sizeof coop_campaign_cache[selected]);
    coop_campaign_cache[selected].used=1;coop_campaign_cache[selected].touched=++coop_campaign_clock;
    return &coop_campaign_cache[selected].state;
}
static int smb_coop_campaign_valid(const SmbCampaign* state) {
    if(!state||state->version!=SMB_MAGIC||state->count>3||state->waves>3||
       !state->waves||state->wave>=state->waves||state->mode>2||state->level>64||
       state->area>8||state->act>7||state->course>25||state->next_wave_delay>180||
       (state->round_serial&&state->next_wave_delay))return 0;
    unsigned mask=(1u<<state->count)-1;
    if(state->npc_air_valid&~63u)return 0;
    for(unsigned i=0;i<3;i++)for(unsigned sub=0;sub<2;sub++)
        if(state->npc_air[i][sub]>0x880||!isfinite(state->npc_air_hurt[i][sub])||
           state->npc_air_hurt[i][sub]<0||state->npc_air_hurt[i][sub]>1e7f)return 0;
    if((state->active_mask|state->defeated_mask)&~mask)return 0;
    if(state->active_mask&state->defeated_mask)return 0;
    for(unsigned i=0;i<3;i++) {
        if((state->active_mask&(1u<<i))&&(state->fighter[i].version!=SMB_MAGIC||state->cpu_level[i]<1||state->cpu_level[i]>9))return 0;
        for(unsigned k=0;k<3;k++)if(!isfinite(state->ko_position[i][k])||fabsf(state->ko_position[i][k])>10000)return 0;
    }
    return 1;
}
void smb_coop_campaign_send(SmbCampaign* state) {
    memset(state,0,sizeof *state);
    if(!embedded_step||!embedded_campaign_snapshot||!smb_coop_campaign_authoritative()||!gCurrentArea)return;
    /* Transport precedes the normal air update. Include hits from the last
     * native tick without advancing air a second time or losing them if the
     * snapshot is immediately followed by an encounter owner handoff. */
    smb_npc_water_capture();
    state->version=SMB_MAGIC;state->sequence=++coop_campaign_sequence;
    state->authority=gNetworkPlayerLocal?gNetworkPlayerLocal->globalIndex:0;
    state->level=gCurrLevelNum;state->area=gCurrAreaIndex;state->act=gCurrActStarNum;state->course=gCurrCourseNum;
    state->key=encounter_key?encounter_key:(gCurrCourseNum?(unsigned)gCurrCourseNum:1000u+(unsigned)gCurrLevelNum);
    state->wave=encounter_wave;state->waves=encounter_waves?encounter_waves:1;
    state->round_serial=encounter_round_serial;
    state->count=smb_npc_count();state->defeated_mask=encounter_defeated&((1u<<state->count)-1);
    const char* difficulty=getenv("SMB_DIFFICULTY");
    state->mode=difficulty&&!strcmp(difficulty,"none")?2:smb_impossible()?1:0;
    state->next_wave_delay=!encounter_round_serial&&encounter_next_wave&&encounter_next_wave>frames?encounter_next_wave-frames:0;
    memcpy(state->npc_air,npc_air,sizeof state->npc_air);state->npc_air_valid=npc_air_valid;
    memcpy(state->npc_air_hurt,npc_air_hurt,sizeof state->npc_air_hurt);
    memcpy(state->ko_serial,npc_ko_serial,sizeof state->ko_serial);
    memcpy(state->ko_position,npc_ko_position,sizeof state->ko_position);state->ko_star_mask=npc_ko_star_mask;
    for(unsigned i=0;i<state->count;i++) {
        SmbNpc npc={0};
        if(!embedded_npc(i+1,NULL,&npc))state->cpu_level[i]=npc.level;
        if(state->defeated_mask&(1u<<i))continue;
        if(!embedded_campaign_snapshot(i+1,&state->fighter[i])&&state->fighter[i].version==SMB_MAGIC&&npc_ready[i]&&!npc.suspended)state->active_mask|=1u<<i;
    }
    if(!smb_coop_campaign_progress_merge(state)){memset(state,0,sizeof *state);return;}
    *smb_coop_campaign_cached(state->level,state->area,state->act,1)=*state;
}
static void smb_coop_campaign_spawned(void) {
    /* Commit the new life before the next progress-application pass. A warp
     * or authority transfer immediately after F7 then sees its real identity. */
    SmbCampaign current;smb_coop_campaign_send(&current);
}
void smb_coop_campaign_receive(unsigned sender,const SmbCampaign* state) {
    if(sender>=16||!gNetworkPlayers[sender].connected||!smb_coop_campaign_valid(state))return;
    const struct NetworkPlayer* peer=&gNetworkPlayers[sender];
    if(state->authority!=peer->globalIndex||state->authority!=smb_coop_campaign_owner_for(state->level,state->area,state->act))return;
    if((unsigned)peer->currLevelNum!=state->level||(unsigned)peer->currAreaIndex!=state->area||(unsigned)peer->currActNum!=state->act)return;
    SmbCampaign* previous=smb_coop_campaign_cached(state->level,state->area,state->act,1);
    if(previous->version==SMB_MAGIC&&!smb_coop_campaign_order(state,previous)&&previous->authority==state->authority&&(int32_t)(state->sequence-previous->sequence)<=0)return;
    SmbCampaign merged=*state;if(!smb_coop_campaign_progress_merge(&merged))return;*previous=merged;
}
static void smb_coop_campaign_mode(unsigned mode) {
    const char* received=mode==2?"none":mode==1?"impossible":"default";
    const char* current=getenv("SMB_DIFFICULTY");
    if(!current||strcmp(current,received))SDL_setenv("SMB_DIFFICULTY",received,1);
}
static void smb_coop_campaign_present(const SmbCampaign* state,int effects) {
    smb_coop_campaign_mode(state->mode);
    for(unsigned i=0;i<3;i++) {
        if(effects&&state->ko_serial[i]!=npc_ko_serial[i]&&(state->defeated_mask&(1u<<i))&&embedded_npc_ko)
            embedded_npc_ko(i+1,state->ko_position[i],(state->ko_star_mask>>i)&1);
        embedded_campaign_remote(i+1,(state->active_mask&(1u<<i))?&state->fighter[i]:NULL);
        if((state->active_mask&(1u<<i))&&state->cpu_level[i]>=1&&state->cpu_level[i]<=9&&embedded_npc_configure) {
            SmbNpc npc={0};
            if(!embedded_npc(i+1,NULL,&npc)&&npc.level!=state->cpu_level[i])
                embedded_npc_configure(i+1,state->cpu_level[i],state->count>1&&state->mode!=1?.7f:1.f);
        }
        npc_ready[i]=(state->active_mask>>i)&1;
    }
    encounter_count=state->count;encounter_defeated=state->defeated_mask;
    encounter_wave=state->wave;encounter_waves=state->waves;
    encounter_round_serial=state->round_serial;encounter_next_wave=0;
    encounter_level=state->level;encounter_act=state->act;
    manual_npcs=state->round_serial||(state->mode==2&&state->count);
    if(manual_npcs){manual_course=state->course;manual_level=state->level;manual_act=state->act;}
    encounter_key=state->key?state->key:(state->course?state->course:1000u+state->level);encounter_course=state->course;
    memcpy(npc_ko_serial,state->ko_serial,sizeof npc_ko_serial);memcpy(npc_ko_position,state->ko_position,sizeof npc_ko_position);
    npc_ko_star_mask=state->ko_star_mask;
    memcpy(npc_air,state->npc_air,sizeof npc_air);npc_air_valid=state->npc_air_valid;
    memcpy(npc_air_hurt,state->npc_air_hurt,sizeof npc_air_hurt);
}
static void smb_coop_campaign_promote(const SmbCampaign* state) {
    /* Rebuild the authored wave tables, then resume the transferred native
     * positions and damage. This also preserves manual additions to a scene. */
    fprintf(stderr,"SMB COOP CAMPAIGN PROMOTE BEGIN owner=%u level=%u area=%u act=%u\n",
        gNetworkPlayerLocal?gNetworkPlayerLocal->globalIndex:0,state->level,state->area,state->act);
    for(unsigned i=1;i<=3;i++)embedded_campaign_release(i);
    SDL_setenv("SMB_COURSE_ENEMIES","1",1);
    smb_coop_campaign_mode(state->mode);
    manual_npcs=state->round_serial||(state->mode==2&&state->count);
    if(state->mode==2&&!state->count) {
        if(embedded_encounter)embedded_encounter(0,NULL);
        encounter_count=encounter_next_wave=0;encounter_round_serial=state->round_serial;
        encounter_level=state->level;encounter_act=state->act;return;
    }
    smb_encounter_enter(gCurrCourseNum);
    encounter_wave=state->wave;
    if(encounter_wave<encounter_waves)smb_encounter_wave();
    encounter_count=state->count;encounter_defeated=state->defeated_mask;encounter_waves=state->waves;
    encounter_round_serial=state->round_serial;encounter_level=state->level;encounter_act=state->act;
    if(manual_npcs){manual_course=state->course;manual_level=state->level;manual_act=state->act;}
    encounter_key=state->key?state->key:(state->course?state->course:1000u+state->level);encounter_course=state->course;
    memcpy(npc_ko_serial,state->ko_serial,sizeof npc_ko_serial);memcpy(npc_ko_position,state->ko_position,sizeof npc_ko_position);
    npc_ko_star_mask=state->ko_star_mask;
    memcpy(npc_air,state->npc_air,sizeof npc_air);npc_air_valid=state->npc_air_valid;
    memcpy(npc_air_hurt,state->npc_air_hurt,sizeof npc_air_hurt);
    for(unsigned i=0;i<3;i++) {
        if(state->active_mask&(1u<<i)) {
            embedded_campaign_remote(i+1,&state->fighter[i]);embedded_campaign_release(i+1);npc_ready[i]=1;
            if(embedded_npc_configure)embedded_npc_configure(i+1,state->cpu_level[i],state->count>1&&state->mode!=1?.7f:1.f);
            npc_pending[i]=npc_deferred[i]=npc_launched[i]=npc_transition_ko[i]=npc_sampled[i]=0;
            npc_last_reset[i]=frames;
        } else if(i<state->count&&!(state->defeated_mask&(1u<<i))) {
            npc_ready[i]=0;
            if(state->fighter[i].version==SMB_MAGIC) {
                embedded_campaign_remote(i+1,&state->fighter[i]);embedded_campaign_release(i+1);
                if(embedded_npc_configure)embedded_npc_configure(i+1,state->cpu_level[i]?state->cpu_level[i]:9,state->count>1&&state->mode!=1?.7f:1.f);
                if(embedded_npc_wait)embedded_npc_wait(i+1);
            }
            npc_pending[i]=1;npc_due[i]=UINT32_MAX;npc_anchor_ready[i]=0;
        } else {embedded_campaign_remote(i+1,NULL);npc_ready[i]=0;}
    }
    encounter_next_wave=!state->round_serial&&state->next_wave_delay?frames+state->next_wave_delay:0;
    fprintf(stderr,"SMB COOP CAMPAIGN PROMOTED owner=%u level=%u area=%u act=%u wave=%u count=%u defeated=%u round=%u\n",
        gNetworkPlayerLocal?gNetworkPlayerLocal->globalIndex:0,state->level,state->area,state->act,state->wave,state->count,state->defeated_mask,state->round_serial);
    for(unsigned i=0;i<state->count;i++)if(state->active_mask&(1u<<i)) {
        SmbRemote resumed={0};SmbNpc npc={0};
        if(embedded_campaign_snapshot&&!embedded_campaign_snapshot(i+1,&resumed)&&embedded_npc&&!embedded_npc(i+1,NULL,&npc))
            fprintf(stderr,"SMB COOP CAMPAIGN RESUMED seat=%u character=%u percent=%.2f level=%u active=1\n",i+1,resumed.character,resumed.percent,npc.level);
    }
}
static void smb_coop_campaign_project_pose(SmbFighterState* pose,const float position[3],float facing_x,float facing_z) {
    /* Root positions, velocities, floor contacts, airborne state, animation
     * motion/frame and attachment poses all refer to the old area's geometry.
     * The new area resumes from a plain fall; the scheduler finds a safe floor. */
    pose->motion=29;pose->frame=0;pose->grounded=0;pose->caps&=3;
    memcpy(pose->position,position,sizeof pose->position);memset(pose->velocity,0,sizeof pose->velocity);
    pose->facing[0]=facing_x;pose->facing[1]=0;pose->facing[2]=facing_z;
}
static void smb_coop_campaign_promote_stage(const SmbCampaign* progress) {
    SmbCampaign projected=*progress;projected.level=gCurrLevelNum;projected.area=gCurrAreaIndex;projected.act=gCurrActStarNum;
    projected.active_mask=0;
    float position[3]={gMarioStates[0].pos[0]*.1f,gMarioStates[0].pos[1]*.1f,gMarioStates[0].pos[2]*.1f};
    float facing_x=sins(gMarioStates[0].faceAngle[1]),facing_z=coss(gMarioStates[0].faceAngle[1]);
    for(unsigned i=0;i<3;i++)if(projected.fighter[i].version==SMB_MAGIC) {
        SmbRemote* fighter=&projected.fighter[i];SmbFighterState primary={0};
        primary.kind=fighter->kind;primary.percent=fighter->percent;primary.caps=fighter->caps;
        smb_coop_campaign_project_pose(&primary,position,facing_x,facing_z);
        fighter->motion=primary.motion;fighter->frame=primary.frame;fighter->grounded=0;fighter->caps=primary.caps;
        memcpy(fighter->position,primary.position,sizeof fighter->position);memcpy(fighter->velocity,primary.velocity,sizeof fighter->velocity);memcpy(fighter->facing,primary.facing,sizeof fighter->facing);
        if(fighter->partner_active)smb_coop_campaign_project_pose(&fighter->partner,position,facing_x,facing_z);
    }
    smb_coop_campaign_promote(&projected);
    fprintf(stderr,"SMB COOP CAMPAIGN AREA HANDOFF source_area=%u area=%d retained_damage=1 safe_respawn=1\n",progress->area,gCurrAreaIndex);
}
static int smb_coop_campaign_spawn_valid(const uint32_t request[3]) {
    if(!request)return 0;
    if(request[0]==UINT32_MAX)return !request[1]&&!request[2];
    return request[0]<=63&&request[1]<=31&&request[2]>=1&&request[2]<=9;
}
static int smb_coop_campaign_spawn_dispatch(unsigned sender,const uint32_t request[3]) {
    if(request[0]==UINT32_MAX)return smb_host_spawn_round_at(&gMarioStates[sender]);
    return smb_host_spawn_enemy(request[0],request[1],request[2]);
}
static void smb_coop_campaign_update(void) {
    if(!embedded_step||!embedded_campaign_remote||!embedded_campaign_release||!gCurrentArea)return;
    if(coop_campaign_spawn_pending&&!smb_coop_campaign_spawn_scene()) {
        coop_campaign_spawn_pending=0;fprintf(stderr,"SMB COOP SPAWN CANCEL reason=scene\n");
    }
    unsigned owner=smb_coop_campaign_owner_for(gCurrLevelNum,gCurrAreaIndex,gCurrActStarNum);
    int authoritative=smb_coop_campaign_authoritative();
    int changed=coop_campaign_level!=(unsigned)gCurrLevelNum||coop_campaign_area!=(unsigned)gCurrAreaIndex||
        coop_campaign_act!=(unsigned)gCurrActStarNum||coop_campaign_owner!=owner;
    int new_stage=coop_campaign_level!=(unsigned)gCurrLevelNum||coop_campaign_act!=(unsigned)gCurrActStarNum;
    SmbCampaign* state=smb_coop_campaign_cached(gCurrLevelNum,gCurrAreaIndex,gCurrActStarNum,0);
    SmbCampaign identity={0};identity.course=gCurrCourseNum;identity.level=gCurrLevelNum;identity.act=gCurrActStarNum;
    SmbCampaign* progress=smb_coop_campaign_stage(&identity,0);
    int newer_progress=progress&&progress->version==SMB_MAGIC&&state&&state->version==SMB_MAGIC&&smb_coop_campaign_order(progress,state)>0;
    if(changed) {
        unsigned old_role=coop_campaign_role;
        coop_campaign_level=gCurrLevelNum;coop_campaign_area=gCurrAreaIndex;coop_campaign_act=gCurrActStarNum;coop_campaign_owner=owner;
        coop_campaign_role=authoritative;coop_campaign_applied=~0u;
        if(authoritative&&state&&state->version==SMB_MAGIC&&!newer_progress)smb_coop_campaign_promote(state);
        else if(authoritative) {
            for(unsigned i=1;i<=3;i++)embedded_campaign_release(i);
            if(!old_role||new_stage) {
                if(progress&&progress->version==SMB_MAGIC)smb_coop_campaign_promote_stage(progress);
                else {encounter_course=-1;encounter_key=0;manual_npcs=encounter_round_serial=0;encounter_level=encounter_act=-1;}
            }
        }
        else {
            for(unsigned i=1;i<=3;i++)embedded_campaign_remote(i,NULL);
            encounter_count=0;memset(npc_ready,0,sizeof npc_ready);
        }
        fprintf(stderr,"SMB COOP CAMPAIGN ROLE owner=%u local=%u authority=%d level=%d area=%d act=%d\n",
            owner,gNetworkPlayerLocal?gNetworkPlayerLocal->globalIndex:~0u,authoritative,gCurrLevelNum,gCurrAreaIndex,gCurrActStarNum);
    }
    if(authoritative) {
        if(coop_campaign_spawn_pending) {
            coop_campaign_spawn_pending=0;
            int result=smb_coop_campaign_spawn_dispatch(0,coop_campaign_spawn_request);
            fprintf(stderr,"SMB COOP SPAWN LOCAL character=%u costume=%u level=%u result=%d\n",coop_campaign_spawn_request[0],coop_campaign_spawn_request[1],coop_campaign_spawn_request[2],result);
        }
        smb_coop_campaign_progress_apply();return;
    }
    if(!state||state->version!=SMB_MAGIC||state->authority!=owner)return;
    /* An area's cached pose must never resurrect a previous manual epoch.
     * Wait for the new owner's pose in that area; the ledger remains intact. */
    if(newer_progress) {
        for(unsigned i=1;i<=3;i++)embedded_campaign_remote(i,NULL);
        encounter_count=0;memset(npc_ready,0,sizeof npc_ready);return;
    }
    if(progress&&progress->version==SMB_MAGIC) {
        unsigned defeated=progress->round_serial==state->round_serial&&progress->wave==state->wave?progress->defeated_mask:0;
        unsigned fresh=defeated&~state->defeated_mask;
        if(fresh){state->defeated_mask|=fresh;state->active_mask&=~fresh;coop_campaign_applied=~0u;}
    }
    if(coop_campaign_applied==state->sequence)return;
    smb_coop_campaign_present(state,coop_campaign_applied!=~0u);coop_campaign_applied=state->sequence;
    static unsigned mirror_logged,mirror_last;
    if(mirror_logged<3||frames-mirror_last>=120) {
        fprintf(stderr,"SMB COOP CAMPAIGN MIRROR sequence=%u owner=%u active=%u defeated=%u count=%u\n",
            state->sequence,state->authority,state->active_mask,state->defeated_mask,state->count);
        mirror_logged++;mirror_last=frames;
    }
}
static int smb_coop_campaign_request_spawn(unsigned character,unsigned costume,unsigned level) {
    const uint32_t request[3]={character,costume,level};
    if(!smb_coop_campaign_spawn_valid(request))return -1;
    coop_campaign_spawn_request[0]=character;coop_campaign_spawn_request[1]=costume;coop_campaign_spawn_request[2]=level;
    coop_campaign_spawn_level=gCurrLevelNum;coop_campaign_spawn_area=gCurrAreaIndex;coop_campaign_spawn_act=gCurrActStarNum;
    coop_campaign_spawn_pending=1;
    fprintf(stderr,"SMB COOP SPAWN REQUEST character=%u costume=%u level=%u\n",character,costume,level);return 0;
}
static int smb_coop_campaign_request_round(void) {
    /* Geometry for a different area is not loaded by this encounter's owner.
     * Do not queue a request that cannot possibly be placed near its sender. */
    unsigned owner=smb_coop_campaign_owner_for(gCurrLevelNum,gCurrAreaIndex,gCurrActStarNum);
    for(unsigned i=0;i<16;i++)if(gNetworkPlayers[i].connected&&gNetworkPlayers[i].globalIndex==owner&&!smb_coop_campaign_same_scene(&gNetworkPlayers[i]))return -2;
    return smb_coop_campaign_request_spawn(UINT32_MAX,0,0);
}
int smb_coop_campaign_take_spawn(uint32_t out[3]) {
    if(!coop_campaign_spawn_pending)return 0;
    coop_campaign_spawn_pending=0;
    if(!smb_coop_campaign_spawn_scene())return 0;
    if(smb_coop_campaign_authoritative()) {
        smb_coop_campaign_spawn_dispatch(0,coop_campaign_spawn_request);return 0;
    }
    memcpy(out,coop_campaign_spawn_request,sizeof coop_campaign_spawn_request);return 1;
}
void smb_coop_campaign_spawn_receive(unsigned sender,const uint32_t request[3]) {
    if(sender>=16||!request||!smb_coop_campaign_authoritative()||!smb_coop_campaign_same_scene(&gNetworkPlayers[sender])||
       !smb_coop_campaign_spawn_valid(request))return;
    if(coop_campaign_spawn_last[sender]&&frames-coop_campaign_spawn_last[sender]<30)return;
    coop_campaign_spawn_last[sender]=frames;
    int result=smb_coop_campaign_spawn_dispatch(sender,request);
    if(request[0]==UINT32_MAX)fprintf(stderr,"SMB COOP ROUND ACCEPT sender=%u serial=%u result=%d\n",sender,encounter_round_serial,result);
    fprintf(stderr,"SMB COOP SPAWN ACCEPT sender=%u character=%u costume=%u level=%u result=%d\n",sender,request[0],request[1],request[2],result);
}
