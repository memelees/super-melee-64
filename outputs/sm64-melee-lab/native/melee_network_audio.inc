/* Owner-authored SFX commands. Native callbacks still produce local audio;
 * peers receive resolved samples and audio-only channel operations. */
#ifndef SMB_NETWORK_AUDIO_INCLUDED
#define SMB_NETWORK_AUDIO_INCLUDED
#include <limits.h>
#include <melee/ft/ft_0877.h>
#include <melee/ft/ft_0881.h>
#include <pc/m3d_akaneia.h>

enum {
    SMB_SOUND_ONESHOT,SMB_SOUND_VOICE30,SMB_SOUND_VOICE42,SMB_SOUND_EFFECT54,
    SMB_SOUND_EFFECT66,SMB_SOUND_EFFECT78,SMB_SOUND_EFFECT90,SMB_SOUND_EFFECT114,
    SMB_SOUND_ITEM_ONESHOT,SMB_SOUND_ITEM_LOOP1,SMB_SOUND_ITEM_LOOP2,
    SMB_SOUND_FIGHTER_STOP,SMB_SOUND_ITEM_STOP,SMB_SOUND_PRODUCER_STOP,SMB_SOUND_OWNER_STOP
};
#define SMB_SOUND_NONE 0x83D60u
#define SMB_SOUND_STOP 0x83D61u
#define SMB_AUDIO_QUEUE 512u
#define SMB_AUDIO_VOICES 128u
static unsigned smb_audio_enabled,smb_audio_depth,smb_audio_count;
static uint32_t smb_audio_sequence;
static SmbSound smb_audio_queue[SMB_AUDIO_QUEUE];
static struct {unsigned active;Vec3 position;} smb_audio_authority[19][2];
static uint32_t smb_audio_received[16],smb_audio_received_count[16][2];
static struct {
    uint32_t source,age,envelope_until;int handle;
    unsigned char used,owner,sub,type;
} smb_audio_voices[16][SMB_AUDIO_VOICES];
static struct {
    Item* item;uint32_t source;Vec3 position;
    unsigned char used,owner,sub;
} smb_audio_items[SMB_AUDIO_VOICES];
static uint32_t smb_audio_age,smb_audio_clock;
static SmbSound* smb_audio_emit_raw(unsigned,unsigned,uint32_t,unsigned,unsigned,unsigned,unsigned,Vec3);

