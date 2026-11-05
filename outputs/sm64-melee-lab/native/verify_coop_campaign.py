#!/usr/bin/env python3
"""Check encounter ownership, cross-room KO persistence and sender validation."""
from pathlib import Path
import json, subprocess, tempfile

HERE = Path(__file__).resolve().parent
SOURCE = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "bridge_protocol.h"
enum {NT_NONE,NT_CLIENT,NT_SERVER};
struct NetworkPlayer {int connected;unsigned globalIndex;int currLevelNum,currAreaIndex,currActNum;};
static struct NetworkPlayer gNetworkPlayers[16],*gNetworkPlayerLocal;
static int gNetworkType=NT_CLIENT,gCurrLevelNum=9,gCurrAreaIndex=1,gCurrActStarNum=1,gCurrCourseNum=1;
static int area;static void* gCurrentArea=&area;
struct MarioState {float pos[3];int16_t faceAngle[3];};
static struct MarioState gMarioStates[16];
static float sins(int16_t angle){return sinf(angle*(6.28318530718f/65536.f));}
static float coss(int16_t angle){return cosf(angle*(6.28318530718f/65536.f));}
static unsigned frames=100,encounter_key=1,encounter_wave,encounter_waves=2,encounter_count=1;
static int encounter_course=1;
static unsigned encounter_next_wave,encounter_defeated,manual_npcs,encounter_round_serial;
static int encounter_level=9,encounter_act=1,manual_course=-1,manual_level=-1,manual_act=-1;
static unsigned npc_ready[3]={1},npc_ko_serial[3],npc_ko_star_mask;
static uint32_t npc_air_valid,npc_air[3][2];
static float npc_air_hurt[3][2];
static float native_air_damage[3][2];
static unsigned air_capture_calls;
static void smb_npc_water_capture(void){air_capture_calls++;for(unsigned i=0;i<3;i++)for(unsigned sub=0;sub<2;sub++){npc_air_hurt[i][sub]+=native_air_damage[i][sub]*32.f;native_air_damage[i][sub]=0;}}
static unsigned npc_pending[3],npc_deferred[3],npc_launched[3],npc_transition_ko[3],npc_sampled[3],npc_last_reset[3],npc_due[3],npc_anchor_ready[3];
static float npc_ko_position[3][3];
static int embedded_step=1,poses[4],released,accepted,effects,configured_calls;static float percentages[4];static SmbRemote last_pose[4];
static unsigned cpu_levels[4]={9,9,9,9};static char received_mode[16]="default";
static unsigned round_accepted,round_sender;static float round_origin[3];
static const char* harness_getenv(const char* name){return !strcmp(name,"SMB_DIFFICULTY")?received_mode:NULL;}
#define getenv harness_getenv
static int remote_stub(unsigned seat,const SmbRemote* state){poses[seat]=state&&state->version;if(state){percentages[seat]=state->percent;last_pose[seat]=*state;}return 0;}
static int release_stub(unsigned seat){(void)seat;released++;return 0;}
static int snapshot_stub(unsigned seat,SmbRemote* state){memset(state,0,sizeof *state);state->version=SMB_MAGIC;state->character=seat+1;state->kind=seat;state->frame=1;state->percent=47;return 0;}
static int npc_stub(unsigned seat,const float* p,SmbNpc* state){(void)p;memset(state,0,sizeof *state);state->level=cpu_levels[seat];return 0;}
static int wait_stub(unsigned seat){(void)seat;return 0;}
static int configure_stub(unsigned s,unsigned l,float a){(void)a;cpu_levels[s]=l;configured_calls++;return 0;}
static int ko_stub(unsigned s,const float* p,unsigned star){(void)s;(void)p;(void)star;effects++;return 0;}
static int encounter_stub(unsigned n,const SmbEncounter* p){(void)p;encounter_count=n;return 0;}
static int (*embedded_campaign_remote)(unsigned,const SmbRemote*)=remote_stub;
static int (*embedded_campaign_release)(unsigned)=release_stub;
static int (*embedded_campaign_snapshot)(unsigned,SmbRemote*)=snapshot_stub;
static int (*embedded_npc)(unsigned,const float*,SmbNpc*)=npc_stub;
static int (*embedded_npc_wait)(unsigned)=wait_stub;
static int (*embedded_npc_configure)(unsigned,unsigned,float)=configure_stub;
static int (*embedded_npc_ko)(unsigned,const float*,unsigned)=ko_stub;
static int (*embedded_encounter)(unsigned,const SmbEncounter*)=encounter_stub;
static unsigned smb_npc_count(void){return encounter_count;}
static int smb_impossible(void){return !strcmp(received_mode,"impossible");}
static int smb_npc_act(void){return gCurrActStarNum;}
static int SDL_setenv(const char* n,const char* v,int o){(void)o;if(!strcmp(n,"SMB_DIFFICULTY"))snprintf(received_mode,sizeof received_mode,"%s",v);return 0;}
static void smb_encounter_enter(int course){encounter_course=course;encounter_level=gCurrLevelNum;encounter_act=smb_npc_act();encounter_round_serial=0;encounter_wave=0;encounter_waves=2;encounter_count=1;encounter_defeated=0;}
static void smb_encounter_wave(void){encounter_count=1;encounter_defeated=0;encounter_next_wave=0;}
static int smb_host_spawn_enemy(unsigned c,unsigned k,unsigned l){(void)c;(void)k;(void)l;accepted++;return 0;}
static void smb_coop_campaign_round_base(unsigned*,unsigned*);
static void smb_coop_campaign_spawned(void);
/* Placement itself is covered by the real NPC-function verifier. This stub
 * captures the actual dispatch origin and commits the resulting round ledger. */
