/* SM64 owns safe rejoining; Melee owns pursuit and combat between transfers. */
#ifdef SMB_COOP_CAMPAIGN
static int smb_coop_campaign_authoritative(void);
static int smb_coop_campaign_request_spawn(unsigned,unsigned,unsigned);
static int smb_coop_campaign_request_round(void);
static void smb_coop_campaign_round_base(unsigned*,unsigned*);
static void smb_coop_campaign_spawned(void);
#endif
static int smb_npc_authoritative(void) {
#ifdef SMB_COOP_CAMPAIGN
    return smb_coop_campaign_authoritative();
#else
    return 1;
#endif
}
static unsigned npc_ready[3],npc_pending[3],npc_last_reset[3],npc_due[3],npc_wait_start[3];
static unsigned npc_ko_serial[3],npc_ko_star_mask;
static float npc_ko_position[3][3];
static float npc_entry[3][3];
static int npc_entry_room[3],npc_anchor_ready[3],npc_room;
static unsigned encounter_count,encounter_defeated,manual_npcs;
static int manual_course=-1,manual_level=-1,manual_act=-1;
static unsigned encounter_wave,encounter_waves,encounter_next_wave,encounter_key;
/* Zero keeps the story's automatic waves. A deliberate round is a new epoch,
 * including when the authored wave index wraps back to zero. */
