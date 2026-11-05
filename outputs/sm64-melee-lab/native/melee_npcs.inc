/* Native Melee CPU seats, sharing the player's live SM64 collision world. */
#include <melee/ft/ftcolanim.h>
#include <melee/ef/eflib.h>
#include <melee/ft/ftdata.h>
#include <pc/m3d_akaneia.h>
static HSD_GObj* npc_sleeping[4][2];
extern unsigned smb_embed_costumes(unsigned);
extern void smb_native_clear_npc_items(unsigned);
extern void smb_native_expect_costume(unsigned,unsigned);
static int smb_native_impossible(void) {
    const char* mode=getenv("SMB_DIFFICULTY");return mode&&!strcmp(mode,"impossible");
}
/* Difficulty is an AI parameter, not an AI restart. The retail setter resets
 * Nana's follow history and dereferences Popo even while he is suspended. */
static void smb_native_npc_level(unsigned seat,unsigned level) {
    Player_GetPtrForSlot(seat)->cpu_level=level;
    for(unsigned sub=0;sub<2;sub++) {
        HSD_GObj* g=Player_GetEntityAtIndex(seat,sub);
        if(g)((Fighter*)g->user_data)->cpu.level=level;
    }
}
void smb_native_npc_levels(void) {
    if(!smb_native_impossible())return;
    for(unsigned seat=1;seat<4;seat++)for(unsigned sub=0;sub<2;sub++) {
        HSD_GObj* object=Player_GetEntityAtIndex(seat,sub);if(!object)continue;
        Fighter* fighter=object->user_data;
        if(fighter->cpu.level!=9) {
            fprintf(stderr,"SMB IMPOSSIBLE LEVEL RESTORE seat=%u sub=%u previous=%d level=9\n",seat,sub,fighter->cpu.level);
            smb_native_npc_level(seat,9);
        }
    }
}
int smb_embed_npc_wait(unsigned slot) {
    if(slot<1||slot>3)return -1;
    for(unsigned sub=0;sub<2;sub++) {
        HSD_GObj* entity=Player_GetEntityAtIndex(slot,sub);if(!entity)continue;
        Fighter* fp=entity->user_data;
        if(!fp->is_sleeping){npc_sleeping[slot][sub]=entity;m3d_ExitLocal(fp);ftCo_800BFD04(entity);}
    }
    smb_native_clear_npc_items(slot);
    return 0;
}
int smb_embed_npc(unsigned slot,const float* position,SmbNpc* state) {
    if(slot<1||slot>3||!state)return -1;
    HSD_GObj* object=Player_GetEntity(slot);if(!object)return -2;
    Fighter* fp=object->user_data;
    if(position)for(unsigned sub=0;sub<2;sub++) {
        HSD_GObj* entity=Player_GetEntityAtIndex(slot,sub);if(!entity)continue;
        Fighter* fighter=entity->user_data;
        if(fighter->is_sleeping&&npc_sleeping[slot][sub]!=entity)continue;
        npc_sleeping[slot][sub]=NULL;
        if(!sub)fighter->cpu.kind=4;
        m3d_ExitLocal(fighter);fighter->cur_pos=(Vec3){position[0]+sub*4,position[1],position[2]};
        fighter->prev_pos=fighter->coll_data.cur_pos=fighter->coll_data.prev_pos=fighter->coll_data.last_pos=fighter->cur_pos;
        fighter->self_vel=(Vec3){0};fighter->xD4_unk_vel=(Vec3){0};fighter->gr_vel=0;
        fighter->x8c_kb_vel=(Vec3){0};fighter->xF0_ground_kb_vel=0;
        Player_SetHPByIndex(slot,sub,fighter->dmg.x1830_percent);mpColl_80043680(&fighter->coll_data,&fighter->cur_pos);
        fighter->coll_data.floor.index=-1;fighter->coll_data.env_flags=0;
        ftCommon_8007D5D4(fighter);ftCo_Fall_Enter(entity);fighter->self_vel.y=-.1f;
        m3d_SetLane(fighter,1,0);HSD_JObjSetTranslate(entity->hsd_obj,&fighter->cur_pos);
        fprintf(stderr,"SMB NPC SPAWN slot=%u sub=%u kind=%d level=%d x=%.2f y=%.2f z=%.2f\n",slot,sub,fighter->kind,fighter->cpu.level,position[0],position[1],position[2]);
    }
    memset(state,0,sizeof *state);state->kind=fp->kind;state->motion=fp->motion_id;state->level=fp->cpu.level;
    state->target=fp->cpu.x44?fp->cpu.x44->player_idx:99;state->percent=fp->dmg.x1830_percent;
    state->grounded=fp->ground_or_air==GA_Ground;
    state->damage_flight=fp->motion_id>=ftCo_MS_DamageHi1&&fp->motion_id<=ftCo_MS_DamageFlyRoll;
    state->knockback=hypotf(fp->x8c_kb_vel.x,fp->x8c_kb_vel.y);state->costume=Player_GetCostumeId(slot);
    state->suspended=npc_sleeping[slot][0]!=NULL||npc_sleeping[slot][1]!=NULL;
    memcpy(state->position,&fp->cur_pos,12);
    for(unsigned i=0;i<4;i++)if(fp->x914[i].state)state->hitboxes|=1u<<i;
    return 0;
}