static void smb_audio_cut(unsigned peer,unsigned slot) {
    if(!smb_audio_voices[peer][slot].used)return;
    int handle=smb_audio_voices[peer][slot].handle;
    if(handle>=0&&lbAudioAx_80023710(handle))lbAudioAx_800236B8(handle);
    memset(&smb_audio_voices[peer][slot],0,sizeof smb_audio_voices[peer][slot]);
}
static void smb_audio_clear_peer(unsigned peer) {
    for(unsigned n=0;n<SMB_AUDIO_VOICES;n++)smb_audio_cut(peer,n);
    smb_audio_received[peer]=0;memset(smb_audio_received_count[peer],0,sizeof smb_audio_received_count[peer]);
}
void smb_native_audio_tick(void) {
    if(!smb_audio_enabled)return;
    smb_audio_clock++;
    for(unsigned peer=0;peer<16;peer++)for(unsigned n=0;n<SMB_AUDIO_VOICES;n++)if(smb_audio_voices[peer][n].used) {
        if(!lbAudioAx_80023710(smb_audio_voices[peer][n].handle))smb_audio_cut(peer,n);
    }
    /* A sender can retain its human player while giving up shared NPCs or a
     * borrowed grab victim. Retire those channels without cutting its player. */
    for(unsigned seat=0;seat<19;seat++)for(unsigned sub=0;sub<2;sub++)if(smb_audio_authority[seat][sub].active) {
        HSD_GObj* object=Player_GetEntityAtIndex(seat,sub);Fighter* fp=object?object->user_data:NULL;
        if(!fp||fp->is_sleeping||smb_native_remote_controlled(fp)) {
            smb_audio_emit_raw(seat,sub,0,SMB_SOUND_OWNER_STOP,SMB_SOUND_STOP,0,64,smb_audio_authority[seat][sub].position);
            smb_audio_authority[seat][sub].active=0;
        }
    }
}
void smb_embed_audio_enabled(unsigned enabled) {
    if(!enabled) {
        for(unsigned peer=0;peer<16;peer++)smb_audio_clear_peer(peer);
        smb_audio_count=0;memset(smb_audio_items,0,sizeof smb_audio_items);memset(smb_audio_authority,0,sizeof smb_audio_authority);
    }
    if(!!enabled!=smb_audio_enabled)fprintf(stderr,"SMB NET AUDIO enabled=%u native_sfx=1 music=0\n",!!enabled);
    smb_audio_enabled=!!enabled;
}
static int smb_audio_proxy(Fighter* fp) {
    return fp&&smb_native_presentation_only(fp);
}
static Vec3 smb_audio_position(void* actor,Vec3 position) {
    if(m3d_MapIsActive(actor))m3d_MapToWorld(&position);
    return position;
}
static SmbSound* smb_audio_emit_raw(unsigned owner,unsigned sub,uint32_t source,unsigned type,unsigned sound,unsigned volume,unsigned pan,Vec3 position) {
    if(!smb_audio_enabled||smb_audio_depth||owner>=19||sub>1||sound==SMB_SOUND_NONE)return NULL;
    if(smb_audio_count==SMB_AUDIO_QUEUE) {
        /* Dropping a stop can leave an authored loop playing indefinitely.
         * A producer reset is preferable to a permanently stuck charge sound. */
        memset(&smb_audio_queue[0],0,sizeof smb_audio_queue[0]);
        smb_audio_queue[0].sequence=++smb_audio_sequence;
        smb_audio_queue[0].type=SMB_SOUND_PRODUCER_STOP;
        smb_audio_queue[0].sound=SMB_SOUND_STOP;smb_audio_count=1;
        fprintf(stderr,"SMB NET AUDIO queue_reset=1 capacity=%u\n",SMB_AUDIO_QUEUE);
    }
    SmbSound* event=&smb_audio_queue[smb_audio_count++];memset(event,0,sizeof *event);
    if(!++smb_audio_sequence)++smb_audio_sequence;
    event->sequence=smb_audio_sequence;event->source=source;event->sound=sound;
    event->owner=owner;event->owner_sub=sub;
    event->type=type;event->volume=volume>127?127:volume;event->pan=pan>127?127:pan;
    memcpy(event->position,&position,sizeof event->position);
    return event;
}
static SmbSound* smb_audio_emit(Fighter* fp,uint32_t source,unsigned type,unsigned sound,unsigned volume,unsigned pan,Vec3 position) {
    if(!fp||smb_audio_proxy(fp))return NULL;
    unsigned seat=fp->player_idx,sub=fp->is_sub_fighter?1:0;
    SmbSound* event=smb_audio_emit_raw(seat,sub,source,type,sound,volume,pan,position);
    if(event){smb_audio_authority[seat][sub].active=1;smb_audio_authority[seat][sub].position=position;}
    return event;
}
static SmbSound* smb_audio_fighter(Fighter* fp,unsigned type,int sound,unsigned volume,unsigned pan) {
    if(!fp||!smb_audio_enabled||smb_audio_depth||smb_audio_proxy(fp))return NULL;
    if((type==SMB_SOUND_VOICE30||type==SMB_SOUND_VOICE42)&&fp->x2225_b6&&
       fp->kind!=Ft_Kind_GameWatch&&fp->kind!=Ft_Kind_Samus)return NULL;
    if((unsigned)sound!=SMB_SOUND_STOP&&(unsigned)sound!=SMB_SOUND_NONE)sound=ft_80087D0C(fp,sound);
    return smb_audio_emit(fp,0,type,(unsigned)sound,volume,pan,smb_audio_position(fp,fp->cur_pos));
}
static Fighter* smb_audio_item_owner(Item* item) {
    if(!item||!item->owner)return NULL;
    /* An orphaned article can outlive its original GObj allocation. Compare
     * ownership against live actors before dereferencing that pointer. */
    for(unsigned seat=0;seat<19;seat++)for(unsigned sub=0;sub<2;sub++) {
        HSD_GObj* object=Player_GetEntityAtIndex(seat,sub);
        if(object&&object==item->owner)return object->user_data;
    }
    return NULL;
}
static void smb_audio_item(Item* item,unsigned type,int sound,unsigned volume,unsigned pan) {
    if(!item||!smb_audio_enabled||smb_audio_depth)return;
    unsigned cached=SMB_AUDIO_VOICES,free_slot=SMB_AUDIO_VOICES;
    for(unsigned n=0;n<SMB_AUDIO_VOICES;n++) {
        if(smb_audio_items[n].used&&smb_audio_items[n].item==item&&smb_audio_items[n].source==(uint32_t)item->x1C){cached=n;break;}
        if(!smb_audio_items[n].used)free_slot=n;
    }
    Fighter* fp=smb_audio_item_owner(item);
    if(!fp||smb_audio_proxy(fp)) {
        if((unsigned)sound==SMB_SOUND_STOP&&cached<SMB_AUDIO_VOICES)
            smb_audio_emit_raw(smb_audio_items[cached].owner,smb_audio_items[cached].sub,smb_audio_items[cached].source,type,
                SMB_SOUND_STOP,0,64,smb_audio_items[cached].position);
        return;
    }
    if((unsigned)sound!=SMB_SOUND_STOP&&(unsigned)sound!=SMB_SOUND_NONE)
        sound=lbAudioAx_800233EC(m3d_akaneia_item_sound(item,sound));
    Vec3 position=smb_audio_position(item,item->pos);
    smb_audio_emit(fp,(uint32_t)item->x1C,type,(unsigned)sound,volume,pan,position);
    if(cached==SMB_AUDIO_VOICES)cached=free_slot;
    if(cached<SMB_AUDIO_VOICES) {
        smb_audio_items[cached].used=1;smb_audio_items[cached].item=item;smb_audio_items[cached].source=(uint32_t)item->x1C;
        smb_audio_items[cached].owner=fp->player_idx;smb_audio_items[cached].sub=fp->is_sub_fighter?1:0;smb_audio_items[cached].position=position;
    }
}
void smb_native_audio_item_retire(Item* item) {
    smb_audio_item(item,SMB_SOUND_ITEM_STOP,SMB_SOUND_STOP,0,64);
    for(unsigned n=0;n<SMB_AUDIO_VOICES;n++)if(smb_audio_items[n].used&&smb_audio_items[n].item==item)memset(&smb_audio_items[n],0,sizeof smb_audio_items[n]);
}
unsigned smb_embed_sounds(unsigned seat,SmbSound* output,unsigned capacity) {
    if(!output||!capacity||!smb_audio_enabled)return 0;
    if(capacity>SMB_SOUNDS)capacity=SMB_SOUNDS;
    unsigned emitted=0,retained=0;
    for(unsigned n=0;n<smb_audio_count;n++) {
        SmbSound event=smb_audio_queue[n];
        if(emitted<capacity&&(seat==UINT_MAX||event.owner==seat||event.type==SMB_SOUND_PRODUCER_STOP))output[emitted++]=event;
        else smb_audio_queue[retained++]=event;
    }
    smb_audio_count=retained;return emitted;
}
static int smb_audio_valid(const SmbSound* event) {
    if(!event->sequence||event->owner>=19||event->owner_sub>1||event->type>SMB_SOUND_OWNER_STOP||
       event->sound>1000000||event->volume>127||event->pan>127||(event->flags&~3u)||event->duration>60000||
       (!(event->flags&1)&&event->duration))return 0;
    for(unsigned k=0;k<3;k++)if(!isfinite(event->position[k])||fabsf(event->position[k])>10000)return 0;
    return 1;
}
static int smb_audio_same(const SmbSound* event,unsigned peer,unsigned n) {
    if(!smb_audio_voices[peer][n].used||smb_audio_voices[peer][n].source!=event->source)return 0;
    /* Reflection changes owner, but not this producer's item lifetime serial. */
    if(event->type>=SMB_SOUND_ITEM_ONESHOT&&event->type<=SMB_SOUND_ITEM_STOP&&
       smb_audio_voices[peer][n].type>=SMB_SOUND_ITEM_ONESHOT)return 1;
    return smb_audio_voices[peer][n].owner==event->owner&&smb_audio_voices[peer][n].sub==event->owner_sub;
}
static int smb_audio_replay(unsigned peer,const SmbSound* event) {
    unsigned type=event->type;
    if(type==SMB_SOUND_PRODUCER_STOP){for(unsigned n=0;n<SMB_AUDIO_VOICES;n++)smb_audio_cut(peer,n);return 0;}
    if(type==SMB_SOUND_OWNER_STOP) {
        for(unsigned n=0;n<SMB_AUDIO_VOICES;n++)if(smb_audio_voices[peer][n].used&&smb_audio_voices[peer][n].owner==event->owner&&smb_audio_voices[peer][n].sub==event->owner_sub) {
            unsigned channel=smb_audio_voices[peer][n].type;uint32_t until=smb_audio_voices[peer][n].envelope_until;
            if((channel!=SMB_SOUND_ONESHOT&&channel!=SMB_SOUND_ITEM_ONESHOT)||(until&&(int32_t)(until-smb_audio_clock)>0))smb_audio_cut(peer,n);
        }
        return 0;
    }
    if(type==SMB_SOUND_FIGHTER_STOP||type==SMB_SOUND_ITEM_STOP) {
        for(unsigned n=0;n<SMB_AUDIO_VOICES;n++)if(smb_audio_voices[peer][n].used) {
            unsigned channel=smb_audio_voices[peer][n].type;
            if(type==SMB_SOUND_FIGHTER_STOP) {
                if(smb_audio_voices[peer][n].owner!=event->owner)continue;
                uint32_t until=smb_audio_voices[peer][n].envelope_until;
                if((channel>SMB_SOUND_ONESHOT&&channel<SMB_SOUND_ITEM_ONESHOT)||
                   (channel==SMB_SOUND_ONESHOT&&(event->flags&2)&&until&&(int32_t)(until-smb_audio_clock)>0))smb_audio_cut(peer,n);
            } else if(channel>SMB_SOUND_ITEM_ONESHOT&&smb_audio_same(event,peer,n))smb_audio_cut(peer,n);
        }
        return 0;
    }
    int tracked=type!=SMB_SOUND_ONESHOT&&type!=SMB_SOUND_ITEM_ONESHOT;
    if(tracked)for(unsigned n=0;n<SMB_AUDIO_VOICES;n++)if(smb_audio_same(event,peer,n)) {
        unsigned channel=smb_audio_voices[peer][n].type;
        if(channel==type||(event->sound!=SMB_SOUND_STOP&&(type==SMB_SOUND_VOICE30||type==SMB_SOUND_VOICE42)&&
           (channel==SMB_SOUND_VOICE30||channel==SMB_SOUND_VOICE42)))smb_audio_cut(peer,n);
    }
    if(event->sound==SMB_SOUND_STOP||event->sound==SMB_SOUND_NONE)return 0;
    u8 volume=event->volume,pan=event->pan;Vec3 position;memcpy(&position,event->position,sizeof position);
    if(!m3d_SpatialSound(&position,&volume,&pan))return 0;
    int handle=lbAudioAx_800237A8((int)event->sound,volume,pan);if(handle<0)return 0;
    unsigned slot=SMB_AUDIO_VOICES,oldest=0;
    for(unsigned n=0;n<SMB_AUDIO_VOICES;n++) {
        if(smb_audio_voices[peer][n].used&&!lbAudioAx_80023710(smb_audio_voices[peer][n].handle))memset(&smb_audio_voices[peer][n],0,sizeof smb_audio_voices[peer][n]);
        if(!smb_audio_voices[peer][n].used){slot=n;break;}
        if(smb_audio_voices[peer][n].age<smb_audio_voices[peer][oldest].age)oldest=n;
    }
    if(slot==SMB_AUDIO_VOICES){slot=oldest;smb_audio_cut(peer,slot);}
    smb_audio_voices[peer][slot].used=1;smb_audio_voices[peer][slot].handle=handle;
    smb_audio_voices[peer][slot].source=event->source;smb_audio_voices[peer][slot].owner=event->owner;
    smb_audio_voices[peer][slot].sub=event->owner_sub;smb_audio_voices[peer][slot].type=type;
    smb_audio_voices[peer][slot].age=++smb_audio_age;
    if((event->flags&1)&&event->duration)smb_audio_voices[peer][slot].envelope_until=smb_audio_clock+event->duration;
    return 1;
}
int smb_embed_remote_sounds(unsigned peer,const SmbSound* events,unsigned count) {
    if(peer>=16||count>SMB_SOUNDS||(!events&&count))return -1;
    if(!events&&!count){smb_audio_clear_peer(peer);return 0;}
    if(!smb_audio_enabled)return 0;
    for(unsigned n=0;n<count;n++)if(!smb_audio_valid(&events[n]))return -2;
    for(unsigned n=0;n<count;n++) {
        const SmbSound* event=&events[n];
        if(smb_audio_received[peer]&&(int32_t)(event->sequence-smb_audio_received[peer])<=0)continue;
        smb_audio_received[peer]=event->sequence;int played=smb_audio_replay(peer,event);
        unsigned stopped=event->sound==SMB_SOUND_STOP;
        unsigned received=++smb_audio_received_count[peer][stopped];
        if(received<=12)fprintf(stderr,"SMB NET SOUND peer=%u sequence=%u owner=%u sub=%u type=%u sound=%u stop=%u played=%d\n",peer,event->sequence,event->owner,event->owner_sub,event->type,event->sound,stopped,played);
    }
    return 0;
}