static unsigned encounter_round_serial;
static int encounter_level=-1,encounter_act=-1;
static uint32_t encounter_roster[3][3],encounter_levels[3][3],encounter_costumes[3][3],encounter_sizes[3];
static int encounter_course=-1;
static unsigned npc_launched[3],npc_deferred[3],npc_transition_ko[3],npc_sampled[3];
static float npc_launch_origin[3][3],npc_previous[3][3];
static int smb_course_enemies(void){return getenv("SMB_COURSE_ENEMIES")!=NULL;}
static int smb_no_melee_enemies(void){const char* mode=getenv("SMB_DIFFICULTY");return !manual_npcs&&mode&&!strcmp(mode,"none");}
static unsigned smb_npc_count(void) {
#ifdef SMB_COOP_CAMPAIGN
    if(!smb_npc_authoritative())return encounter_count;
#endif
    if(smb_no_melee_enemies())return 0;
    if(smb_course_enemies())return encounter_count;
    int n=getenv("SMB_CPU_COUNT")?atoi(getenv("SMB_CPU_COUNT")):0;
    return n<0?0:n>3?3:(unsigned)n;
}
static void smb_host_npc_queue(unsigned reason) {
    for(unsigned i=0;i<smb_npc_count();i++) {
        if(encounter_defeated&(1u<<i))continue;
        SmbNpc state;
        if(npc_ready[i]&&!embedded_npc(i+1,NULL,&state)&&!state.grounded) {
            npc_deferred[i]|=reason;
            if((reason&1)&&(npc_launched[i]||state.damage_flight))npc_transition_ko[i]=1;
            if(getenv("SMB_SCENARIO"))fprintf(stderr,"SMB NPC AIR RECALL DEFERRED frame=%u slot=%u reason=%u damage=%u launched=%u\n",frames,i+1,reason,state.damage_flight,npc_launched[i]);
            continue; /* Never recall an airborne fighter, including during recovery. */
        }
        npc_ready[i]=0;npc_pending[i]|=reason;npc_due[i]=UINT32_MAX;npc_anchor_ready[i]=0;
        if(embedded_npc_wait)embedded_npc_wait(i+1);
    }
}
static int smb_impossible(void){return getenv("SMB_DIFFICULTY")&&!strcmp(getenv("SMB_DIFFICULTY"),"impossible");}
static int smb_npc_act(void) {
#ifdef SMB_COOP_CAMPAIGN
    return gCurrActStarNum;
#else
    return gCurrActNum;
#endif
}
static void smb_encounter_wave(void) {
    encounter_count=encounter_sizes[encounter_wave];encounter_defeated=0;encounter_next_wave=0;
    uint32_t* roster=encounter_roster[encounter_wave];
    SmbEncounter plan={0};memcpy(plan.character,roster,sizeof plan.character);memcpy(plan.costume,encounter_costumes[encounter_wave],sizeof plan.costume);
    if(embedded_encounter&&embedded_encounter(encounter_count,&plan)){fprintf(stderr,"SMB HOST FAIL encounter roster\n");exit(2);}
    for(unsigned i=0;i<encounter_count;i++)if(embedded_npc_configure)embedded_npc_configure(i+1,encounter_levels[encounter_wave][i],encounter_count>1&&!smb_impossible()?.7f:1.f);
    memset(npc_ready,0,sizeof npc_ready);memset(npc_pending,0,sizeof npc_pending);
    memset(npc_launched,0,sizeof npc_launched);memset(npc_deferred,0,sizeof npc_deferred);memset(npc_sampled,0,sizeof npc_sampled);memset(npc_transition_ko,0,sizeof npc_transition_ko);
    for(unsigned i=1;i<=3;i++)smb_npc_water_reset(i);
    npc_room=0;smb_host_npc_queue(1);
    fprintf(stderr,"SMB ENCOUNTER WAVE course=%d key=%u mode=%s wave=%u/%u count=%u external=%u,%u,%u levels=%u,%u,%u\n",encounter_course,encounter_key,smb_impossible()?"impossible":"default",encounter_wave+1,encounter_waves,encounter_count,roster[0],roster[1],roster[2],encounter_levels[encounter_wave][0],encounter_levels[encounter_wave][1],encounter_levels[encounter_wave][2]);
}
/* Authored identities and CPU levels share a source with the recording guide. */
typedef struct {unsigned waves,size[3],roster[3][3],costume[3][3],level[3][3];} SmbStagePlan;
#include "sm64_campaign.inc"
static SmbStagePlan campaign[27];
static void smb_campaign_plan(void) {
    /* A joining peer adopts the encounter owner's mode. Rebuild if it later
     * becomes authority with a different mode from its original local setup. */
    static int ready,planned_mode=-1;int mode=smb_impossible();
    if(ready&&planned_mode==mode)return;ready=1;planned_mode=mode;
    memcpy(campaign,authored_campaign,sizeof campaign);
    if(smb_impossible()) {
        for(unsigned stage=0;stage<27;stage++)for(unsigned w=0;w<campaign[stage].waves;w++)
            for(unsigned i=0;i<campaign[stage].size[w];i++)campaign[stage].level[w][i]=9;
        SmbStagePlan opening={1,{3},{{17,12,5}},{{0,0,0}},{{9,9,9}}};campaign[0]=opening;
    }
    uint64_t seen=0;unsigned coverage=0;
    for(unsigned stage=0;stage<27;stage++) {
        SmbStagePlan* p=&campaign[stage];
        for(unsigned w=0;w<p->waves;w++)for(unsigned i=0;i<p->size[w];i++) {
            unsigned character=p->roster[w][i];
            if(!(seen&(1ull<<character))){seen|=1ull<<character;coverage++;}
            fprintf(stderr,"SMB CAMPAIGN PLAN stage=%u course=%u wave=%u seat=%u external=%u costume_variant=%u level=%u\n",stage,stage<3?0:stage-2,w+1,i+1,character,p->costume[w][i],p->level[w][i]);
        }
    }
    fprintf(stderr,"SMB CAMPAIGN COVERAGE forms=%u expected=33 mode=%s authored=1\n",coverage,smb_impossible()?"impossible":"default");
}
static void smb_encounter_enter(int course) {
    smb_campaign_plan();encounter_course=course;encounter_key=course?course:1000+gCurrLevelNum;
    encounter_wave=encounter_next_wave=encounter_round_serial=0;
    encounter_level=gCurrLevelNum;encounter_act=smb_npc_act();
    unsigned stage=course?course+2:gCurrLevelNum==LEVEL_CASTLE_GROUNDS?0:gCurrLevelNum==LEVEL_CASTLE?1:2;
    memset(encounter_sizes,0,sizeof encounter_sizes);memset(encounter_roster,0,sizeof encounter_roster);memset(encounter_levels,0,sizeof encounter_levels);
    encounter_waves=1;
    if(stage<27&&course!=COURSE_CAKE_END) {
        SmbStagePlan* p=&campaign[stage];encounter_waves=p->waves;
        memcpy(encounter_sizes,p->size,sizeof encounter_sizes);memcpy(encounter_roster,p->roster,sizeof encounter_roster);
        memcpy(encounter_levels,p->level,sizeof encounter_levels);memcpy(encounter_costumes,p->costume,sizeof encounter_costumes);
    }
    smb_encounter_wave();
    fprintf(stderr,"SMB ENCOUNTER ENTER course=%d count=%u external=%u,%u,%u reset_defeated=1 mode=%s key=%u waves=%u\n",course,encounter_count,encounter_roster[0][0],encounter_roster[0][1],encounter_roster[0][2],smb_impossible()?"impossible":"default",encounter_key,encounter_waves);
}
static int smb_npc_knocked_out(unsigned slot,const SmbNpc* npc,struct MarioState* m) {
    if(smb_npc_environment_dead(slot+1))return 1;
    if(npc_transition_ko[slot])return 2;
    if(npc->grounded)npc_launched[slot]=0;
    else if(!npc_launched[slot]&&npc->damage_flight&&npc->knockback>.05f) {
        npc_launched[slot]=1;
        memcpy(npc_launch_origin[slot],npc_sampled[slot]?npc_previous[slot]:npc->position,12);
        fprintf(stderr,"SMB NPC LAUNCH TRACK frame=%u slot=%u origin=%.2f,%.2f,%.2f percent=%.1f\n",frames,slot+1,npc_launch_origin[slot][0],npc_launch_origin[slot][1],npc_launch_origin[slot][2],npc->percent);
    }
    if(getenv("SMB_LOCAL_BLAST_ZONES")&&npc_launched[slot]) {
        float rise=(npc->position[1]-npc_launch_origin[slot][1])*10;
        float travel=hypotf(npc->position[0]-npc_launch_origin[slot][0],npc->position[2]-npc_launch_origin[slot][2])*10;
        if(rise>1000||travel>1800||rise< -1600) {
            fprintf(stderr,"SMB LOCAL BLAST frame=%u slot=%u rise=%.1f travel=%.1f star=%d\n",frames,slot+1,rise,travel,rise>1000);
            return rise>1000?2:1;
        }
    }
    memcpy(npc_previous[slot],npc->position,12);npc_sampled[slot]=1;
    float x=npc->position[0]*10,y=npc->position[1]*10,z=npc->position[2]*10;
    struct Surface* floor=NULL;float fy=find_floor(x,y+100,z,&floor);
    if(floor&&(floor->type==SURFACE_DEATH_PLANE||floor->type==SURFACE_INSTANT_QUICKSAND||floor->type==SURFACE_INSTANT_MOVING_QUICKSAND)&&y<fy+100)return 1;
    /* Course-wide blast bounds, independent of the camera/player position.
     * Recovery remains possible below ledges; walking away is never a KO. */
    float lo[3]={1e9f,1e9f,1e9f},hi[3]={-1e9f,-1e9f,-1e9f};
    for(int i=0;i<gSurfacesAllocated;i++) {
        struct Surface* s=&sSurfacePool[i];
        if(s->type==SURFACE_DEATH_PLANE||s->normal.y<.01f)continue;
        s16* v[3]={s->vertex1,s->vertex2,s->vertex3};
        for(unsigned j=0;j<3;j++)for(unsigned k=0;k<3;k++){lo[k]=fminf(lo[k],v[j][k]);hi[k]=fmaxf(hi[k],v[j][k]);}
    }
    if(lo[0]>hi[0])return !floor&&y<m->pos[1]-3000;
    return x<lo[0]-1000||x>hi[0]+1000||z<lo[2]-1000||z>hi[2]+1000||y<lo[1]-1200||y>hi[1]+2000;
}
static void smb_host_npc_observe(struct MarioState* m,int world_changed) {
    if(!smb_npc_authoritative())return;
    if(!embedded_npc)return;
    if(manual_npcs&&(gCurrCourseNum!=manual_course||gCurrLevelNum!=manual_level||smb_npc_act()!=manual_act)) {
        manual_npcs=0;encounter_round_serial=0;
    }
    static int cleared;
    if(smb_no_melee_enemies()) {
        if(!cleared) {
            /* Remove native CPU seats and their HUD/items, rather than hiding
             * fighters that could still simulate or rejoin in another room. */
            if(embedded_encounter&&embedded_encounter(0,NULL)){fprintf(stderr,"SMB HOST FAIL clear encounters\n");exit(2);}
            encounter_count=encounter_next_wave=encounter_round_serial=0;cleared=1;
            fprintf(stderr,"SMB ENEMIES DISABLED native_cpus=0 sm64_enemies=unchanged\n");
        }
        return;
    }
    cleared=0;
    static int doorway,seen_world;
    if(!manual_npcs&&smb_course_enemies()&&(gCurrCourseNum!=encounter_course||encounter_level!=gCurrLevelNum||encounter_act!=smb_npc_act()||(!gCurrCourseNum&&encounter_key!=1000u+gCurrLevelNum)))smb_encounter_enter(gCurrCourseNum);
    if(world_changed==2){npc_room=0;smb_host_npc_queue(seen_world?1:16);seen_world=1;}
    int room=m->floor?m->floor->room:0;
    if(room>0&&room!=npc_room){npc_room=room;smb_host_npc_queue(2);}
    int entering=m->action==ACT_PULLING_DOOR||m->action==ACT_PUSHING_DOOR||
        m->action==ACT_WARP_DOOR_SPAWN||m->action==ACT_ENTERING_STAR_DOOR||
        m->action==ACT_UNLOCKING_KEY_DOOR||m->action==ACT_UNLOCKING_STAR_DOOR||
        m->action==ACT_TELEPORT_FADE_OUT||m->action==ACT_TELEPORT_FADE_IN;
    if(entering&&!doorway) {
        smb_host_npc_queue(4);
        fprintf(stderr,"SMB NPC TRANSITION frame=%u level=%d area=%d room=%d action=%08x deferred=1\n",frames,gCurrLevelNum,gCurrentArea->index,npc_room,m->action);
    }
    doorway=entering;
    if(respawn_pending)smb_host_npc_queue(8);
}
static int smb_npc_safe_floor(float x,float z,float y,int room,float tolerance,float* point,int* spawn_room) {
    struct Surface* floor=NULL;float py=find_floor(x,y+250,z,&floor);
    if(!floor||floor->normal.y<.7f||floor->type==SURFACE_DEATH_PLANE||fabsf(py-y)>tolerance)return 0;
    if(room>0&&floor->room!=room)return 0;
    struct Surface* ceiling=NULL;float cy=find_ceil(x,py+10,z,&ceiling);
    if(ceiling&&cy-py<200)return 0;
    struct WallCollisionData walls={0};walls.x=x;walls.y=py+5;walls.z=z;walls.offsetY=80;walls.radius=65;
    find_wall_collisions(&walls);if(hypotf(walls.x-x,walls.z-z)>1)return 0;
    point[0]=x*.1f;point[1]=(py+5)*.1f;point[2]=z*.1f;*spawn_room=floor->room;return 1;
}
/* A water surface is an environment boundary, never a replacement floor.
 * Keep the real terrain below a submerged fighter and test a conservative
 * body volume, including tall fighters, before waking its native simulation. */