static int smb_host_spawn_round_at(struct MarioState* mario){
    unsigned serial=encounter_round_serial,wave=encounter_wave;
    smb_coop_campaign_round_base(&serial,&wave);
    round_accepted++;round_sender=(unsigned)(mario-gMarioStates);memcpy(round_origin,mario->pos,sizeof round_origin);
    encounter_round_serial=serial+1;encounter_wave=serial?(wave+1)%2:0;encounter_waves=2;
    encounter_count=1;encounter_defeated=encounter_next_wave=0;npc_ready[0]=1;
    encounter_course=manual_course=gCurrCourseNum;encounter_level=manual_level=gCurrLevelNum;encounter_act=manual_act=smb_npc_act();
    encounter_key=gCurrCourseNum?gCurrCourseNum:1000u+gCurrLevelNum;manual_npcs=1;
    for(unsigned i=0;i<3;i++)npc_ko_serial[i]++;
    smb_coop_campaign_spawned();return 0;
}
#include "sm64_coop_campaign.inc"
static void reset_round_fixture(void) {
    memset(gNetworkPlayers,0,sizeof gNetworkPlayers);memset(gMarioStates,0,sizeof gMarioStates);
    gNetworkPlayers[0]=(struct NetworkPlayer){1,0,9,1,1};gNetworkPlayers[1]=(struct NetworkPlayer){1,1,9,1,1};
    gNetworkPlayerLocal=&gNetworkPlayers[1];gNetworkType=NT_CLIENT;
    gCurrLevelNum=9;gCurrAreaIndex=gCurrActStarNum=gCurrCourseNum=1;frames=100;
    encounter_course=1;encounter_key=1;encounter_wave=encounter_round_serial=0;encounter_waves=2;encounter_count=1;
    encounter_level=9;encounter_act=1;encounter_next_wave=encounter_defeated=manual_npcs=0;
    manual_course=manual_level=manual_act=-1;
    memset(npc_ready,0,sizeof npc_ready);npc_ready[0]=1;
    memset(npc_ko_serial,0,sizeof npc_ko_serial);npc_ko_star_mask=0;memset(npc_ko_position,0,sizeof npc_ko_position);
    memset(npc_pending,0,sizeof npc_pending);memset(npc_deferred,0,sizeof npc_deferred);memset(npc_due,0,sizeof npc_due);
    memset(npc_launched,0,sizeof npc_launched);memset(npc_transition_ko,0,sizeof npc_transition_ko);
    memset(npc_sampled,0,sizeof npc_sampled);memset(npc_last_reset,0,sizeof npc_last_reset);memset(npc_anchor_ready,0,sizeof npc_anchor_ready);
    npc_air_valid=0;memset(npc_air,0,sizeof npc_air);memset(npc_air_hurt,0,sizeof npc_air_hurt);
    memset(native_air_damage,0,sizeof native_air_damage);air_capture_calls=0;
    memset(poses,0,sizeof poses);memset(percentages,0,sizeof percentages);memset(last_pose,0,sizeof last_pose);
    for(unsigned i=0;i<4;i++)cpu_levels[i]=9;
    released=accepted=effects=configured_calls=0;round_accepted=round_sender=0;memset(round_origin,0,sizeof round_origin);
    snprintf(received_mode,sizeof received_mode,"default");
    memset(coop_campaign_cache,0,sizeof coop_campaign_cache);memset(coop_campaign_progress,0,sizeof coop_campaign_progress);
    coop_campaign_clock=coop_campaign_sequence=0;
    coop_campaign_level=coop_campaign_area=coop_campaign_act=coop_campaign_owner=coop_campaign_applied=~0u;
    coop_campaign_role=coop_campaign_spawn_pending=0;memset(coop_campaign_spawn_request,0,sizeof coop_campaign_spawn_request);
    coop_campaign_spawn_level=coop_campaign_spawn_area=coop_campaign_spawn_act=0;
    memset(coop_campaign_spawn_last,0,sizeof coop_campaign_spawn_last);
}
static SmbCampaign round_frame(unsigned serial,unsigned wave,unsigned sequence,unsigned count) {
    SmbCampaign state={0};state.version=SMB_MAGIC;state.authority=0;state.sequence=sequence;
    state.level=9;state.area=1;state.act=1;state.course=1;state.key=1;
    state.round_serial=serial;state.wave=wave;state.waves=2;state.count=count;state.active_mask=(1u<<count)-1;
    state.npc_air_valid=63;
    for(unsigned i=0;i<3;i++) {
        state.ko_serial[i]=serial*10+i;state.cpu_level[i]=5+i;
        state.fighter[i].version=SMB_MAGIC;state.fighter[i].character=i+2;state.fighter[i].kind=i+1;
        state.fighter[i].motion=14;state.fighter[i].frame=1;state.fighter[i].percent=30+i;
        state.fighter[i].position[0]=100+i;state.fighter[i].position[1]=200+i;
        state.npc_air[i][0]=0x400+i*8;state.npc_air[i][1]=0x600+i*8;
        state.npc_air_hurt[i][0]=.25f+i;state.npc_air_hurt[i][1]=.75f+i;
    }
    assert(smb_coop_campaign_valid(&state));return state;
}
static void test_manual_round_epochs(void) {
    reset_round_fixture();
    SmbCampaign old=round_frame(1,1,100,3);old.active_mask=0;old.defeated_mask=7;
    smb_coop_campaign_receive(0,&old);smb_coop_campaign_update();
    assert(encounter_round_serial==1&&encounter_wave==1&&encounter_defeated==7&&!encounter_next_wave);
    /* A fresh epoch beats both a later old wave and a higher transport
     * sequence. Removed third seats carry no old KO into the wrapped round. */
    SmbCampaign wrapped=round_frame(2,0,1,1);
    smb_coop_campaign_receive(0,&wrapped);smb_coop_campaign_update();
    assert(encounter_round_serial==2&&encounter_wave==0&&encounter_count==1&&encounter_defeated==0);
    assert(poses[1]&&!poses[2]&&!poses[3]);
    old.sequence=1000000;for(unsigned i=0;i<3;i++)old.ko_serial[i]=100000+i;
    smb_coop_campaign_receive(0,&old);smb_coop_campaign_update();
    SmbCampaign* cache=smb_coop_campaign_cached(9,1,1,0);
    assert(cache&&cache->round_serial==2&&cache->wave==0&&cache->sequence==1);
    assert(encounter_defeated==0&&npc_ko_serial[0]==wrapped.ko_serial[0]);
    SmbCampaign impossible_wave=wrapped;impossible_wave.wave=1;impossible_wave.sequence=2000000;
    smb_coop_campaign_receive(0,&impossible_wave);smb_coop_campaign_update();
    assert(cache->wave==0&&cache->sequence==1); /* One epoch has one wave identity. */
    unsigned serial=0,wave=1;smb_coop_campaign_round_base(&serial,&wave);assert(serial==2&&wave==0);
    SmbCampaign knocked=wrapped;knocked.sequence=2;knocked.active_mask=0;knocked.defeated_mask=1;knocked.ko_serial[0]++;
    smb_coop_campaign_receive(0,&knocked);smb_coop_campaign_update();
    gNetworkPlayers[0].connected=0;smb_coop_campaign_update();frames+=300;smb_coop_campaign_update();
    assert(smb_coop_campaign_authoritative()&&encounter_round_serial==2&&encounter_wave==0&&encounter_defeated==1&&!encounter_next_wave);
    SmbCampaign outgoing;smb_coop_campaign_send(&outgoing);
    assert(outgoing.round_serial==2&&!outgoing.next_wave_delay);
    SmbCampaign invalid=outgoing;invalid.next_wave_delay=1;assert(!smb_coop_campaign_valid(&invalid));

    /* Independent life revisions cover every seat, including slots which had
     * been empty in the preceding one-fighter manual round. */
    gNetworkPlayers[0].connected=1;
    SmbCampaign trio=round_frame(3,1,200,3);
    smb_coop_campaign_receive(0,&trio);smb_coop_campaign_update();
    SmbCampaign replacement=trio;replacement.sequence++;
    for(unsigned i=0;i<3;i++) {
        replacement.ko_serial[i]++;replacement.fighter[i].character=38-i;
        replacement.fighter[i].percent=70+i;replacement.npc_air[i][0]=0x700+i;
        replacement.npc_air_hurt[i][0]=.125f+i;
    }
    smb_coop_campaign_receive(0,&replacement);smb_coop_campaign_update();
    trio.sequence=replacement.sequence+1;trio.active_mask=0;trio.defeated_mask=7;
    for(unsigned i=0;i<3;i++)trio.npc_air_hurt[i][0]=900;
    smb_coop_campaign_receive(0,&trio);smb_coop_campaign_update();
    assert(encounter_defeated==0&&encounter_count==3);
    for(unsigned i=0;i<3;i++) {
        assert(poses[i+1]&&last_pose[i+1].character==replacement.fighter[i].character);
        assert(npc_ko_serial[i]==replacement.ko_serial[i]&&npc_air[i][0]==replacement.npc_air[i][0]);
        assert(npc_air_hurt[i][0]==replacement.npc_air_hurt[i][0]);
    }
    /* A KO produced after the transport pass keeps its newer effect revision
     * until the following send commits it into the shared progress ledger. */
    gNetworkPlayers[0].connected=0;smb_coop_campaign_update();
    npc_ko_serial[0]++;unsigned local_ko=npc_ko_serial[0];encounter_defeated|=1;npc_ready[0]=0;
    smb_coop_campaign_progress_apply();assert(npc_ko_serial[0]==local_ko&&(encounter_defeated&1));
    smb_coop_campaign_send(&outgoing);assert(outgoing.ko_serial[0]==local_ko&&(outgoing.defeated_mask&1));
    puts("PASS: manual round wrap, immutable epoch waves, stale high-sequence/high-KO rejection, all-seat life revisions, local KO retention and no automatic manual-round progression");
}
static void test_none_round_handoff(void) {
    reset_round_fixture();
    SmbCampaign manual=round_frame(5,0,50,2);manual.mode=2;
    manual.npc_air[0][0]=0x456;manual.npc_air_hurt[0][0]=512.25f;manual.npc_air_hurt[1][1]=.875f;
    smb_coop_campaign_receive(0,&manual);smb_coop_campaign_update();
    assert(manual_npcs&&encounter_count==2&&encounter_round_serial==5&&!strcmp(received_mode,"none"));
    assert(manual_course==1&&manual_level==9&&manual_act==1);
    /* Promotion in another room keeps epoch, identities and fractional air
     * queues while replacing old geometry with a safe pending spawn pose. */
    gCurrAreaIndex=2;gNetworkPlayers[1].currAreaIndex=2;smb_coop_campaign_update();
    gMarioStates[0].pos[0]=100;gMarioStates[0].pos[1]=200;gMarioStates[0].pos[2]=300;
    gNetworkPlayers[0].connected=0;smb_coop_campaign_update();
    assert(smb_coop_campaign_authoritative()&&manual_npcs&&encounter_count==2&&encounter_round_serial==5);
    assert(last_pose[1].character==manual.fighter[0].character&&last_pose[2].character==manual.fighter[1].character);
    assert(last_pose[1].motion==29&&last_pose[1].position[0]==10&&last_pose[1].position[1]==20);
    assert(npc_pending[0]&&npc_pending[1]&&!encounter_next_wave);
    assert(npc_air[0][0]==0x456&&npc_air_hurt[0][0]==512.25f&&npc_air_hurt[1][1]==.875f);
    SmbCampaign outgoing;smb_coop_campaign_send(&outgoing);
    assert(outgoing.mode==2&&outgoing.count==2&&outgoing.round_serial==5&&outgoing.area==2);
    assert(outgoing.npc_air_hurt[0][0]==512.25f&&outgoing.npc_air_hurt[1][1]==.875f);
    puts("PASS: none-mode manual actors and epoch survive cross-area authority handoff with exact fractional air queues");
}
static void test_round_requests(void) {
    reset_round_fixture();
    const uint32_t request[3]={UINT32_MAX,0,0};uint32_t outgoing[3];
    gMarioStates[0].pos[0]=11;gMarioStates[0].pos[1]=22;gMarioStates[0].pos[2]=33;
    gMarioStates[2].pos[0]=987;gMarioStates[2].pos[1]=654;gMarioStates[2].pos[2]=321;
    gNetworkPlayers[2]=(struct NetworkPlayer){1,77,9,1,1};
    assert(!smb_coop_campaign_request_round());assert(smb_coop_campaign_take_spawn(outgoing));
    assert(outgoing[0]==UINT32_MAX&&!outgoing[1]&&!outgoing[2]&&!round_accepted);
    assert(!smb_coop_campaign_take_spawn(outgoing));
    smb_coop_campaign_spawn_receive(2,request);assert(!round_accepted); /* Non-owner cannot execute. */
    gNetworkPlayerLocal=&gNetworkPlayers[0];smb_coop_campaign_spawn_receive(2,request);
    assert(round_accepted==1&&round_sender==2&&round_origin[0]==987&&round_origin[1]==654);
    smb_coop_campaign_spawn_receive(2,request);assert(round_accepted==1);
    frames+=29;smb_coop_campaign_spawn_receive(2,request);assert(round_accepted==1);
    frames++;smb_coop_campaign_spawn_receive(2,request);assert(round_accepted==2);
    const uint32_t malformed[3]={UINT32_MAX,1,0};frames+=40;
    smb_coop_campaign_spawn_receive(2,malformed);smb_coop_campaign_spawn_receive(16,request);assert(round_accepted==2);
    gNetworkPlayers[2].currAreaIndex=2;smb_coop_campaign_spawn_receive(2,request);assert(round_accepted==2);
    /* A client in a separate room cannot ask an owner to place actors using
     * geometry that is absent from the owner's currently loaded area. */
    gNetworkPlayerLocal=&gNetworkPlayers[1];gNetworkPlayers[0].currAreaIndex=2;
    assert(smb_coop_campaign_request_round()== -2&&!coop_campaign_spawn_pending);
    gNetworkPlayers[0].currAreaIndex=1;assert(!smb_coop_campaign_request_round());
    gCurrActStarNum=2;gNetworkPlayers[1].currActNum=2;
    assert(!smb_coop_campaign_take_spawn(outgoing)&&round_accepted==2); /* Scene-bound queue. */
    /* A queued request follows a same-scene authority promotion once, using
     * Mario0 as the local requester's origin instead of a remote seat. */
    reset_round_fixture();gMarioStates[0].pos[0]=123;gMarioStates[0].pos[1]=456;
    assert(!smb_coop_campaign_request_round());gNetworkPlayers[0].connected=0;
    smb_coop_campaign_update();assert(smb_coop_campaign_authoritative()&&round_accepted==1&&round_sender==0);
    assert(round_origin[0]==123&&round_origin[1]==456&&encounter_round_serial==1);
    smb_coop_campaign_update();assert(round_accepted==1&&!smb_coop_campaign_take_spawn(outgoing));
    puts("PASS: reserved round request validation, sender-index origin, rate bound, non-owner routing, scene filtering and single execution on promotion");
}
int main(void) {
    gNetworkPlayers[0]=(struct NetworkPlayer){1,0,9,1,1};
    gNetworkPlayers[1]=(struct NetworkPlayer){1,1,9,1,1};
    for(unsigned i=2;i<16;i++)gNetworkPlayers[i]=(struct NetworkPlayer){1,i,12,(int)(i%3)+1,2};
    assert(smb_coop_campaign_owner_for(12,1,2)==2);
    gNetworkPlayers[2].connected=0;assert(smb_coop_campaign_owner_for(12,3,2)==3);
    for(unsigned i=2;i<16;i++)gNetworkPlayers[i].connected=0;
    gNetworkPlayerLocal=&gNetworkPlayers[0];
    assert(smb_coop_campaign_authoritative());
    npc_air_valid=3;npc_air[0][0]=0x321;npc_air[0][1]=0x678;
    npc_air_hurt[0][0]=193.5f;npc_air_hurt[0][1]=.75f;
    native_air_damage[0][0]=.5f;native_air_damage[0][1]=.25f;
    SmbCampaign state;smb_coop_campaign_send(&state);
    assert(state.npc_air_valid==3&&state.npc_air[0][0]==0x321&&state.npc_air[0][1]==0x678);
    assert(state.npc_air_hurt[0][0]==209.5f&&state.npc_air_hurt[0][1]==8.75f&&air_capture_calls==1);
    assert(native_air_damage[0][0]==0&&native_air_damage[0][1]==0);
    smb_coop_campaign_send(&state);assert(air_capture_calls==2&&state.npc_air_hurt[0][0]==209.5f&&state.npc_air_hurt[0][1]==8.75f);
    assert(state.count==1&&state.active_mask==1&&state.authority==0);
    gNetworkPlayerLocal=&gNetworkPlayers[1];
    assert(!smb_coop_campaign_authoritative());
    state.sequence++;state.cpu_level[0]=7;
    smb_coop_campaign_receive(0,&state);smb_coop_campaign_update();
    assert(encounter_count==1&&poses[1]&&cpu_levels[1]==7&&configured_calls==1);
    assert(npc_air_valid==3&&npc_air[0][0]==0x321);
    assert(npc_air_hurt[0][0]==209.5f&&npc_air_hurt[0][1]==8.75f);
    state.sequence++;smb_coop_campaign_receive(0,&state);smb_coop_campaign_update();assert(configured_calls==1);
    state.sequence++;state.mode=1;state.cpu_level[0]=9;smb_coop_campaign_receive(0,&state);smb_coop_campaign_update();
    assert(cpu_levels[1]==9&&!strcmp(received_mode,"impossible"));
    SmbCampaign forged=state;forged.authority=1;forged.sequence=100;
    smb_coop_campaign_receive(0,&forged);
    assert(smb_coop_campaign_cached(9,1,1,0)->sequence==state.sequence);
    forged=state;forged.active_mask=8;assert(!smb_coop_campaign_valid(&forged));
    forged=state;forged.npc_air[0][0]=0x881;assert(!smb_coop_campaign_valid(&forged));
    forged=state;forged.npc_air_hurt[0][0]=NAN;assert(!smb_coop_campaign_valid(&forged));
    forged=state;forged.npc_air_hurt[0][1]=INFINITY;assert(!smb_coop_campaign_valid(&forged));
    forged=state;forged.npc_air_hurt[0][0]=-.5f;assert(!smb_coop_campaign_valid(&forged));
    forged=state;forged.npc_air_hurt[2][1]=1e7f+1;assert(!smb_coop_campaign_valid(&forged));
    forged=state;forged.npc_air_hurt[2][1]=1e7f;assert(smb_coop_campaign_valid(&forged));
    /* A second room does not elect a second copy of the level roster. */
    gCurrAreaIndex=2;gNetworkPlayers[1].currAreaIndex=2;smb_coop_campaign_update();
    assert(!smb_coop_campaign_authoritative()&&!poses[1]&&encounter_count==0);
    state.sequence++;state.defeated_mask=1;state.active_mask=0;state.ko_serial[0]=1;
    smb_coop_campaign_receive(0,&state);
    /* A stale live snapshot cannot resurrect an enemy across a door. */
    SmbCampaign stale=state;stale.sequence++;stale.defeated_mask=0;stale.active_mask=1;
    smb_coop_campaign_receive(0,&stale);
    assert(smb_coop_campaign_cached(9,1,1,0)->defeated_mask==1);
    gCurrAreaIndex=1;gNetworkPlayers[1].currAreaIndex=1;smb_coop_campaign_update();
    assert(encounter_defeated==1&&!poses[1]);
    /* On owner departure, damage/progress transfer and the roster stays KO'd. */
    gNetworkPlayers[0].connected=0;smb_coop_campaign_update();
    assert(smb_coop_campaign_authoritative()&&encounter_defeated==1&&!poses[1]);
    assert(npc_air_valid==3&&npc_air[0][0]==0x321);
    assert(npc_air_hurt[0][0]==209.5f&&npc_air_hurt[0][1]==8.75f);
    assert(encounter_next_wave==frames+90);
    gNetworkPlayers[2]=(struct NetworkPlayer){1,2,9,1,1};
    uint32_t spawn[3]={38,1,9};smb_coop_campaign_spawn_receive(2,spawn);assert(accepted==1);
    smb_coop_campaign_spawn_receive(2,spawn);assert(accepted==1); /* Rate bound. */
    frames+=40;gNetworkPlayers[2].currAreaIndex=2;smb_coop_campaign_spawn_receive(2,spawn);assert(accepted==1);
    gNetworkPlayers[2].currAreaIndex=1;spawn[2]=10;smb_coop_campaign_spawn_receive(2,spawn);assert(accepted==1);
    gNetworkPlayers[0].connected=1;
    assert(!smb_coop_campaign_request_spawn(38,1,9));
    uint32_t outgoing[3];assert(smb_coop_campaign_take_spawn(outgoing)&&outgoing[0]==38&&outgoing[2]==9);
    assert(!smb_coop_campaign_take_spawn(outgoing));
    /* A queued request never follows the requester into another star. */
    assert(!smb_coop_campaign_request_spawn(38,1,9));
    gCurrActStarNum=2;gNetworkPlayers[1].currActNum=2;
    assert(!smb_coop_campaign_take_spawn(outgoing)&&accepted==1);
    gCurrActStarNum=1;gNetworkPlayers[1].currActNum=1;
    assert(!smb_coop_campaign_request_spawn(38,1,9));
    gCurrAreaIndex=2;gNetworkPlayers[1].currAreaIndex=2;smb_coop_campaign_update();
    assert(!smb_coop_campaign_take_spawn(outgoing)&&accepted==1);
    gCurrAreaIndex=1;gNetworkPlayers[1].currAreaIndex=1;smb_coop_campaign_update();
    /* Promotion executes a same-scene pending request locally exactly once,
     * even when the last other player left before the transport can drain. */
    assert(!smb_coop_campaign_request_spawn(38,1,9));
    gNetworkPlayers[0].connected=0;smb_coop_campaign_update();
    assert(smb_coop_campaign_authoritative()&&accepted==2&&!smb_coop_campaign_take_spawn(outgoing));
    /* The shared wave number advances monotonically across room caches. */
    gNetworkPlayers[0].connected=1;state.sequence+=10;state.wave=1;state.defeated_mask=0;state.active_mask=1;
    smb_coop_campaign_receive(0,&state);smb_coop_campaign_update();assert(encounter_wave==1&&encounter_defeated==0&&poses[1]);
    /* A different-area owner handoff retains damage but discards old geometry. */
    state.sequence++;state.fighter[0].percent=73;state.fighter[0].motion=252;state.fighter[0].position[0]=999;
    state.npc_air[0][0]=0x456;state.npc_air_hurt[0][0]=512.25f;state.npc_air_hurt[0][1]=8.5f;
    smb_coop_campaign_receive(0,&state);gCurrAreaIndex=2;gNetworkPlayers[1].currAreaIndex=2;smb_coop_campaign_update();
    gMarioStates[0].pos[0]=100;gMarioStates[0].pos[1]=200;gMarioStates[0].pos[2]=300;
    gNetworkPlayers[0].connected=0;smb_coop_campaign_update();
    assert(smb_coop_campaign_authoritative()&&percentages[1]==73&&npc_pending[0]);
    assert(last_pose[1].motion==29&&last_pose[1].position[0]==10&&last_pose[1].position[1]==20);
    assert(npc_air_valid==3&&npc_air[0][0]==0x456&&npc_air_hurt[0][0]==512.25f&&npc_air_hurt[0][1]==8.5f);
    /* Effective manual actors survive a none-mode hub ownership transfer. */
    gCurrLevelNum=16;gCurrCourseNum=0;gCurrAreaIndex=1;gCurrActStarNum=1;
    gNetworkPlayers[0]=(struct NetworkPlayer){1,0,16,1,1};gNetworkPlayers[1]=(struct NetworkPlayer){1,1,16,1,1};
    SmbCampaign manual=state;manual.sequence+=100;manual.level=16;manual.course=0;manual.key=0;manual.area=1;manual.act=1;
    manual.wave=0;manual.waves=1;manual.count=2;manual.active_mask=3;manual.defeated_mask=0;manual.mode=0;
    manual.fighter[0].character=38;manual.fighter[0].kind=37;manual.fighter[1]=manual.fighter[0];manual.fighter[1].character=9;manual.fighter[1].kind=18;
    manual.cpu_level[0]=5;manual.cpu_level[1]=6;
    smb_coop_campaign_receive(0,&manual);smb_coop_campaign_update();assert(encounter_count==2&&encounter_key==1016);
    gNetworkPlayers[0].connected=0;smb_coop_campaign_update();
    assert(encounter_count==2&&encounter_key==1016&&last_pose[1].character==38&&last_pose[2].character==9);
    gCurrActStarNum=2;gNetworkPlayers[1].currActNum=2;smb_coop_campaign_update();assert(encounter_course==-1);
    /* A manual replacement is a new life, including when it occupies a KO'd
     * seat. An old KO packet with a newer transport sequence cannot erase it. */
    gCurrLevelNum=9;gCurrCourseNum=1;gCurrActStarNum=3;
    gNetworkPlayers[0]=(struct NetworkPlayer){1,0,9,1,3};gNetworkPlayers[1]=(struct NetworkPlayer){1,1,9,1,3};
    SmbCampaign knocked=state;knocked.sequence+=1000;knocked.level=9;knocked.area=1;knocked.act=3;knocked.course=1;knocked.key=1;
    knocked.wave=0;knocked.waves=1;knocked.count=1;knocked.active_mask=0;knocked.defeated_mask=1;knocked.ko_serial[0]=10;
    knocked.npc_air_hurt[0][0]=900;
    smb_coop_campaign_receive(0,&knocked);smb_coop_campaign_update();assert(encounter_defeated==1&&!poses[1]);
    SmbCampaign replacement=knocked;replacement.sequence++;replacement.active_mask=1;replacement.defeated_mask=0;replacement.ko_serial[0]++;
    replacement.fighter[0].character=38;replacement.fighter[0].kind=37;
    replacement.npc_air[0][0]=0x777;replacement.npc_air_hurt[0][0]=.25f;
    smb_coop_campaign_receive(0,&replacement);smb_coop_campaign_update();
    assert(encounter_defeated==0&&poses[1]&&last_pose[1].character==38);
    knocked.sequence=replacement.sequence+1;smb_coop_campaign_receive(0,&knocked);smb_coop_campaign_update();
    assert(encounter_defeated==0&&poses[1]&&last_pose[1].character==38&&npc_ko_serial[0]==11);
    assert(npc_air[0][0]==0x777&&npc_air_hurt[0][0]==.25f);
    /* Locally committed replacements are recorded before progress can apply
     * the previous KO again, even without another transport tick. */
    gNetworkPlayers[0].connected=0;smb_coop_campaign_update();
    encounter_defeated=1;npc_ready[0]=0;npc_ko_serial[0]++;smb_coop_campaign_send(&state);
    assert(state.defeated_mask==1);
    encounter_defeated=0;npc_ready[0]=1;npc_ko_serial[0]++;smb_coop_campaign_spawned();
    smb_coop_campaign_progress_apply();smb_coop_campaign_send(&state);
    assert(encounter_defeated==0&&state.active_mask==1&&state.defeated_mask==0&&state.ko_serial[0]==13);
    test_manual_round_epochs();test_none_round_handoff();test_round_requests();
    puts("PASS: ownership, 16-slot selection, cross-room KO persistence, authority handoff with independent pending air damage, finite queue bounds, shared waves, retained damage with safe area poses, session CPU levels, manual hub actors, act filtering, scene-bound spawn requests, local promotion requests, manual respawn revisions, stale KO rejection and sender validation");
    return 0;
}
'''

def main():
    with tempfile.TemporaryDirectory(prefix="sm64-campaign-test-") as folder:
        folder = Path(folder)
        source, binary = folder / "test.c", folder / "test"
        source.write_text(SOURCE)
        subprocess.run(["clang", "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(HERE), str(source), "-lm", "-o", str(binary)], check=True)
        result = subprocess.run([str(binary)], capture_output=True, text=True, check=True)
        npc_source = (HERE / "sm64_npcs.inc").read_text()
        plan_source = npc_source[npc_source.index("typedef struct {unsigned waves"):npc_source.index("static void smb_encounter_enter")]
        plan_test = '#include <assert.h>\n#include <stdint.h>\n#include <stdio.h>\n#include <string.h>\nstatic int impossible_mode;static int smb_impossible(void){return impossible_mode;}\n' + plan_source + r'''