/* The build renames only the originals' definitions. Calls inside them still
 * pass through these wrappers, and depth suppresses nested duplicate events. */
#define SMB_AUDIO_FIGHTER(name,type) \
    extern void smb_audio_orig_##name(Fighter*,int,u8,u8); \
    void name(Fighter* fp,int sound,u8 volume,u8 pan) { \
        if(smb_audio_proxy(fp)&&(unsigned)sound!=SMB_SOUND_STOP&&(unsigned)sound!=SMB_SOUND_NONE)return; \
        smb_audio_fighter(fp,type,sound,volume,pan);smb_audio_depth++; \
        smb_audio_orig_##name(fp,sound,volume,pan);smb_audio_depth--; \
    }
SMB_AUDIO_FIGHTER(ft_PlaySFX,SMB_SOUND_ONESHOT)
SMB_AUDIO_FIGHTER(ft_800881D8,SMB_SOUND_VOICE30)
SMB_AUDIO_FIGHTER(ft_80088328,SMB_SOUND_VOICE42)
SMB_AUDIO_FIGHTER(ft_80088478,SMB_SOUND_EFFECT54)
SMB_AUDIO_FIGHTER(ft_80088510,SMB_SOUND_EFFECT66)
SMB_AUDIO_FIGHTER(ft_800885A8,SMB_SOUND_EFFECT78)
SMB_AUDIO_FIGHTER(ft_80088640,SMB_SOUND_EFFECT90)
SMB_AUDIO_FIGHTER(ftCo_800886D8,SMB_SOUND_EFFECT114)
#undef SMB_AUDIO_FIGHTER
#define SMB_AUDIO_FIGHTER_STOP(name,type) \
    extern void smb_audio_orig_##name(Fighter*); \
    void name(Fighter* fp) { \
        smb_audio_fighter(fp,type,SMB_SOUND_STOP,0,64);smb_audio_depth++; \
        smb_audio_orig_##name(fp);smb_audio_depth--; \
    }