static int smb_npc_safe_water(float x,float z,float y,int room,float* point,int* spawn_room) {
    const float radius=65,body=320;
    const float offsets[5][2]={{0,0},{65,0},{-65,0},{0,65},{0,-65}};
    int actual_room=0;
    if(!isfinite(x)||!isfinite(y)||!isfinite(z)||!point||!spawn_room)return 0;
    for(unsigned i=0;i<5;i++) {
        float px=x+offsets[i][0],pz=z+offsets[i][1],water=find_water_level(px,pz);
        if(!isfinite(water)||water<= -10000||water-y<body)return 0;
        struct Surface* floor=NULL;float py=find_floor(px,y+body,pz,&floor);
        if(!floor||!isfinite(py)||floor->type==SURFACE_DEATH_PLANE||py>y-20||(room>0&&floor->room!=room))return 0;
        struct Surface* ceiling=NULL;float cy=find_ceil(px,y-20,pz,&ceiling);
        if(ceiling&&cy-y<body)return 0;
        if(!i)actual_room=floor->room;
    }
    const float heights[]={20,160,300};
    for(unsigned i=0;i<3;i++) {
        struct WallCollisionData walls={0};walls.x=x;walls.y=y;walls.z=z;walls.offsetY=heights[i];walls.radius=radius;
        find_wall_collisions(&walls);if(hypotf(walls.x-x,walls.z-z)>1)return 0;
    }
    point[0]=x*.1f;point[1]=y*.1f;point[2]=z*.1f;*spawn_room=actual_room;return 1;
}
static int smb_npc_safe_spawn(float x,float z,float y,int room,float tolerance,float* point,int* spawn_room) {
    float water=find_water_level(x,z);
    /* Lift feet a small amount above an underwater floor. This also permits
     * deliberate rounds when the player is standing on the real seabed. */
    if(water> -10000&&y<=water-350)return smb_npc_safe_water(x,z,y+30,room,point,spawn_room);
    return smb_npc_safe_floor(x,z,y,room,tolerance,point,spawn_room);
}
static int smb_npc_spawn_path(const float* origin,const float* point) {
    float destination[3]={point[0]*10,point[1]*10,point[2]*10};
    float dx=destination[0]-origin[0],dz=destination[2]-origin[2],distance=hypotf(dx,dz);
    float water=find_water_level(destination[0],destination[2]);
    int wet=water> -10000&&destination[1]<=water-320;
    if(wet) {
        /* Every sampled section must contain the body in the same open water
         * passage. Endpoints alone would allow spawning through a shoreline. */
        for(unsigned i=0;i<=4;i++) {
            float t=i*.25f,checked[3];int room;
            if(!smb_npc_safe_water(origin[0]+dx*t,origin[2]+dz*t,
                origin[1]+30+(destination[1]-origin[1]-30)*t,0,checked,&room))return 0;
        }
    }
    unsigned rays=wet?9:1;
    for(unsigned i=0;i<rays;i++) {
        float lateral=wet?((int)(i%3)-1)*65.f:0,height=wet?(i/3==0?20:i/3==1?160:300):120;
        float ox=distance>1?-dz/distance*lateral:0,oz=distance>1?dx/distance*lateral:0;
        Vec3f from={origin[0]+ox,origin[1]+height,origin[2]+oz};
        Vec3f direction={dx,destination[1]-origin[1],dz},hit;
        struct Surface* obstruction=NULL;find_surface_on_ray(from,direction,&obstruction,hit);
        if(obstruction)return 0;
    }
    return 1;
}
static void smb_host_npcs(struct MarioState* m) {
    if(!smb_npc_authoritative())return;
    if(!embedded_npc||smb_no_melee_enemies())return;
    if(!encounter_round_serial&&smb_course_enemies()&&encounter_next_wave&&frames>=encounter_next_wave){encounter_wave++;smb_encounter_wave();}
    for(unsigned i=0;i<smb_npc_count();i++) {
        if(encounter_defeated&(1u<<i))continue;
        SmbNpc npc;if(embedded_npc(i+1,NULL,&npc))continue;
        int ko=npc_ready[i]?(smb_course_enemies()?smb_npc_knocked_out(i,&npc,m):smb_npc_environment_dead(i+1)):0;
        if(ko) {
            encounter_defeated|=1u<<i;npc_ready[i]=0;
            if(embedded_npc_ko) {
                float dx=npc.position[0]*10-m->pos[0],dz=npc.position[2]*10-m->pos[2],d=hypotf(dx,dz);
                float effect[3]={(m->pos[0]+(d>1?dx/d*350:0))*.1f,(m->pos[1]+160)*.1f,(m->pos[2]+(d>1?dz/d*350:0))*.1f};
                memcpy(npc_ko_position[i],effect,sizeof effect);npc_ko_serial[i]++;
                if(ko==2)npc_ko_star_mask|=1u<<i;else npc_ko_star_mask&=~(1u<<i);
                embedded_npc_ko(i+1,effect,ko==2);
            }
            fprintf(stderr,"SMB ENCOUNTER KO course=%d slot=%u kind=%u percent=%.2f defeated=%u count=%u\n",encounter_course,i+1,npc.kind,npc.percent,encounter_defeated,encounter_count);
            if(encounter_defeated==((1u<<encounter_count)-1)) {
                if(!encounter_round_serial&&encounter_wave+1<encounter_waves){encounter_next_wave=frames+90;fprintf(stderr,"SMB ENCOUNTER NEXT course=%d due=%u\n",encounter_course,encounter_next_wave);}
                else fprintf(stderr,"SMB ENCOUNTER CLEARED course=%d\n",encounter_course);
            }
            continue;
        }
        float x=npc.position[0]*10,y=npc.position[1]*10,z=npc.position[2]*10;
        float old_distance=hypotf(x-m->pos[0],z-m->pos[2]);
        struct Surface* floor=NULL;float height=find_floor(x,y+100,z,&floor);
        if(npc_deferred[i]&&npc.grounded) {
            npc_ready[i]=0;npc_pending[i]|=npc_deferred[i];npc_deferred[i]=0;npc_due[i]=UINT32_MAX;npc_anchor_ready[i]=0;embedded_npc_wait(i+1);
        }
        int lost=(!floor&&y<m->pos[1]-1500)||(floor&&floor->type==SURFACE_DEATH_PLANE&&y<height+100)||old_distance>4000;
        if(!npc.grounded||npc.damage_flight||npc_launched[i])lost=0;
        if(smb_course_enemies())lost=0; /* Course opponents never teleport after a retreat. */
        int transition=!npc_ready[i];
        if(transition&&npc_due[i]==UINT32_MAX) {
            npc_wait_start[i]=frames;
            npc_due[i]=frames+((npc_pending[i]&15)?90:0); /* Three seconds of active host gameplay. */
            memcpy(npc_entry[i],m->pos,sizeof npc_entry[i]);npc_entry_room[i]=npc_room;
            fprintf(stderr,"SMB NPC WAIT frame=%u slot=%u due=%u reason=%u\n",frames,i+1,npc_due[i],npc_pending[i]);
        }
        if(transition&&!npc_anchor_ready[i]) {
            float water_point[3];int water_room;
            struct Surface* entry_floor=NULL;
            float py=find_floor(m->pos[0],m->pos[1]+250,m->pos[2],&entry_floor);
            if(smb_npc_safe_water(m->pos[0],m->pos[2],m->pos[1],npc_room,water_point,&water_room)) {
                memcpy(npc_entry[i],m->pos,sizeof npc_entry[i]);npc_entry_room[i]=water_room;npc_anchor_ready[i]=1;
            } else if(entry_floor&&entry_floor->normal.y>=.7f&&entry_floor->type!=SURFACE_DEATH_PLANE) {
                npc_entry[i][0]=m->pos[0];npc_entry[i][1]=py+5;npc_entry[i][2]=m->pos[2];
                npc_entry_room[i]=entry_floor->room;npc_anchor_ready[i]=1;
            }
        }
        if((transition&&frames>=npc_due[i]&&npc_anchor_ready[i])||(!transition&&lost&&frames-npc_last_reset[i]>90)) {
            float point[3],front_dot=0;int found=0,spawn_room=0;
            if(transition) {
                /* After the grace period, place the opponent ahead in the
                 * current view. Never spawn behind a retreating camera/player. */
                float fx=gLakituState.focus[0]-gLakituState.pos[0],fz=gLakituState.focus[2]-gLakituState.pos[2],len=hypotf(fx,fz);
                if(len<1){fx=sins(m->faceAngle[1]);fz=coss(m->faceAngle[1]);len=1;}
                fx/=len;fz/=len;
                const float angles[]={0,.261799f,-.261799f,.523599f,-.523599f,.785398f,-.785398f,1.047198f,-1.047198f};
                const float radii[]={900,700,500,350};
                for(unsigned ring=0;ring<4&&!found;ring++)for(unsigned probe=0;probe<9;probe++) {
                    float angle=angles[(probe+i*3)%9],dx=(fx*cosf(angle)+fz*sinf(angle))*radii[ring],dz=(fz*cosf(angle)-fx*sinf(angle))*radii[ring];
                    if(!smb_npc_safe_spawn(m->pos[0]+dx,m->pos[2]+dz,m->pos[1],npc_room,300,point,&spawn_room))continue;
                    if(!smb_npc_spawn_path(m->pos,point))continue;
                    front_dot=(dx*fx+dz*fz)/radii[ring];found=1;break;
                }
            } else {
                /* Search around the midpoint first. Never fall back to a ring
                 * around the player when the intervening terrain is unsafe. */
                const float fractions[]={.5f,.375f,.625f,.25f,.75f};
                for(unsigned t=0;t<5&&!found;t++)for(unsigned probe=0;probe<17;probe++) {
                    float mix=fractions[t],angle=i*2.0943951f+probe*.7853982f,radius=probe==0?0:probe<9?70:140;
                    float px=x+(m->pos[0]-x)*mix+sinf(angle)*radius;
                    float pz=z+(m->pos[2]-z)*mix+cosf(angle)*radius;
                    float gap=hypotf(px-m->pos[0],pz-m->pos[2]);
                    if(old_distance>4000&&(gap<fmaxf(600,old_distance*.2f)||gap>old_distance*.85f))continue;
                    if(smb_npc_safe_spawn(px,pz,fmaxf(m->pos[1],fminf(y+(m->pos[1]-y)*mix,m->pos[1]+1000)),0,1500,point,&spawn_room)&&smb_npc_spawn_path(m->pos,point)){found=1;break;}
                }
            }
            float percent=npc.percent;
            if(found&&!embedded_npc(i+1,point,&npc)) {
                float gap=hypotf(point[0]*10-m->pos[0],point[2]*10-m->pos[2]);
                float anchor_distance=transition?hypotf(point[0]*10-npc_entry[i][0],point[2]*10-npc_entry[i][2]):0;
                fprintf(stderr,"SMB NPC REJOIN frame=%u slot=%u level=%d area=%d room=%d spawn_room=%d reason=%u distance=%.1f old_distance=%.1f wait_frames=%u percent_before=%.2f percent_after=%.2f catchup=%d anchor_distance=%.1f front_dot=%.3f\n",frames,i+1,gCurrLevelNum,gCurrentArea->index,transition?npc_entry_room[i]:0,spawn_room,npc_pending[i],gap,old_distance,transition?frames-npc_wait_start[i]:0,percent,npc.percent,!transition,anchor_distance,front_dot);
                float water=find_water_level(point[0]*10,point[2]*10);
                if(water> -10000&&point[1]*10<water-70)fprintf(stderr,"SMB NPC WATER PLACE frame=%u slot=%u x=%.1f y=%.1f z=%.1f surface=%.1f\n",frames,i+1,point[0]*10,point[1]*10,point[2]*10,water);
                npc_ready[i]=1;npc_pending[i]=0;npc_last_reset[i]=frames;npc_sampled[i]=npc_launched[i]=0;
            }
        }
        if(frames%30==0)fprintf(stderr,"SMB NPC FRAME frame=%u slot=%u kind=%u level=%u target=%u motion=%u hits=%u percent=%.2f x=%.2f y=%.2f z=%.2f player_percent=%.2f area=%u ready=%u health=%d map_area=%d room=%d suspended=%u\n",frames,i+1,npc.kind,npc.level,npc.target,npc.motion,npc.hitboxes,npc.percent,npc.position[0],npc.position[1],npc.position[2],snapshot.percent,gCurrLevelNum,npc_ready[i],m->health,gCurrentArea->index,npc_room,npc.suspended);
    }
}

