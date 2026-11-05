/* Bounded native-state messages share Coop Deluxe's existing transport.
 * Articles and hitboxes have independent sequences, so one dropped fragment
 * cannot mix two frames or block character/player movement packets. */
#define SMB_WIRE_ARTICLES 1u
#define SMB_WIRE_HITS 2u
#define SMB_WIRE_CAMPAIGN 3u
#define SMB_WIRE_SPAWN 4u
#define SMB_WIRE_SOUNDS 7u
#define SMB_WIRE_CHUNK 20u
#include "sm64_coop_identity.h"
_Static_assert(SMB_WIRE_CHUNK*sizeof(SmbItem)+64<PACKET_LENGTH,"Article chunk exceeds network packet");
_Static_assert(SMB_WIRE_CHUNK*sizeof(SmbHit)+64<PACKET_LENGTH,"Hit chunk exceeds network packet");
static struct {
    unsigned owner_global,owner_level,owner_area,owner_act,identified;
    unsigned item_seq,hit_seq,item_total,hit_total,item_at,hit_at;
    uint64_t item_mask;uint32_t hit_mask;
    SmbItem items[SMB_ITEMS];SmbHit hits[SMB_HITS];
    SmbItem pending_items[SMB_ITEMS];SmbHit pending_hits[SMB_HITS];
    unsigned pending_item_seq,pending_hit_seq,pending_item_total,pending_hit_total;
    uint64_t pending_item_mask;uint32_t pending_hit_mask;
} coop_native_frames[19];
static unsigned coop_wire_sequence;
static int smb_coop_scene_peer(unsigned local) {
    if(local>=16||!local||!gNetworkPlayers[local].connected||!gNetworkPlayerLocal)return 0;
    return gNetworkPlayers[local].currLevelNum==gNetworkPlayerLocal->currLevelNum&&
        gNetworkPlayers[local].currAreaIndex==gNetworkPlayerLocal->currAreaIndex&&gNetworkPlayers[local].currActNum==gNetworkPlayerLocal->currActNum;
}
static void smb_coop_wire_begin(struct Packet* packet,unsigned type,int reliable,enum PacketLevelMatchType match) {
    memset(packet,0,sizeof *packet);packet_init(packet,PACKET_CUSTOM,reliable,match);
    uint32_t magic=SMB_MAGIC,kind=type;uint8_t sender=gNetworkPlayerLocal->globalIndex;
    packet_write(packet,&magic,sizeof magic);packet_write(packet,&kind,sizeof kind);packet_write(packet,&sender,sizeof sender);
}
static void smb_coop_wire_frame(unsigned actor,const SmbNetFrame* frame) {
    for(unsigned type=SMB_WIRE_ARTICLES;type<=SMB_WIRE_HITS;type++) {
        unsigned total=type==SMB_WIRE_ARTICLES?frame->item_count:frame->hit_count;
        size_t size=type==SMB_WIRE_ARTICLES?sizeof(SmbItem):sizeof(SmbHit);
        const void* data=type==SMB_WIRE_ARTICLES?(const void*)frame->item:(const void*)frame->hit;
        for(unsigned offset=0;;offset+=SMB_WIRE_CHUNK) {
            unsigned count=total-offset;if(count>SMB_WIRE_CHUNK)count=SMB_WIRE_CHUNK;
            struct Packet packet;smb_coop_wire_begin(&packet,type,0,PLMT_AREA);
            uint32_t header[5]={actor,frame->sequence,total,offset,count};packet_write(&packet,header,sizeof header);
            if(count)packet_write(&packet,(const char*)data+offset*size,count*size);
            if(!packet.writeError)network_send(&packet);
            if(offset+count>=total)break;
        }
    }
}
#include "sm64_coop_grabs.inc"
void smb_coop_packet_receive(struct Packet* packet) {
    uint32_t magic=0,type=0;uint8_t global=255;
    packet_read(packet,&magic,sizeof magic);packet_read(packet,&type,sizeof type);packet_read(packet,&global,sizeof global);
    if(packet->error||magic!=SMB_MAGIC||!smb_net_sender_allowed(packet,global))return;
    struct NetworkPlayer* sender=network_player_from_global_index(global);
    if(!sender||!sender->connected||sender->localIndex>=16)return;
    unsigned local=sender->localIndex;
    if(type==SMB_WIRE_SOUNDS){uint32_t count;packet_read(packet,&count,sizeof count);if(packet->error||count>64||!smb_coop_scene_peer(local))return;SmbSound events[64];packet_read(packet,events,count*sizeof(SmbSound));if(!packet->error&&embedded_remote_sounds)embedded_remote_sounds(local,events,count);return;}
    if(type==SMB_WIRE_GRAB){SmbGrabWire wire;packet_read(packet,&wire,sizeof wire);if(!packet->error)smb_coop_grab_receive(local,&wire);return;}
    if(type==SMB_WIRE_IMPACT){uint32_t data[4];float point[3];packet_read(packet,data,sizeof data);packet_read(packet,point,sizeof point);if(!packet->error)smb_coop_impact_receive(local,data,point);return;}
    if(type==SMB_WIRE_CAMPAIGN) {
        SmbCampaign campaign;packet_read(packet,&campaign,sizeof campaign);if(!packet->error)smb_coop_campaign_receive(local,&campaign);return;
    }
    if(type==SMB_WIRE_SPAWN) {
        uint32_t request[3];packet_read(packet,request,sizeof request);if(!packet->error)smb_coop_campaign_spawn_receive(local,request);return;
    }
    if((type!=SMB_WIRE_ARTICLES&&type!=SMB_WIRE_HITS)||!smb_coop_scene_peer(local))return;
    uint32_t header[5];packet_read(packet,header,sizeof header);if(packet->error)return;
    unsigned actor=header[0],serial=header[1],total=header[2],offset=header[3],count=header[4];
    unsigned maximum=type==SMB_WIRE_ARTICLES?SMB_ITEMS:SMB_HITS;
    if(actor>3||total>maximum||offset>total||count>SMB_WIRE_CHUNK||count>total-offset||offset%SMB_WIRE_CHUNK||count!=(total-offset>SMB_WIRE_CHUNK?SMB_WIRE_CHUNK:total-offset)||(total&&offset==total))return;
    if(actor&&!smb_coop_campaign_same_scene(sender))return;
    if(actor&&sender->globalIndex!=smb_coop_campaign_owner_for(gCurrLevelNum,gCurrAreaIndex,gCurrActStarNum))return;
    unsigned peer=actor?actor+15:local;
    typeof(coop_native_frames[0])* target=&coop_native_frames[peer];
    if(!target->identified||target->owner_global!=global||target->owner_level!=(unsigned)gCurrLevelNum||target->owner_area!=(unsigned)gCurrAreaIndex||target->owner_act!=(unsigned)gCurrActStarNum){
        if(embedded_remote_articles)embedded_remote_articles(peer,NULL,0);if(embedded_net_hits)embedded_net_hits(peer,NULL,0);memset(target,0,sizeof *target);
        target->identified=1;target->owner_global=global;target->owner_level=gCurrLevelNum;target->owner_area=gCurrAreaIndex;target->owner_act=gCurrActStarNum;
    }
    if(type==SMB_WIRE_ARTICLES) {
        if(serial!=target->pending_item_seq) {
            if((int32_t)(serial-target->pending_item_seq)<0)return;
            target->pending_item_seq=serial;target->pending_item_total=total;target->pending_item_mask=0;
        }
        if(total!=target->pending_item_total)return;
        if(count)packet_read(packet,&target->pending_items[offset],count*sizeof(SmbItem));if(packet->error)return;
        unsigned chunks=(total+SMB_WIRE_CHUNK-1)/SMB_WIRE_CHUNK;
        target->pending_item_mask|=1ull<<(offset/SMB_WIRE_CHUNK);
        if(!total||target->pending_item_mask==((1ull<<chunks)-1)) {
            memcpy(target->items,target->pending_items,total*sizeof(SmbItem));target->item_total=total;target->item_seq=serial;target->item_at=frames;
            if(embedded_remote_articles)embedded_remote_articles(peer,target->items,total);
            static unsigned traces;if(total&&traces++<24)fprintf(stderr,"SMB COOP ARTICLES peer=%u count=%u sequence=%u\n",peer,total,serial);
        }
    } else {
        if(serial!=target->pending_hit_seq) {
            if((int32_t)(serial-target->pending_hit_seq)<0)return;
            target->pending_hit_seq=serial;target->pending_hit_total=total;target->pending_hit_mask=0;
        }
        if(total!=target->pending_hit_total)return;
        if(count)packet_read(packet,&target->pending_hits[offset],count*sizeof(SmbHit));if(packet->error)return;
        unsigned chunks=(total+SMB_WIRE_CHUNK-1)/SMB_WIRE_CHUNK;
        target->pending_hit_mask|=1u<<(offset/SMB_WIRE_CHUNK);
        if(!total||target->pending_hit_mask==((1u<<chunks)-1)) {
            memcpy(target->hits,target->pending_hits,total*sizeof(SmbHit));target->hit_total=total;target->hit_seq=serial;target->hit_at=frames;
        }
    }
}
static void smb_coop_transport_update(void) {
    static unsigned audio_active;unsigned wants_audio=embedded_step&&gNetworkType!=NT_NONE;
    if(embedded_audio_enabled&&wants_audio!=audio_active){embedded_audio_enabled(wants_audio);audio_active=wants_audio;}
    if(!embedded_step||!embedded_net_snapshot||!gNetworkPlayerLocal||gNetworkType==NT_NONE)return;
    static unsigned last_frame=~0u;if(last_frame==frames)return;last_frame=frames;
    for(unsigned peer=1;peer<=18;peer++) {
        typeof(coop_native_frames[0])* state=&coop_native_frames[peer];
        int valid=peer<16?smb_coop_scene_peer(peer):!smb_coop_campaign_authoritative()&&smb_npc_count()>peer-16;
        if(!valid||frames-state->item_at>15) {
            if(state->item_total&&embedded_remote_articles)embedded_remote_articles(peer,NULL,0);state->item_total=0;
        }
        if(valid&&frames-state->hit_at<=5&&state->hit_total&&embedded_net_hits)embedded_net_hits(peer,state->hits,state->hit_total);
        if(!valid) {if(peer<16&&embedded_remote_sounds)embedded_remote_sounds(peer,NULL,0);if(embedded_net_hits)embedded_net_hits(peer,NULL,0);memset(state,0,sizeof *state);}
    }
    smb_coop_grabs_update();
    unsigned others=0;for(unsigned i=1;i<16;i++)others+=gNetworkPlayers[i].connected;
    if(embedded_sounds)for(unsigned batch=0;batch<8;batch++){SmbSound events[64];uint32_t count=embedded_sounds(UINT32_MAX,events,64);if(!count)break;if(others){struct Packet packet;smb_coop_wire_begin(&packet,SMB_WIRE_SOUNDS,1,PLMT_AREA);packet_write(&packet,&count,sizeof count);packet_write(&packet,events,count*sizeof(SmbSound));network_send(&packet);}}
    if(!others)return;
    SmbNetFrame frame;
    if(!embedded_net_snapshot(0,&frame)){if(embedded_net_hits)embedded_net_hits(0,frame.hit,frame.hit_count);smb_coop_wire_frame(0,&frame);}
    if(smb_coop_campaign_authoritative()) {
        for(unsigned i=1;i<=smb_npc_count();i++)if(!embedded_net_snapshot(i,&frame))smb_coop_wire_frame(i,&frame);
        if(frames%3==0) {
            SmbCampaign campaign;smb_coop_campaign_send(&campaign);
            if(campaign.version==SMB_MAGIC){struct Packet packet;smb_coop_wire_begin(&packet,SMB_WIRE_CAMPAIGN,0,PLMT_NONE);packet_write(&packet,&campaign,sizeof campaign);if(!packet.writeError)network_send(&packet);}
        }
    }
    uint32_t request[3];if(smb_coop_campaign_take_spawn(request)) {
        struct Packet packet;smb_coop_wire_begin(&packet,SMB_WIRE_SPAWN,1,PLMT_AREA);packet_write(&packet,request,sizeof request);network_send(&packet);
    }
    if(++coop_wire_sequence%120==0)fprintf(stderr,"SMB COOP NATIVE SYNC peers=%u sequence=%u campaign_authority=%d\n",others,coop_wire_sequence,smb_coop_campaign_authoritative());
}