/* A seat belongs to the current course, never to the donor match's roster. */
#include <melee/if/ifstatus.h>
#include <melee/lb/lbaudio_ax.h>
#include <melee/gm/gm_1601.h>
static unsigned encounter_visible=15;
int smb_native_npc_visible(unsigned seat) {return seat<4&&(encounter_visible&(1u<<seat));}
static void smb_native_create_npc(unsigned seat,unsigned character,unsigned costume) {
        Player_SetPlayerCharacter(seat,character);
        int kind=Player_800325C8(character,false);
        unsigned costumes=kind>=33&&kind<40?m3d_akaneia_costume_count(kind):CostumeListsForeachCharacter[kind].numCostumes;
        Player_SetCostumeId(seat,costumes?costume%costumes:0);
        smb_native_expect_costume(seat,Player_GetCostumeId(seat));
        Player_SetSlottype(seat,Gm_PKind_Cpu);
        Player_SetTeam(seat,1);Player_SetPadPort(seat,seat);Player_SetHandicap(seat,9);
        Player_SetStocks(seat,1);Player_SetFacingDirection(seat,-1);
        Player_SetPlayerAndEntityCpuLevel(seat,9);Player_SetPlayerAndEntityCpuType(seat,4);
        Player_SetNametagSlotID(seat,0x78);
        /* Fighter_Create synchronously resolves cached archive/animation data. */
        Player_80031AD0(seat);Player_80031848(seat);ifStatus_802F6508(seat);
        encounter_visible|=1u<<seat;smb_embed_npc_wait(seat);
        fprintf(stderr,"SMB ENCOUNTER ROSTER seat=%u external=%u costume=%u choices=%u kind=%d\n",seat,character,Player_GetCostumeId(seat),costumes,((Fighter*)Player_GetEntity(seat)->user_data)->kind);
}
int smb_embed_npc_add(unsigned seat,unsigned character,unsigned costume,unsigned level,const float* position) {
    if(seat<1||seat>3||!smb_embed_costumes(character)||level<1||level>9||!position)return -1;
    smb_native_clear_npc_items(seat);ifStatus_802F6788(seat);
    if(Player_GetEntity(seat))Player_80031EBC(seat);
    memset(npc_sleeping[seat],0,sizeof npc_sleeping[seat]);Player_InitOrResetPlayer(seat);
    smb_native_create_npc(seat,character,costume);
    smb_native_npc_level(seat,smb_native_impossible()?9:level);
    gm_LoadAnnouncer();lbAudioAx_8002785C();
    SmbNpc state;return smb_embed_npc(seat,position,&state);
}
int smb_embed_encounter(unsigned count,const SmbEncounter* plan) {
    if(count>3||(count&&!plan))return -1;
    /* Use Melee's real remove/create lifecycle, including transformed forms,
     * tether callbacks, HUD objects, costume and animation data. */
    for(unsigned seat=1;seat<4;seat++) {
        smb_native_clear_npc_items(seat);
        ifStatus_802F6788(seat);
        if(Player_GetEntity(seat))Player_80031EBC(seat);
        memset(npc_sleeping[seat],0,sizeof npc_sleeping[seat]);
        Player_InitOrResetPlayer(seat);
    }
    encounter_visible=1;
    for(unsigned i=0;i<count;i++) {
        unsigned seat=i+1,character=plan->character[i];
        smb_native_create_npc(seat,character,plan->costume[i]+character*7+1);
    }
    gm_LoadAnnouncer();
    lbAudioAx_8002785C(); /* Load voices/effects for the newly assigned roster. */
    return 0;
}
#include <melee/ef/efsync.h>
#include <melee/gm/gm_1601.h>
/* Announcements serialize, so simultaneous KOs do not interrupt one another. */
static unsigned ko_name[16],ko_head,ko_tail,ko_phase;
static int ko_voice=-1;
void smb_native_ko_audio_tick(void) {
    if(ko_head==ko_tail)return;
    if(ko_phase && ko_voice>=0 && lbAudioAx_80023710(ko_voice))return;
    if(!ko_phase) {
        static const int calls[27]={0x7C830,0x7C831,0x7C835,0x7C83A,0x7C83F,0x7C840,0x7C842,0x7C844,0x7C845,0x7C846,0x7C848,0x7C84A,0x7C84B,0x7C84D,0x7C83B,0x7C83D,0x7C84E,0x7C84F,0x7C851,0x7C850,0x7C834,0x7C843,0x7C832,0x7C83C,0x7C84C,0x7C836,0x7C849};
        unsigned name=ko_name[ko_head%16];int sound=name<27?calls[name]:0x7C865;
        ko_voice=lbAudioAx_80023870(sound,127,64,0x19);ko_phase=1;
        fprintf(stderr,"SMB KO ANNOUNCER name=%u sound=%x voice=%d generic=%d\n",name,sound,ko_voice,name>=27);
    } else if(ko_phase==1) {
        ko_voice=lbAudioAx_80023870(0x7C852,127,64,0x19);ko_phase=2;
        fprintf(stderr,"SMB KO ANNOUNCER defeated=1 voice=%d\n",ko_voice);
    } else {ko_head++;ko_phase=0;}
}
int smb_embed_npc_configure(unsigned seat,unsigned level,float attack) {
    if(seat<1||seat>3||level<1||level>9)return -1;
    if(smb_native_impossible()){level=9;attack=1.f;}
    smb_native_npc_level(seat,level);Player_SetAttackRatio(seat,attack);
    fprintf(stderr,"SMB NPC CONFIGURE seat=%u level=%u attack=%.2f impossible=%d\n",seat,level,attack,smb_native_impossible());
    return 0;
}
int smb_embed_npc_ko(unsigned seat,const float* presentation,unsigned star) {
    if(seat<1||seat>3)return -1;
    HSD_GObj* g=Player_GetEntity(seat);if(!g)return -2;
    Fighter* fp=g->user_data;unsigned name=Player_GetPlayerCharacter(seat);
    if(fp->kind==7)name=19;else if(fp->kind==19)name=18;
    Vec3 pos=presentation?(Vec3){presentation[0],presentation[1],presentation[2]}:fp->cur_pos;
    smb_embed_npc_wait(seat);Player_SetStocks(seat,0);encounter_visible&=~(1u<<seat);
    /* Detached native blast effect survives sleeping/unloading the fighter. */
    float angle=3.14159265f,scale=1.f;
    void* effect=star?efSync_Spawn(0x42D,NULL,&pos):efSync_Spawn(0x42B,NULL,&pos,&angle,&scale,0xFFE8A0,0xFF5020);
    int sound=lbAudioAx_800237A8(star?0x83:0x61,127,64);
    if(ko_tail-ko_head<16)ko_name[ko_tail++%16]=name;
    fprintf(stderr,"SMB KO EFFECT slot=%u native=%x created=%d sound=%d x=%.2f y=%.2f z=%.2f\n",seat,star?0x42d:0x42b,effect!=NULL,sound,pos.x,pos.y,pos.z);
    return 0;
}