/* Follow the stock camera's yaw, obstruction handling and cutscenes. Only its
 * preferred distance, FOV and smoothed focus gain modest combat framing. */
static void smb_combat_camera_update(struct Camera* c) {
    float zoom=0,lift=0;
    int allowed=embedded_step&&!melee_camera_enabled()&&active&&!c->cutscene&&gMarioState&&gMarioState->health>=0x100&&
        (c->mode==CAMERA_MODE_FREE_ROAM||c->mode==CAMERA_MODE_RADIAL||c->mode==CAMERA_MODE_OUTWARD_RADIAL||c->mode==CAMERA_MODE_CLOSE||c->mode==CAMERA_MODE_8_DIRECTIONS||c->mode==CAMERA_MODE_BEHIND_MARIO);
    if(allowed&&embedded_npc)for(unsigned i=0;i<smb_npc_count();i++) {
        if(!npc_ready[i]||(smb_course_enemies()&&(encounter_defeated&(1u<<i))))continue;
        SmbNpc npc;if(embedded_npc(i+1,NULL,&npc)||npc.suspended)continue;
        float dx=npc.position[0]*10-gMarioState->pos[0],dz=npc.position[2]*10-gMarioState->pos[2];
        float height=npc.position[1]*10-gMarioState->pos[1],distance=hypotf(dx,dz);
        if(distance>2200||height< -400||height>1800)continue;
        Vec3f origin={gMarioState->pos[0],gMarioState->pos[1]+100,gMarioState->pos[2]},dir={dx,height,dz},hit;
        struct Surface* wall=NULL;find_surface_on_ray(origin,dir,&wall,hit);if(wall)continue;
        zoom=fmaxf(zoom,fminf(.45f,fmaxf(0,height)/2300+fmaxf(0,distance-650)/5000));
        lift=fmaxf(lift,fminf(250,fmaxf(0,height)*.28f));
    }
    if(!allowed){combat_zoom=combat_lift=0;return;}
    combat_zoom+=(zoom-combat_zoom)*.16f;combat_lift+=(lift-combat_lift)*.18f;
    if(getenv("SMB_SCENARIO")&&frames%10==0&&combat_zoom>.01f)fprintf(stderr,"SMB COMBAT CAMERA frame=%u zoom=%.3f lift=%.1f fov_extra=%.2f\n",frames,combat_zoom,combat_lift,combat_zoom*24);
}

