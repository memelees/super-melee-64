/* Transport rides Coop Deluxe's normal player packet: its entity ownership,
 * level/area filtering, doors, objects and save synchronization stay intact. */
#include "pc/network/network.h"
#include "pc/network/network_player.h"
#include "level_table.h"
static SmbRemote coop_peers[16];
int smb_host_spawn_enemy(unsigned,unsigned,unsigned);
static void smb_coop_transport_update(void);
static void smb_coop_campaign_update(void);
static void smb_coop_test_prepare(struct MarioState*);
static int smb_coop_test_input(struct MarioState*,SmbInput*);
void smb_coop_send(SmbRemote* state) {
    memset(state,0,sizeof *state);if(!embedded_step||snapshot.magic!=SMB_MAGIC||gMarioStates[0].health<0x100)return;
    state->version=SMB_MAGIC;state->character=snapshot.character;
    /* Zelda and Sheik are native external forms, not a shared model ID. */
    if(snapshot.kind==7)state->character=19;else if(snapshot.kind==19)state->character=18;
    state->costume=snapshot.costume;state->model=snapshot.model;state->partner_active=snapshot.partner_active;state->partner=snapshot.partner;
    state->kind=snapshot.kind;state->motion=snapshot.motion;state->grounded=snapshot.grounded;
    state->frame=fmaxf(0,snapshot.animation);state->percent=snapshot.percent;
    for(unsigned k=0;k<3;k++)state->position[k]=gMarioStates[0].pos[k]*.1f;
    memcpy(state->facing,snapshot.facing,sizeof state->facing);
    state->velocity[0]=snapshot.vx;state->velocity[1]=snapshot.vy;state->velocity[2]=snapshot.vz;
    state->caps=((gMarioStates[0].flags&MARIO_METAL_CAP)?1:0)|((gMarioStates[0].flags&MARIO_VANISH_CAP)?2:0)|(gMarioStates[0].action==ACT_FLYING?4:0)|(snapshot.shielding?8:0);
    if(state->partner_active&&(state->caps&4))state->partner.caps|=4;
    if(gMarioStates[0].action==ACT_FLYING||(gMarioStates[0].action&ACT_FLAG_RIDING_SHELL)){state->facing[0]=sins(gMarioStates[0].faceAngle[1]);state->facing[1]=0;state->facing[2]=coss(gMarioStates[0].faceAngle[1]);}
}
void smb_coop_receive(unsigned local,const SmbRemote* state) {
    if(local>0&&local<16)coop_peers[local]=*state;
}
static void smb_coop_update(void) {
    if(!embedded_remote)return;
    smb_coop_campaign_update();
    smb_coop_transport_update();
    static unsigned tick;
    if(++tick%120==0){unsigned count=0;for(unsigned i=0;i<16;i++)if(gNetworkPlayers[i].connected)count++;
        fprintf(stderr,"SMB COOP CONNECTIONS count=%u type=%u capacity=16\n",count,gNetworkType);}

    for(unsigned i=1;i<16;i++) {
        struct NetworkPlayer* peer=&gNetworkPlayers[i];
        int visible=peer->connected&&gNetworkPlayerLocal&&peer->currLevelNum==gNetworkPlayerLocal->currLevelNum&&
            peer->currAreaIndex==gNetworkPlayerLocal->currAreaIndex&&peer->currActNum==gNetworkPlayerLocal->currActNum;
        const SmbRemote* state=visible&&coop_peers[i].version==SMB_MAGIC?&coop_peers[i]:NULL;
        int result=embedded_remote(i,state);
        if(result){static unsigned errors;if(errors++<20)fprintf(stderr,"SMB COOP REMOTE ERROR peer=%u code=%d kind=%u motion=%u\n",i,result,coop_peers[i].kind,coop_peers[i].motion);}
    }
}
int smb_coop_hidden(struct Object* object) {
    if(!embedded_step||object==gMarioStates[0].marioObj)return 0;
    for(unsigned i=1;i<16;i++)if(object==gMarioStates[i].marioObj)return coop_peers[i].version==SMB_MAGIC;
    return 0;
}

static void smb_coop_input(SmbInput* command) {
    if(!getenv("SMB_SMOKE")||!getenv("SMB_COOP_TEST"))return;
    unsigned tick=frames%120;
    if(frames>=240){command->stick_y=tick>=20&&tick<45?.65f:0;command->stick_x=0;command->camera_yaw=(frames/120%4)*1.5707963268f;command->buttons=tick==30?0x400:tick==35?0x100:0;}
    if(frames%30==0)fprintf(stderr,"SMB COOP OWNED frame=%u x=%.3f z=%.3f yaw=%.3f input=%.2f\n",frames,snapshot.x,snapshot.z,command->camera_yaw,command->stick_y);
}