SMB_AUDIO_FIGHTER_STOP(ft_80088770,SMB_SOUND_VOICE30)
SMB_AUDIO_FIGHTER_STOP(ft_800887CC,SMB_SOUND_VOICE42)
SMB_AUDIO_FIGHTER_STOP(ft_80088828,SMB_SOUND_EFFECT54)
SMB_AUDIO_FIGHTER_STOP(ft_80088884,SMB_SOUND_EFFECT66)
SMB_AUDIO_FIGHTER_STOP(ft_800888E0,SMB_SOUND_EFFECT78)
SMB_AUDIO_FIGHTER_STOP(ft_8008893C,SMB_SOUND_EFFECT90)
SMB_AUDIO_FIGHTER_STOP(ft_80088998,SMB_SOUND_EFFECT114)
SMB_AUDIO_FIGHTER_STOP(ft_80088A50,SMB_SOUND_FIGHTER_STOP)
#undef SMB_AUDIO_FIGHTER_STOP
extern void smb_audio_orig_ft_80088C5C(Fighter_GObj*);
void ft_80088C5C(Fighter_GObj* object) {
    SmbSound* event=smb_audio_fighter(object?object->user_data:NULL,SMB_SOUND_FIGHTER_STOP,SMB_SOUND_STOP,0,64);
    if(event)event->flags|=2;
    smb_audio_depth++;smb_audio_orig_ft_80088C5C(object);smb_audio_depth--;
}
#define SMB_AUDIO_ITEM(name,type) \
    extern void smb_audio_orig_##name(Item*,enum_t,u8,u8); \
    void name(Item* item,enum_t sound,u8 volume,u8 pan) { \
        if(smb_audio_proxy(smb_audio_item_owner(item))&&(unsigned)sound!=SMB_SOUND_STOP&&(unsigned)sound!=SMB_SOUND_NONE)return; \
        smb_audio_item(item,type,sound,volume,pan);smb_audio_depth++; \
        smb_audio_orig_##name(item,sound,volume,pan);smb_audio_depth--; \
    }