/* Add one chosen fighter without resetting the player's progress or opponents.
 * At capacity the last CPU seat is replaced; the other two keep their state. */
int smb_host_spawn_enemy(unsigned character,unsigned costume,unsigned level) {
#ifdef SMB_COOP_CAMPAIGN
    if(!smb_npc_authoritative())return smb_coop_campaign_request_spawn(character,costume,level);
#endif
    if(!embedded_npc_add||!gMarioState||!active)return -1;
    unsigned count=smb_npc_count(),slot=count<3?count:2;
    for(unsigned i=0;i<count;i++)if(encounter_defeated&(1u<<i)){slot=i;break;}
    struct MarioState* m=gMarioState;float point[3];int room=0,found=0;
    struct Surface* under=NULL;float base=find_floor(m->pos[0],m->pos[1]+100,m->pos[2],&under);
    if(!under||under->type==SURFACE_DEATH_PLANE)base=m->pos[1];
    float water=find_water_level(m->pos[0],m->pos[2]);if(water> -10000&&water>m->pos[1]+70)base=m->pos[1];
    for(unsigned i=0;i<16;i++) {
        float angle=m->faceAngle[1]*(6.28318530718f/65536.f)+i*.39269908f;
        if(smb_npc_safe_spawn(m->pos[0]+sinf(angle)*350,m->pos[2]+cosf(angle)*350,base,npc_room,700,point,&room)&&smb_npc_spawn_path(m->pos,point)){found=1;break;}
    }
    if(!found)return -2;
    if(embedded_npc_add(slot+1,character,costume,level,point))return -3;
    manual_npcs=1;manual_course=gCurrCourseNum;manual_level=gCurrLevelNum;manual_act=smb_npc_act();encounter_count=count>slot?count:slot+1;encounter_defeated&=~(1u<<slot);
    encounter_next_wave=0;npc_ready[slot]=1;npc_pending[slot]=npc_launched[slot]=npc_deferred[slot]=npc_transition_ko[slot]=npc_sampled[slot]=0;
    npc_last_reset[slot]=frames;
    smb_npc_water_reset(slot+1);
    if(!smb_course_enemies()){char value[8];snprintf(value,sizeof value,"%u",encounter_count);SDL_setenv("SMB_CPU_COUNT",value,1);}
#ifdef SMB_COOP_CAMPAIGN
    /* A deliberate manual replacement is a new life for this encounter seat.
     * Its newer revision distinguishes it from delayed packets of the KO. */
    npc_ko_serial[slot]++;smb_coop_campaign_spawned();
#endif
    fprintf(stderr,"SMB MANUAL ENEMY slot=%u character=%u level=%u count=%u\n",slot+1,character,level,encounter_count);return 0;
}