int main(void){
    impossible_mode=0;smb_campaign_plan();assert(campaign[0].size[0]==1&&campaign[0].level[0][0]==3);
    impossible_mode=1;smb_campaign_plan();assert(campaign[0].size[0]==3);
    for(unsigned stage=0;stage<27;stage++)for(unsigned wave=0;wave<campaign[stage].waves;wave++)
        for(unsigned seat=0;seat<campaign[stage].size[wave];seat++)assert(campaign[stage].level[wave][seat]==9);
    impossible_mode=0;smb_campaign_plan();assert(campaign[0].size[0]==1&&campaign[0].level[0][0]==3&&campaign[3].level[0][0]==5);
    puts("PASS: authored plans rebuild when session difficulty changes in either direction");return 0;
}
'''
        plan_file, plan_binary = folder / "plans.c", folder / "plans"
        plan_file.write_text(plan_test)
        subprocess.run(["clang", "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(HERE), str(plan_file), "-o", str(plan_binary)], check=True)
        plan_result = subprocess.run([str(plan_binary)], capture_output=True, text=True, check=True)
    output = HERE.parent / "verification/coop-campaign-ledger.json"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps({"passed": True, "output": result.stdout.strip(), "authored_mode_switch": plan_result.stdout.strip(), "trace": result.stderr.strip(), "scope": "Isolated native C ownership/ledger contract and actual authored-plan mode switch; full engine/network playtest remains separate."}, indent=2)+"\n")
    print(result.stdout.strip())
    print(plan_result.stdout.strip())
    print(output)

if __name__ == "__main__":
    main()
