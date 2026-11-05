/* Each fighter samples the water at its own position. The elected encounter
 * owner alone advances CPU air; remote poses never receive a second swim tick. */
#include "game/main.h"
static struct {unsigned wet,kind,scene,stamp;} actor_water_state[4][2];
static void smb_npc_water_reset(unsigned slot) {
    if(slot<1||slot>3)return;
    npc_air_valid&=~(3u<<((slot-1)*2));
    npc_air[slot-1][0]=npc_air[slot-1][1]=0x880;
    npc_air_hurt[slot-1][0]=npc_air_hurt[slot-1][1]=0;
    memset(actor_water_state[slot],0,sizeof actor_water_state[slot]);
}
static void smb_npc_water_capture_actor(unsigned seat,unsigned sub,int wet) {
    unsigned bit=1u<<((seat-1)*2+sub);
    if(!(npc_air_valid&bit)){
        npc_air[seat-1][sub]=0x880;npc_air_hurt[seat-1][sub]=0;npc_air_valid|=bit;
    }
    /* Consume on dry frames too. A hit cannot remain queued in native Melee
     * and later masquerade as new underwater damage after entering water. */
    float damage=embedded_actor_damage?embedded_actor_damage(seat,sub):0;
    if(wet&&isfinite(damage)&&damage>0) {
        npc_air_hurt[seat-1][sub]=fminf(1e7f,npc_air_hurt[seat-1][sub]+damage*32.f);
        if(getenv("SMB_SCENARIO"))fprintf(stderr,"SMB NPC AIR DAMAGE slot=%u sub=%u damage=%.3f queued=%.3f\n",
            seat,sub,damage,npc_air_hurt[seat-1][sub]);
    }
}
#ifdef SMB_COOP_CAMPAIGN
static void smb_npc_water_capture(void) {
    if(!smb_npc_authoritative()||!embedded_actor_state||!embedded_actor_damage)return;
    for(unsigned seat=1;seat<=smb_npc_count();seat++)for(unsigned sub=0;sub<2;sub++) {
        if(!npc_ready[seat-1]||(encounter_defeated&(1u<<(seat-1))))continue;
        SmbFighterState actor={0};if(embedded_actor_state(seat,sub,&actor))continue;
        typeof(actor_water_state[0][0])* sample=&actor_water_state[seat][sub];
        int wet;
        if(sample->scene&&sample->kind==actor.kind&&sample->stamp==npc_last_reset[seat-1])
            wet=sample->wet; /* The environment used by the preceding native tick. */
        else {
            /* A first post-spawn hit still counts before the first ordinary
             * water update. Never drain its getter merely to reset a seat. */
            float water=find_water_level(actor.position[0]*10,actor.position[2]*10);
            wet=water> -10000&&actor.position[1]*10<water-70;
        }
        smb_npc_water_capture_actor(seat,sub,wet);
    }
}
#endif
static void smb_host_actor_water(struct MarioState* m,const SmbInput* command) {
    if(!embedded_actor_state||!embedded_actor_environment)return;
    int authority=smb_npc_authoritative();
    unsigned count=smb_npc_count();
    for(unsigned seat=0;seat<=3;seat++)for(unsigned sub=0;sub<2;sub++) {
        if(seat&&(!authority||seat>count||!npc_ready[seat-1]||
           (encounter_defeated&(1u<<(seat-1)))))continue;
        SmbFighterState actor={0};
        if(embedded_actor_state(seat,sub,&actor))continue;
        float x=actor.position[0]*10,y=actor.position[1]*10,z=actor.position[2]*10;
        float water=find_water_level(x,z);
        unsigned flags=actor.caps&1?SMB_ENV_METAL:0;
        unsigned scene=((unsigned)gCurrLevelNum<<16)|((unsigned)gCurrAreaIndex<<8)|(unsigned)gCurrActNum;
        unsigned stamp=seat?npc_last_reset[seat-1]:character_changed_at;
        typeof(actor_water_state[0][0])* sample=&actor_water_state[seat][sub];
        if(sample->kind!=actor.kind||sample->scene!=scene||sample->stamp!=stamp)sample->wet=0;
        int wet=water> -10000&&y<water+(sample->wet?20:-70);
        sample->kind=actor.kind;sample->scene=scene;sample->stamp=stamp;
        if(!seat&&!sub) {
            /* A warp/hosted action may have moved Mario since the preceding
             * native frame. Its command is already sampled at the new pose. */
            wet=!!(command->flags&SMB_ENV_WATER);water=command->water_level*10;
            flags=SMB_ENV_CAP_MANAGED|((m->flags&MARIO_METAL_CAP)?SMB_ENV_METAL:0);
        } else if(!seat)flags=SMB_ENV_CAP_MANAGED|((m->flags&MARIO_METAL_CAP)?SMB_ENV_METAL:0);
        sample->wet=wet;
        if(wet)flags|=SMB_ENV_WATER;
        embedded_actor_environment(seat,sub,flags,water> -10000?water*.1f:0);
        if(!seat)continue; /* The player's native SM64 air HUD owns their air. */
        /* Consume every live actor's native event queue, including on land.
         * Only actual positive applied damage counts: shielded/invulnerable
         * contacts never become events, and healing cannot mask a hit. */
        smb_npc_water_capture_actor(seat,sub,wet);
        int air=(int)npc_air[seat-1][sub];
        int cold=(m->area->terrainType&TERRAIN_MASK)==TERRAIN_SNOW;
        float heal=0;
        /* Melee cloak is not an intangible SM64 action. Metal and the real
         * native hit filter provide their own corresponding exemptions. */
        air=smb_air_step(air,&heal,&npc_air_hurt[seat-1][sub],wet,!!(flags&SMB_ENV_METAL),
                         cold,y>=water-140,0,gDebugLevelSelect);
        npc_air[seat-1][sub]=(unsigned)air;
        if(air<0x100) {
            smb_npc_environment_kill(seat,sub,"drowning");
        }
        if(getenv("SMB_SCENARIO")&&frames%15==0&&wet)
            fprintf(stderr,"SMB NPC WATER frame=%u slot=%u sub=%u y=%.3f surface=%.1f vy=%.3f air=%u metal=%u hurt=%.3f\n",
                frames,seat,sub,y,water,actor.velocity[1],npc_air[seat-1][sub],!!(flags&SMB_ENV_METAL),npc_air_hurt[seat-1][sub]);
    }
}
