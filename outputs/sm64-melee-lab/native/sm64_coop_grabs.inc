/* A victim grants a bounded lease; the catcher runs native paired callbacks.
 * Every peer receives presentation, while only the victim owner resumes flight. */
#define SMB_WIRE_GRAB 5u
#define SMB_WIRE_IMPACT 6u
typedef struct {uint32_t catcher_global,catcher_actor,victim_global,victim_actor,source,buttons;float stick[2];SmbGrabState state;} SmbGrabWire;
static struct {unsigned used,at,level,area,act;SmbGrabWire wire;} coop_grabs[32];
static unsigned coop_grab_serial;
static struct{unsigned id,at;} coop_grab_terminal[128];static unsigned coop_grab_terminal_cursor;
static int smb_coop_grab_completed(unsigned id){for(unsigned i=0;i<128;i++)if(coop_grab_terminal[i].id==id&&frames-coop_grab_terminal[i].at<1800)return 1;return 0;}
static void smb_coop_grab_complete(unsigned id){unsigned i=coop_grab_terminal_cursor++%128;coop_grab_terminal[i].id=id;coop_grab_terminal[i].at=frames;}
static unsigned smb_coop_actor_seat(unsigned global,unsigned actor) {
    if(actor>3||!gNetworkPlayerLocal)return ~0u;
    struct NetworkPlayer* player=network_player_from_global_index(global);
    if(!player||!player->connected)return ~0u;
    if(player->currLevelNum!=gCurrLevelNum||player->currAreaIndex!=gCurrAreaIndex||player->currActNum!=gCurrActStarNum)return ~0u;
    if(actor&&global!=smb_coop_campaign_owner_for(gCurrLevelNum,gCurrAreaIndex,gCurrActStarNum))return ~0u;
    if(global==gNetworkPlayerLocal->globalIndex)return actor;
    return actor?actor:player->localIndex+3;
}
static int smb_coop_grab_owned(unsigned global,unsigned actor){return gNetworkPlayerLocal&&global==gNetworkPlayerLocal->globalIndex&&(!actor||smb_coop_campaign_authoritative());}
static int smb_coop_grab_slot(unsigned id,int create) {
    for(unsigned i=0;i<32;i++)if(coop_grabs[i].used&&coop_grabs[i].wire.state.id==id)return i;
    if(create)for(unsigned i=0;i<32;i++)if(!coop_grabs[i].used){memset(&coop_grabs[i],0,sizeof coop_grabs[i]);coop_grabs[i].used=1;coop_grabs[i].at=frames;coop_grabs[i].level=gCurrLevelNum;coop_grabs[i].area=gCurrAreaIndex;coop_grabs[i].act=gCurrActStarNum;return i;}
    return -1;
}
static void smb_coop_grab_send(const SmbGrabWire* wire,int reliable) {
    struct Packet packet;smb_coop_wire_begin(&packet,SMB_WIRE_GRAB,reliable,PLMT_AREA);packet_write(&packet,wire,sizeof *wire);if(!packet.writeError)network_send(&packet);
}
static void smb_coop_grab_receive(unsigned sender,const SmbGrabWire* incoming) {
    if(!incoming||incoming->state.version!=SMB_MAGIC||!incoming->state.id||incoming->state.phase>5||incoming->state.catcher_sub>1||incoming->state.victim_sub>1||
       !smb_coop_scene_peer(sender))return;
    unsigned global=gNetworkPlayers[sender].globalIndex;
    if(incoming->state.phase==0||incoming->state.phase==5){if(global!=incoming->victim_global)return;}
    else if(global!=incoming->catcher_global)return;
    unsigned catcher=smb_coop_actor_seat(incoming->catcher_global,incoming->catcher_actor),victim=smb_coop_actor_seat(incoming->victim_global,incoming->victim_actor);
    if(catcher==~0u||victim==~0u||(catcher==victim&&incoming->state.catcher_sub==incoming->state.victim_sub))return;
    if(smb_coop_grab_completed(incoming->state.id))return;
    if(incoming->state.phase==0&&!smb_coop_grab_owned(incoming->catcher_global,incoming->catcher_actor))return;
    int slot=smb_coop_grab_slot(incoming->state.id,incoming->state.phase<=1);if(slot<0)return;
    SmbGrabWire wire=*incoming;wire.state.catcher=catcher;wire.state.victim=victim;
    int catcher_owned=smb_coop_grab_owned(wire.catcher_global,wire.catcher_actor);
    int victim_owned=smb_coop_grab_owned(wire.victim_global,wire.victim_actor);
    if(wire.state.phase==0) {
        if(!catcher_owned)return;
        int result=embedded_grab_begin?embedded_grab_begin(wire.state.id,catcher,wire.state.catcher_sub,victim,wire.state.victim_sub):-1;
        if(!result&&!embedded_grab_snapshot(wire.state.id,&wire.state)){coop_grabs[slot].wire=wire;coop_grabs[slot].at=frames;smb_coop_grab_send(&wire,1);}
        else {wire.state.phase=4;smb_coop_grab_send(&wire,1);smb_coop_grab_complete(wire.state.id);coop_grabs[slot].used=0;}
        fprintf(stderr,"SMB COOP GRAB GRANT id=%u catcher=%u victim=%u result=%d\n",wire.state.id,wire.catcher_global,wire.victim_global,result);return;
    }
    if(wire.state.phase==5) {
        if(catcher_owned&&embedded_grab_mash)embedded_grab_mash(wire.state.id,wire.buttons,wire.stick[0],wire.stick[1]);
        coop_grabs[slot].at=frames;return;
    }
    if(catcher_owned)return;
    coop_grabs[slot].wire=wire;coop_grabs[slot].at=frames;
    int result=embedded_grab_present?embedded_grab_present(wire.state.id,victim,wire.state.victim_sub,&wire.state):-1;
    if(wire.state.phase!=1){if(embedded_grab_end)embedded_grab_end(wire.state.id,victim_owned);smb_coop_grab_complete(wire.state.id);coop_grabs[slot].used=0;}
    static unsigned traces;if(traces++<30||wire.state.phase!=1)fprintf(stderr,"SMB COOP GRAB PRESENT id=%u phase=%u victim_owned=%d result=%d percent=%.2f\n",wire.state.id,wire.state.phase,victim_owned,result,wire.state.victim_state.percent);
}
static void smb_coop_impact_receive(unsigned sender,const uint32_t data[4],const float point[3]) {
    if(!smb_coop_scene_peer(sender)||data[0]!=gNetworkPlayerLocal->globalIndex||data[1]>3||data[3]>3)return;
    if(data[1]&&!smb_coop_campaign_authoritative())return;
    for(unsigned k=0;k<3;k++)if(!isfinite(point[k])||fabsf(point[k])>10000)return;
    if(data[1]==0&&embedded_impact)embedded_impact(data[2],data[3],point);
    if(!(data[2]&0x80000000u)&&embedded_contact)embedded_contact(data[2]);
}
static void smb_coop_grabs_update(void) {
    if(!embedded_grab_begin||!embedded_net_event||!gNetworkPlayerLocal)return;
    unsigned global=gNetworkPlayerLocal->globalIndex;
    for(unsigned type=1;type<=2;type++) {
        uint32_t data[5];float point[3];
        for(unsigned n=0;n<32&&embedded_net_event(type,data,point)>0;n++) {
            unsigned peer=data[0],catcher_global,catcher_actor;
            if(!peer){if(type==1&&!(data[1]&0x80000000u)&&embedded_contact)embedded_contact(data[1]);continue;}
            if(peer<16){if(!smb_coop_scene_peer(peer))continue;catcher_global=gNetworkPlayers[peer].globalIndex;catcher_actor=0;}
            else {catcher_global=smb_coop_campaign_owner_for(gCurrLevelNum,gCurrAreaIndex,gCurrActStarNum);catcher_actor=peer-15;}
            if(type==1) {
                uint32_t impact[4]={catcher_global,catcher_actor,data[1],data[2]};
                struct Packet packet;smb_coop_wire_begin(&packet,SMB_WIRE_IMPACT,1,PLMT_AREA);packet_write(&packet,impact,sizeof impact);packet_write(&packet,point,sizeof point);network_send(&packet);continue;
            }
            if(data[3]>3)continue; /* A currently borrowed victim already has a lease. */
            SmbGrabWire wire={0};wire.catcher_global=catcher_global;wire.catcher_actor=catcher_actor;wire.victim_global=global;wire.victim_actor=data[3];wire.source=data[1];
            if(!coop_grab_serial)coop_grab_serial=(unsigned)SDL_GetPerformanceCounter()&0xffffff;
            wire.state.version=SMB_MAGIC;wire.state.id=((global+1)<<24)|(++coop_grab_serial&0xffffff);wire.state.catcher_sub=data[2];wire.state.victim_sub=data[4];
            int slot=smb_coop_grab_slot(wire.state.id,1);if(slot>=0){coop_grabs[slot].wire=wire;smb_coop_grab_send(&wire,1);fprintf(stderr,"SMB COOP GRAB REQUEST id=%u catcher=%u actor=%u victim=%u/%u\n",wire.state.id,catcher_global,catcher_actor,global,data[3]);}
        }
    }
    for(unsigned i=0;i<32;i++)if(coop_grabs[i].used) {
        SmbGrabWire* wire=&coop_grabs[i].wire;unsigned catcher=smb_coop_actor_seat(wire->catcher_global,wire->catcher_actor),victim=smb_coop_actor_seat(wire->victim_global,wire->victim_actor);
        int catcher_owned=smb_coop_grab_owned(wire->catcher_global,wire->catcher_actor),victim_owned=smb_coop_grab_owned(wire->victim_global,wire->victim_actor);
        if(catcher==~0u||victim==~0u||coop_grabs[i].level!=(unsigned)gCurrLevelNum||coop_grabs[i].area!=(unsigned)gCurrAreaIndex||coop_grabs[i].act!=(unsigned)gCurrActStarNum||frames-coop_grabs[i].at>60) {
            if(embedded_grab_end)embedded_grab_end(wire->state.id,victim_owned);smb_coop_grab_complete(wire->state.id);coop_grabs[i].used=0;continue;
        }
        if(catcher_owned&&wire->state.phase==1) {
            if(embedded_grab_snapshot(wire->state.id,&wire->state)){coop_grabs[i].used=0;continue;}
            smb_coop_grab_send(wire,wire->state.phase!=1);coop_grabs[i].at=frames;
            if(wire->state.phase!=1){embedded_grab_end(wire->state.id,0);smb_coop_grab_complete(wire->state.id);coop_grabs[i].used=0;}
        } else if(victim_owned&&wire->state.phase==1) {
            SmbGrabWire mash=*wire;mash.state.phase=5;
            if(embedded_grab_input&&!embedded_grab_input(wire->state.id,&mash.buttons,mash.stick))smb_coop_grab_send(&mash,0);
        }
    }
}
