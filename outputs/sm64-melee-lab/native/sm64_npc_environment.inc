/* Course CPUs have no hidden Mario objects. Resolve only hostile world
 * contacts here; pickups, doors, stars and save progression belong to humans. */
#include "object_fields.h"
#include "object_constants.h"
#include "behavior_data.h"
#include "model_ids.h"
#include "game/object_helpers.h"
static struct {
    unsigned dead,hit_until,lava_until,last_reset,life,kind,initialized;
    float quicksand;
} npc_environment[4][2];
static unsigned npc_environment_scene=~0u;
static int smb_npc_environment_dead(unsigned slot) {
    return slot>=1&&slot<=3&&npc_environment[slot][0].dead;
}
static void smb_npc_environment_kill(unsigned slot,unsigned sub,const char* reason) {
    if(slot<1||slot>3||sub>1||!smb_npc_authoritative()||npc_environment[slot][sub].dead)return;
    if(sub&&embedded_actor_world_ko&&embedded_actor_world_ko(slot,sub)<=0)return;
    npc_environment[slot][sub].dead=1;
    fprintf(stderr,"SMB NPC ENVIRONMENT KO slot=%u sub=%u reason=%s ledger=%u\n",slot,sub,reason?reason:"world",!sub);
}
static SmbHit smb_npc_environment_hit(float damage,unsigned element,unsigned angle,unsigned base) {
    SmbHit hit={0};hit.damage=damage;hit.element=element;hit.kb_angle=angle;
    hit.growth=100;hit.base=base;hit.radius=1;hit.severity=1;hit.sound=1;return hit;
}
static void smb_host_npc_environment(struct MarioState* player) {
    (void)player;
    if(!smb_npc_authoritative()||!embedded_actor_state||!embedded_actor_world_hit)return;
    unsigned scene=((unsigned)gCurrLevelNum<<16)|((unsigned)gCurrAreaIndex<<8)|(unsigned)gCurrActNum;
    if(scene!=npc_environment_scene){memset(npc_environment,0,sizeof npc_environment);npc_environment_scene=scene;}
    static unsigned last_frame=~0u;if(last_frame==frames)return;last_frame=frames;
    for(unsigned slot=1;slot<=3;slot++)for(unsigned sub=0;sub<2;sub++) {
        unsigned i=slot-1;
        if(slot>smb_npc_count()||!npc_ready[i]||(encounter_defeated&(1u<<i)))continue;
        SmbFighterState state={0};if(embedded_actor_state(slot,sub,&state))continue;
        typeof(npc_environment[0][0])* env=&npc_environment[slot][sub];
        if(!env->initialized||env->last_reset!=npc_last_reset[i]||env->life!=npc_ko_serial[i]||env->kind!=state.kind) {
            memset(env,0,sizeof *env);env->initialized=1;env->last_reset=npc_last_reset[i];env->life=npc_ko_serial[i];env->kind=state.kind;
        }
        if(env->dead)continue;
        float x=state.position[0]*10,y=state.position[1]*10,z=state.position[2]*10;
        if(find_poison_gas_level(x,z)>y+80&&frames>=env->hit_until) {
            SmbHit gas=smb_npc_environment_hit(2,0,80,10);memcpy(gas.start,state.position,12);memcpy(gas.end,state.position,12);
            if(embedded_actor_world_hit(slot,sub,&gas,1)>0)env->hit_until=frames+30;
        }
        struct Surface* floor=NULL;float floor_y=find_floor(x,y+100,z,&floor);
        int touching=floor&&y<=floor_y+40&&y>=floor_y-80;
        if(touching&&(floor->type==SURFACE_DEATH_PLANE||floor->type==SURFACE_INSTANT_QUICKSAND||floor->type==SURFACE_INSTANT_MOVING_QUICKSAND)) {
            smb_npc_environment_kill(slot,sub,"fatal_floor");continue;
        }
        if(touching&&(floor->type==SURFACE_DEEP_QUICKSAND||floor->type==SURFACE_DEEP_MOVING_QUICKSAND)&&state.grounded) {
            env->quicksand+=1.f;
            if(env->quicksand>=160){smb_npc_environment_kill(slot,sub,"deep_quicksand");continue;}
        } else env->quicksand=0;
        if(touching&&floor->type==SURFACE_BURNING&&state.velocity[1]<=.1f&&frames>=env->lava_until) {
            SmbHit hit=smb_npc_environment_hit(24,1,80,70);hit.source=0x70000000u|floor->type;
            memcpy(hit.start,state.position,sizeof hit.start);memcpy(hit.end,state.position,sizeof hit.end);
            if(embedded_actor_world_hit(slot,sub,&hit,3)>0){env->lava_until=frames+45;env->hit_until=frames+15;}
        }
        if(frames<env->hit_until)continue;
        for(unsigned object_index=0;object_index<OBJECT_POOL_CAPACITY;object_index++) {
            struct Object* object=&gObjectPool[object_index];
            if(!(object->activeFlags&ACTIVE_FLAG_ACTIVE)||object->oIntangibleTimer!=0||object->oHeldState!=HELD_FREE||
               (object->oInteractStatus&(INT_STATUS_WAS_ATTACKED|INT_STATUS_ATTACKED_MARIO)))continue;
            unsigned type=object->oInteractType;
            unsigned hostile=INTERACT_DAMAGE|INTERACT_BOUNCE_TOP|INTERACT_BOUNCE_TOP2|INTERACT_HIT_FROM_BELOW|
                INTERACT_MR_BLIZZARD|INTERACT_SNUFIT_BULLET|INTERACT_SHOCK|INTERACT_FLAME|
                INTERACT_KOOPA|INTERACT_CLAM_OR_BUBBA|INTERACT_BULLY|INTERACT_TORNADO|INTERACT_STRONG_WIND|INTERACT_WHIRLPOOL;
            if(!(type&hostile)&&!((type&INTERACT_GRABBABLE)&&(object->oInteractionSubtype&INT_SUBTYPE_GRABS_MARIO)))continue;
            if(object->oInteractionSubtype&INT_SUBTYPE_HOLDABLE_NPC)continue;
            float radius=object->hitboxRadius,height=object->hitboxHeight,bottom=object->oPosY-object->hitboxDownOffset;
            if(radius<=0||height<=0||hypotf(x-object->oPosX,z-object->oPosZ)>radius+400||y<bottom-400||y>bottom+height+400)continue;
            if((type&INTERACT_FLAME)&&find_water_level(x,z)>y+50)continue;
            if((type&INTERACT_WHIRLPOOL)||((type&INTERACT_CLAM_OR_BUBBA)&&(object->oInteractionSubtype&INT_SUBTYPE_EATS_MARIO))) {
                /* Eating needs a real contact before a fatal world KO. */
                SmbHit touch=smb_npc_environment_hit(0,0,45,0);touch.source=object_index+1;
                touch.start[0]=touch.end[0]=object->oPosX*.1f;touch.start[2]=touch.end[2]=object->oPosZ*.1f;
                touch.start[1]=bottom*.1f;touch.end[1]=(bottom+height)*.1f;touch.radius=radius*.1f;
                if(embedded_actor_world_hit(slot,sub,&touch,12)>0){smb_npc_environment_kill(slot,sub,(type&INTERACT_WHIRLPOOL)?"whirlpool":"eaten");break;}
                continue;
            }
            float damage=fmaxf(0,object->oDamageOrCoinValue)*8;
            if(damage==0&&!(type&(INTERACT_BULLY|INTERACT_TORNADO|INTERACT_STRONG_WIND|INTERACT_GRABBABLE)))continue;
            SmbHit hit=smb_npc_environment_hit(damage,(type&INTERACT_FLAME)?1:(type&INTERACT_SHOCK)?2:0,(type&INTERACT_TORNADO)?80:45,
                (object->oInteractionSubtype&INT_SUBTYPE_BIG_KNOCKBACK)?90:(type&INTERACT_BULLY)?45:60);
            hit.source=object_index+1;hit.start[0]=hit.end[0]=object->oPosX*.1f;hit.start[2]=hit.end[2]=object->oPosZ*.1f;
            hit.start[1]=bottom*.1f;hit.end[1]=(bottom+height)*.1f;hit.radius=radius*.1f;
            int result=embedded_actor_world_hit(slot,sub,&hit,4);
            if(result>0) {
                env->hit_until=frames+30;
                if(result==1)object->oInteractStatus|=INT_STATUS_INTERACTED|INT_STATUS_ATTACKED_MARIO;
                fprintf(stderr,"SMB NPC ENVIRONMENT OBJECT slot=%u sub=%u object=%u type=%08x damage=%.2f result=%d cooldown=30\n",slot,sub,object_index,type,damage,result);break;
            }
        }
    }
}
/* A real SM64 Goomba is held against a live native CPU, proving that contact
 * uses its hurt capsules and cannot repeatedly drain damage every frame. */