/* Deterministic stress fixture: exercise genuine native knockback through a
 * course blast boundary. Unreachable in normal play. */
#include <melee/ft/ftcoll.h>
#include <melee/ft/fighter.h>
#include <melee/ft/kinds/ftCommon/ftCo_Damage.h>
void smb_native_encounter_test(void) {
    if(!getenv("SMB_SMOKE")||!getenv("SMB_TEST_ENCOUNTER_HIT"))return;
    static unsigned done;unsigned serial=atoi(getenv("SMB_TEST_ENCOUNTER_HIT"));
    if(serial==done)return;done=serial;
    HSD_GObj* object=Player_GetEntity(1);if(!object)return;Fighter* fp=object->user_data;
    if(fp->is_sleeping)return;
    /* A high-damage finisher, calculated by the same native hit/weight path
     * as SM64 enemy impacts; no direct position edit or KO flag. */
    HitCapsule hit={0};hit.x24=100;hit.x2C=60;
    fp->dmg.x1830_percent=getenv("SMB_TEST_HIT_PERCENT")?atof(getenv("SMB_TEST_HIT_PERCENT")):500;fp->dmg.x1838_percentTemp=20;
    fp->dmg.kb_applied=ftColl_80079AB0(fp,&hit,20,1,1,1,smb_native_weight(fp));
    Fighter_TakeDamage_8006CC7C(fp,20);fp->dmg.facing_dir_1=-1;fp->dmg.x1848_kb_angle=getenv("SMB_TEST_HIT_ANGLE")?atoi(getenv("SMB_TEST_HIT_ANGLE")):90;
    fp->dmg.x184c_damaged_hurtbox=1;fp->dmg.x1860_element=HitElement_Normal;fp->dmg.x1854_collpos=fp->cur_pos;
    HSD_GObj* old=HSD_GObj_804D7814;HSD_GObj_804D7814=object;
    ftCo_Damage_CalcKnockback(fp);ftCo_8008DCE0(object,-1,0);HSD_GObj_804D7814=old;
    fp->dmg.x1838_percentTemp=0;
    fprintf(stderr,"SMB ENCOUNTER TEST HIT serial=%u percent=%.1f knockback=%.3f velocity=%.3f,%.3f,%.3f\n",serial,fp->dmg.x1830_percent,fp->dmg.kb_applied,fp->x8c_kb_vel.x,fp->x8c_kb_vel.y,fp->x8c_kb_vel.z);
}