SMB_AUDIO_ITEM(Item_8026AE84,SMB_SOUND_ITEM_ONESHOT)
SMB_AUDIO_ITEM(Item_8026AF0C,SMB_SOUND_ITEM_LOOP1)
SMB_AUDIO_ITEM(Item_8026AFA0,SMB_SOUND_ITEM_LOOP2)
#undef SMB_AUDIO_ITEM
#define SMB_AUDIO_ITEM_STOP(name,type) \
    extern void smb_audio_orig_##name(Item*); \
    void name(Item* item) { \
        smb_audio_item(item,type,SMB_SOUND_STOP,0,64);smb_audio_depth++; \
        smb_audio_orig_##name(item);smb_audio_depth--; \
    }
SMB_AUDIO_ITEM_STOP(Item_8026B034,SMB_SOUND_ITEM_LOOP1)
SMB_AUDIO_ITEM_STOP(Item_8026B074,SMB_SOUND_ITEM_LOOP2)
#undef SMB_AUDIO_ITEM_STOP

extern HSD_GObj* smb_audio_orig_lbAudioAx_800263E8(float,HSD_GObj*,int,int,int,int,int,int,int,int,int);
HSD_GObj* lbAudioAx_800263E8(float direction,HSD_GObj* owner,int behavior,int sound,
                            int start_volume,int end_volume,int pan_left,int pan_right,
                            int duration,int track,int voice) {
    Fighter* fp=owner&&ftLib_IsFighter(owner)?owner->user_data:NULL;
    if(smb_audio_proxy(fp))return NULL;
    if(fp&&!smb_audio_depth&&behavior>=0&&behavior<9&&sound>=0) {
        unsigned type=SMB_SOUND_ONESHOT;
        int retail=track-(fp->player_idx+(fp->is_sub_fighter?1:0));
        switch(retail) {
        case 30:type=SMB_SOUND_VOICE30;break;case 42:type=SMB_SOUND_VOICE42;break;
        case 54:type=SMB_SOUND_EFFECT54;break;case 66:type=SMB_SOUND_EFFECT66;break;
        case 78:type=SMB_SOUND_EFFECT78;break;case 90:type=SMB_SOUND_EFFECT90;break;
        case 114:type=SMB_SOUND_EFFECT114;break;
        }
        int volume=start_volume<0?0:start_volume;
        int pan=(pan_left+pan_right)/2;pan=pan<0?0:pan;
        SmbSound* event=smb_audio_emit(fp,0,type,(unsigned)sound,(unsigned)volume,(unsigned)pan,smb_audio_position(fp,fp->cur_pos));
        if(event&&duration>0){event->flags=1;event->duration=duration>60000?60000:duration;}
    }
    smb_audio_depth++;
    HSD_GObj* result=smb_audio_orig_lbAudioAx_800263E8(direction,owner,behavior,sound,start_volume,end_volume,pan_left,pan_right,duration,track,voice);
    smb_audio_depth--;return result;
}
#endif