static void smb_host_npc_lava_test(struct MarioState* m) {
    static unsigned phase,entered,bounced;static float lava_y;
    if(gCurrLevelNum!=LEVEL_LLL) {
        if(!phase){extern void initiate_warp(s16,s16,s16,s32);initiate_warp(LEVEL_LLL,1,0x0a,0);active=0;phase=1;}
        return;
    }
    if(!entered){entered=frames;return;}
    if(phase<2&&frames-entered>10&&active&&embedded_actor_state) {
        float point[3]={0};int found=0;
        for(unsigned i=0;i<(unsigned)gSurfacesAllocated&&!found;i++) {
#ifdef SMB_COOP_CAMPAIGN
            struct Surface* triangle=smb_coop_surface(i);
#else
            struct Surface* triangle=&sSurfacePool[i];
#endif
            if(!triangle||triangle->type!=SURFACE_BURNING||triangle->normal.y<.7f)continue;
            const float weights[4][3]={{.333333f,.333333f,.333334f},{.6f,.2f,.2f},{.2f,.6f,.2f},{.2f,.2f,.6f}};
            for(unsigned sample=0;sample<4&&!found;sample++) {
                float x=triangle->vertex1[0]*weights[sample][0]+triangle->vertex2[0]*weights[sample][1]+triangle->vertex3[0]*weights[sample][2];
                float y=triangle->vertex1[1]*weights[sample][0]+triangle->vertex2[1]*weights[sample][1]+triangle->vertex3[1]*weights[sample][2];
                float z=triangle->vertex1[2]*weights[sample][0]+triangle->vertex2[2]*weights[sample][1]+triangle->vertex3[2]*weights[sample][2];
                struct Surface* floor=NULL;float fy=find_floor(x,y+100,z,&floor);
                if(!floor||floor->type!=SURFACE_BURNING||fabsf(fy-y)>5)continue;
                struct Surface* ceiling=NULL;float cy=find_ceil(x,fy+20,z,&ceiling);if(ceiling&&cy-fy<800)continue;
                point[0]=x*.1f;point[1]=(fy+15)*.1f;point[2]=z*.1f;lava_y=fy;found=1;
            }
        }
        if(!found)return;
        SmbEncounter roster={0};roster.character[0]=0;
        if(embedded_encounter(1,&roster))return;
        manual_npcs=1;manual_course=gCurrCourseNum;manual_level=gCurrLevelNum;
        encounter_count=1;encounter_defeated=0;encounter_next_wave=0;SDL_setenv("SMB_CPU_COUNT","1",1);
        SmbNpc cpu;embedded_npc_configure(1,1,.1f);if(embedded_npc(1,point,&cpu))return;
        npc_ready[0]=1;npc_pending[0]=0;npc_last_reset[0]=frames;smb_npc_water_reset(1);phase=2;
        fprintf(stderr,"SMB NPC ENV TEST LAVA FLOOR level=%d type=%u x=%.2f y=%.2f z=%.2f\n",gCurrLevelNum,SURFACE_BURNING,point[0]*10,lava_y,point[2]*10);
    }
    if(phase==2||phase==3) {
        SmbFighterState cpu;int live=!embedded_actor_state(1,0,&cpu)&&npc_ready[0]&&!(encounter_defeated&1);
        if(phase==2&&live&&cpu.percent>=24&&cpu.velocity[1]>0) {
            bounced=frames;phase=3;
            fprintf(stderr,"SMB NPC ENV TEST LAVA BOUNCE pass=1 percent=%.2f vy=%.3f live=1\n",cpu.percent,cpu.velocity[1]);
        }
        if(phase==3&&frames-bounced>=12) {
            int pass=live&&cpu.percent>=24&&cpu.position[1]*10>lava_y+80;
            fprintf(stderr,"SMB NPC ENV TEST LAVA SURVIVES pass=%d live=%d percent=%.2f height=%.2f frames=%u\n",pass,live,live?cpu.percent:0,live?cpu.position[1]*10-lava_y:0,frames-bounced);phase=4;
        }
    }
    (void)m;
}
static void smb_host_npc_environment_test(struct MarioState* m) {
    if(!getenv("SMB_SMOKE")||!getenv("SMB_SCENARIO"))return;
    const char* test=getenv("SMB_SCENARIO");if(!strcmp(test,"npc-lava")){smb_host_npc_lava_test(m);return;}
    if(strcmp(test,"npc-environment"))return;
    static unsigned phase,first_frame;static float first_percent;static struct Object* goomba;
    if(!phase&&frames>=20&&active&&embedded_encounter&&embedded_npc&&embedded_actor_state) {
        SmbEncounter roster={0};roster.character[0]=14;
        if(embedded_encounter(1,&roster))return;
        manual_npcs=1;manual_course=gCurrCourseNum;manual_level=gCurrLevelNum;
        encounter_count=1;encounter_defeated=0;encounter_next_wave=0;SDL_setenv("SMB_CPU_COUNT","1",1);
        float point[3]={m->pos[0]*.1f,m->pos[1]*.1f+1,m->pos[2]*.1f-25};SmbNpc cpu;
        embedded_npc_configure(1,1,.1f);if(embedded_npc(1,point,&cpu))return;
        npc_ready[0]=1;npc_last_reset[0]=frames;smb_npc_water_reset(1);
        goomba=spawn_object_abs_with_rot(gMarioObject,0,MODEL_GOOMBA,bhvGoomba,point[0]*10,point[1]*10,point[2]*10,0,0,0);
        if(!goomba)return;goomba->parentObj=goomba;phase=1;
    }
    if(phase==1||phase==2) {
        SmbFighterState cpu;if(embedded_actor_state(1,0,&cpu))return;
        goomba->oPosX=cpu.position[0]*10;goomba->oPosY=cpu.position[1]*10;goomba->oPosZ=cpu.position[2]*10;
        goomba->oIntangibleTimer=0;goomba->oInteractStatus=0;
        if(phase==1&&cpu.percent>0){first_frame=frames;first_percent=cpu.percent;phase=2;}
        if(phase==2&&frames-first_frame>=20) {
            fprintf(stderr,"SMB NPC ENV TEST GOOMBA pass=%d cooldown_pass=%d percent=%.2f after=%.2f frames=%u\n",first_percent>=8,fabsf(cpu.percent-first_percent)<.01f,first_percent,cpu.percent,frames-first_frame);
            goomba->activeFlags=ACTIVE_FLAG_DEACTIVATED;phase=3;
        }
    }
}