/* Integration fixtures still use each real game process and real packets. */
static const Collision coop_test_floor[]={COL_INIT(),COL_VERTEX_INIT(4),COL_VERTEX(-6000,0,-6000),COL_VERTEX(-6000,0,6000),COL_VERTEX(6000,0,6000),COL_VERTEX(6000,0,-6000),COL_TRI_INIT(SURFACE_DEFAULT,2),COL_TRI(0,1,2),COL_TRI(0,2,3),COL_TRI_STOP(),COL_END()};
static void coop_test_floor_tick(void){gCurrentObject->collisionData=(Collision*)coop_test_floor;gCurrentObject->oCollisionDistance=20000;gCurrentObject->oDistanceToMario=0;load_object_collision_model();}
static const BehaviorScript coop_test_floor_behavior[]={((uintptr_t)OBJ_LIST_SURFACE<<16),0x08000000,0x0c000000,(uintptr_t)coop_test_floor_tick,0x09000000};
static void smb_coop_test_prepare(struct MarioState* m) {
    (void)m;const char* test=getenv("SMB_COOP_TEST");if(!getenv("SMB_SMOKE")||!test||!strcmp(test,"movement")||!strcmp(test,"campaign")||!strcmp(test,"rounds"))return;
    static unsigned spawned;if(!spawned&&frames>15&&gCurrentArea){spawn_object_abs_with_rot(gMarioObject,0,MODEL_NONE,coop_test_floor_behavior,0,6000,0,0,0,0);spawned=1;}
}
static int smb_coop_test_input(struct MarioState* m,SmbInput* command) {
    const char* test=getenv("SMB_COOP_TEST");if(!getenv("SMB_SMOKE")||!test||!strcmp(test,"movement"))return 0;
    command->stick_x=command->stick_y=0;command->buttons=0;
    if(!strcmp(test,"rounds")) {
        static unsigned departed;
        if(!gNetworkPlayerLocal||!gNetworkPlayers[1].connected)return 1;
        unsigned global=gNetworkPlayerLocal->globalIndex;
        command->buttons=0x20;
        if(global) {
            int down=(frames>=300&&frames<310)||(frames>=480&&frames<490)||(frames>=850&&frames<860);
            smb_host_key(SDL_SCANCODE_F8,down);
        } else if(!departed&&frames>=650) {
            extern bool warp_to_level(s32,s32,s32);
            departed=1;int result=warp_to_level(LEVEL_BOB,1,1);
            fprintf(stderr,"SMB COOP ROUNDS DEPART global=0 frame=%u result=%d\n",frames,result);
        }
        return 1;
    }
    if(!strcmp(test,"campaign")) {
        static unsigned requested,departed;
        if(!requested&&frames>300&&gNetworkPlayerLocal&&gNetworkPlayerLocal->globalIndex&&gNetworkPlayers[1].connected){requested=1;smb_host_spawn_enemy(38,0,9);}
        if(!departed&&frames>=600&&gNetworkPlayerLocal&&!gNetworkPlayerLocal->globalIndex&&gNetworkPlayers[1].connected) {
            extern bool warp_to_level(s32,s32,s32);
            departed=1;int result=warp_to_level(LEVEL_BOB,1,1);
            fprintf(stderr,"SMB COOP CAMPAIGN DEPART global=0 frame=%u target=%d result=%d\n",frames,LEVEL_BOB,result);
        }
        return 1;
    }
    static unsigned placed,began;
    if(!gNetworkPlayerLocal||!gNetworkPlayers[1].connected||frames<200)return 1;
    unsigned global=gNetworkPlayerLocal->globalIndex;
    if(!placed){placed=1;m->pos[0]=global?65:0;m->pos[1]=6002;m->pos[2]=0;active=0;}
    if(!began){if(coop_peers[1].version!=SMB_MAGIC||coop_peers[1].position[1]<599||coop_peers[1].position[1]>601)return 1;began=frames;}
    unsigned t=frames-began;
    command->camera_yaw=global?1.5707963268f:-1.5707963268f;
    if(t==0){m->pos[0]=global?65:0;m->pos[1]=6002;m->pos[2]=0;active=0;}
    if(!strcmp(test,"projectiles")) {
        if(!global){if(t>=30&&t<65)command->buttons=0x200;if(t>=100&&t<104)command->buttons=0x240;}
    } else if(!strcmp(test,"combat")) {
        if(t==140||t==300){m->pos[0]=global?(t==300?30:65):0;m->pos[1]=6002;m->pos[2]=0;active=0;}
        if(!global){if(t%90>=25&&t%90<27)command->buttons=0x100;}
        else if(t>=150&&t<240)command->buttons=0x20;
        if(!global&&t>=320&&t<323)command->buttons=0x108;
    } else if(!strcmp(test,"grab")) {
        if(!global) {
            static unsigned phase,pummel_at,throw_until;
            if(t>=30&&t<33)command->buttons=0x10;
            /* Native CatchWait=216, CatchAttack=217. Act on the actual
             * tether/pummel completion instead of outwaiting the retail
             * escape timer. Smash Royale uses its up modifier for throws. */
            if(!phase&&t>=45&&snapshot.motion==216){phase=1;pummel_at=t;}
            if(phase==1&&t-pummel_at<2)command->buttons=0x100;
            if(phase==1&&snapshot.motion==217)phase=2;
            if(phase==2&&snapshot.motion==216){phase=3;throw_until=t+4;}
            if(phase==3&&t<throw_until)command->buttons=0x8;
        }
    }
    if(t%15==0)fprintf(stderr,"SMB COOP CASE name=%s global=%u tick=%u x=%.3f y=%.3f z=%.3f percent=%.2f motion=%u\n",test,global,t,snapshot.x,snapshot.y,snapshot.z,snapshot.percent,snapshot.motion);
    return 1;
}