/* A press replaces the encounter with exactly one authored round. Resolve all
 * placements first, so an obstructed room never erases the current opponents. */
static int smb_host_spawn_round_at(struct MarioState* m) {
    if(!smb_npc_authoritative()||!embedded_encounter||!embedded_npc||!m||!active||!gCurrentArea)return -1;
    smb_campaign_plan();
    unsigned stage=gCurrCourseNum?gCurrCourseNum+2:gCurrLevelNum==LEVEL_CASTLE_GROUNDS?0:gCurrLevelNum==LEVEL_CASTLE?1:2;
    if(stage>=27||gCurrCourseNum==COURSE_CAKE_END)return -1;
    const SmbStagePlan* plan=&campaign[stage];
    unsigned serial=encounter_level==gCurrLevelNum&&encounter_act==smb_npc_act()?encounter_round_serial:0;
    unsigned wave=serial?encounter_wave:0;
#ifdef SMB_COOP_CAMPAIGN
    smb_coop_campaign_round_base(&serial,&wave);
#endif
    if(serial==UINT32_MAX||!plan->waves||plan->waves>3)return -4;
    unsigned next=serial?(wave+1)%plan->waves:0,count=plan->size[next];
    if(!count||count>3)return -1;
    for(unsigned i=0;i<3;i++)if(npc_ko_serial[i]==UINT32_MAX)return -4;
    struct Surface* under=NULL;float base=find_floor(m->pos[0],m->pos[1]+250,m->pos[2],&under);
    int room=under?under->room:0;
    if(!under||under->type==SURFACE_DEATH_PLANE)base=m->pos[1];
    float water=find_water_level(m->pos[0],m->pos[2]);if(water> -10000&&water>m->pos[1]+70)base=m->pos[1];
    float points[3][3];int rooms[3]={0};
    const float angles[]={0,.261799f,-.261799f,.523599f,-.523599f,.785398f,-.785398f,1.047198f,-1.047198f};
    const float radii[]={500,700,350};
    float fx=sins(m->faceAngle[1]),fz=coss(m->faceAngle[1]);
    for(unsigned i=0;i<count;i++) {
        int found=0;
        for(unsigned ring=0;ring<3&&!found;ring++)for(unsigned probe=0;probe<9;probe++) {
            float angle=angles[(probe+i*3)%9];
            float dx=(fx*cosf(angle)+fz*sinf(angle))*radii[ring],dz=(fz*cosf(angle)-fx*sinf(angle))*radii[ring];
            if(!smb_npc_safe_spawn(m->pos[0]+dx,m->pos[2]+dz,base,room,700,points[i],&rooms[i]))continue;
            unsigned j=0;for(;j<i;j++)if(hypotf(points[i][0]-points[j][0],points[i][2]-points[j][2])<18.f)break;
            if(j<i)continue;
            if(!smb_npc_spawn_path(m->pos,points[i]))continue;
            found=1;break;
        }
        if(!found) {
            fprintf(stderr,"SMB MANUAL ROUND BLOCKED stage=%u wave=%u seat=%u retained=1\n",stage,next+1,i+1);return -2;
        }
    }
    memcpy(encounter_sizes,plan->size,sizeof encounter_sizes);memcpy(encounter_roster,plan->roster,sizeof encounter_roster);
    memcpy(encounter_costumes,plan->costume,sizeof encounter_costumes);memcpy(encounter_levels,plan->level,sizeof encounter_levels);
    encounter_waves=plan->waves;encounter_wave=next;encounter_round_serial=serial+1;
    encounter_course=gCurrCourseNum;encounter_key=gCurrCourseNum?gCurrCourseNum:1000u+gCurrLevelNum;
    encounter_level=gCurrLevelNum;encounter_act=smb_npc_act();
    manual_npcs=1;manual_course=gCurrCourseNum;manual_level=gCurrLevelNum;manual_act=smb_npc_act();
    if(!smb_course_enemies()){char value[8];snprintf(value,sizeof value,"%u",count);SDL_setenv("SMB_CPU_COUNT",value,1);}
    /* New epochs also revise empty seats: a late KO from a three-fighter round
     * must not reappear after a one-fighter round or its later replacement. */
    for(unsigned i=0;i<3;i++)npc_ko_serial[i]++;
    npc_ko_star_mask=0;memset(npc_ko_position,0,sizeof npc_ko_position);
    smb_encounter_wave();npc_room=room;
    for(unsigned i=0;i<count;i++) {
        SmbNpc npc={0};
        if(embedded_npc(i+1,points[i],&npc)){fprintf(stderr,"SMB MANUAL ROUND FAIL seat=%u\n",i+1);return -3;}
        npc_ready[i]=1;npc_pending[i]=0;npc_due[i]=0;npc_anchor_ready[i]=0;npc_last_reset[i]=frames;
        fprintf(stderr,"SMB MANUAL ROUND SPAWN serial=%u wave=%u slot=%u character=%u costume=%u level=%u x=%.2f y=%.2f z=%.2f room=%d\n",encounter_round_serial,next+1,i+1,plan->roster[next][i],plan->costume[next][i],plan->level[next][i],points[i][0],points[i][1],points[i][2],rooms[i]);
    }
#ifdef SMB_COOP_CAMPAIGN
    smb_coop_campaign_spawned();
#endif
    fprintf(stderr,"SMB MANUAL ROUND serial=%u stage=%u wave=%u/%u count=%u automatic=0\n",encounter_round_serial,stage,next+1,encounter_waves,count);
    return 0;
}
int smb_host_spawn_round(void) {
#ifdef SMB_COOP_CAMPAIGN
    if(!smb_npc_authoritative())return smb_coop_campaign_request_round();
#endif
    return smb_host_spawn_round_at(gMarioState);
}
