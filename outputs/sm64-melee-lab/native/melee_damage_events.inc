/* Actual applied damage is independent of percentage. A coin can heal during
 * the same host frame as a hit, and a hit at 999% must still cost air. Native
 * TakeDamage calls this inside its application guard, once per application. */
#include <float.h>
#include <melee/ft/fighter.h>
#include <melee/pl/player.h>
typedef struct {
    HSD_GObj* object;unsigned spawn;
    double pending,total;
} SmbDamageEvents;
static SmbDamageEvents smb_damage_events[GM_MAX_PLAYERS][2];
static SmbDamageEvents* smb_native_damage_slot(Fighter* fp) {
    if(!fp||fp->player_idx>=GM_MAX_PLAYERS)return NULL;
    SmbDamageEvents* events=&smb_damage_events[fp->player_idx][fp->is_sub_fighter];
    if(events->object!=fp->gobj||events->spawn!=fp->x8_spawnNum) {
        memset(events,0,sizeof *events);events->object=fp->gobj;events->spawn=fp->x8_spawnNum;
    }
    return events;
}
void smb_native_damage_reset(Fighter* fp) {
    SmbDamageEvents* events=smb_native_damage_slot(fp);
    if(events){events->pending=0;events->total=0;}
}
void smb_native_damage_applied(Fighter* fp,float amount) {
    if(!isfinite(amount)||amount<=0)return;
    SmbDamageEvents* events=smb_native_damage_slot(fp);if(!events)return;
    events->pending+=amount;events->total+=amount;
}
static double smb_native_damage_total(Fighter* fp) {
    SmbDamageEvents* events=smb_native_damage_slot(fp);return events?events->total:0;
}
/* Consume once after host world interactions and native combat. Absent
 * actors return zero; a deleted/replaced actor never leaks old events.
 * Remote-grab imports use this same queue without re-applying native damage. */
float smb_embed_actor_damage(unsigned seat,unsigned sub) {
    if(seat>=GM_MAX_PLAYERS||sub>1)return -1;
    HSD_GObj* object=Player_GetEntityAtIndex(seat,sub);
    if(!object||!object->user_data) {memset(&smb_damage_events[seat][sub],0,sizeof smb_damage_events[seat][sub]);return 0;}
    SmbDamageEvents* events=smb_native_damage_slot(object->user_data);if(!events)return 0;
    double result=fmin(events->pending,FLT_MAX);
    events->pending-=result;return (float)result;
}
/* Called only on the disposable Popo/Nana water diagnostic actors. Exercise
 * the real damage routine: net percentage cannot substitute for these events. */
static int smb_native_damage_test_actor(Fighter* fp) {
    unsigned seat=fp->player_idx,sub=fp->is_sub_fighter,spawn=fp->x8_spawnNum;
    SmbDamageEvents saved=*smb_native_damage_slot(fp);
    HSD_GObj* partner=Player_GetEntityAtIndex(seat,1-sub);
    SmbDamageEvents* other=partner?smb_native_damage_slot(partner->user_data):NULL;
    double other_pending=other?other->pending:0;
    float percent=fp->dmg.x1830_percent;unsigned cloak=fp->x2226_b4,allow=fp->x2226_b3;
    fp->x2226_b4=fp->x2226_b3=false;smb_native_damage_reset(fp);
    fp->dmg.x1830_percent=20;
    Fighter_TakeDamage_8006CC7C(fp,8);fp->dmg.x1830_percent-=8;
    Player_SetHPByIndex(seat,sub,fp->dmg.x1830_percent);
    int healing=fp->dmg.x1830_percent==20&&smb_embed_actor_damage(seat,sub)==8&&smb_embed_actor_damage(seat,sub)==0;
    fp->dmg.x1830_percent=996;Fighter_TakeDamage_8006CC7C(fp,10);
    int capped=fp->dmg.x1830_percent==999&&smb_embed_actor_damage(seat,sub)==10;
    fp->x2226_b4=true;Fighter_TakeDamage_8006CC7C(fp,5);
    int blocked=fp->dmg.x1830_percent==999&&smb_embed_actor_damage(seat,sub)==0;
    fp->x2226_b4=false;Fighter_TakeDamage_8006CC7C(fp,3);
    fp->x8_spawnNum++;
    int reset=smb_embed_actor_damage(seat,sub)==0;
    int isolated=!other||other->pending==other_pending;
    fp->x8_spawnNum=spawn;fp->dmg.x1830_percent=percent;
    fp->x2226_b4=cloak;fp->x2226_b3=allow;
    Player_SetHPByIndex(seat,sub,percent);*smb_native_damage_slot(fp)=saved;
    fprintf(stderr,"SMB DAMAGE EVENTS PROBE seat=%u sub=%u healing=%d capped=%d blocked=%d spawn_reset=%d isolated=%d passed=%d\n",
        seat,sub,healing,capped,blocked,reset,isolated,healing&&capped&&blocked&&reset&&isolated);
    return healing&&capped&&blocked&&reset&&isolated;
}
