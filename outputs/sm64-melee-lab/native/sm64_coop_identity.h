/* Transport identity checks shared by the packet gateway and native payloads.
 * A socket client receives peer broadcasts through its trusted host. The host
 * must authenticate each original claim before forwarding it. CoopNet peers
 * arrive directly with a transport-assigned local index. */
#ifndef SMB_COOP_IDENTITY_H
#define SMB_COOP_IDENTITY_H
static int smb_net_from_server(const struct Packet* packet) {
    if(!packet||gNetworkType!=NT_CLIENT||!gNetworkSystem)return 0;
    if(gNetworkPlayerServer&&packet->localIndex==gNetworkPlayerServer->localIndex)return 1;
    return packet->localIndex==UNKNOWN_LOCAL_INDEX&&gNetworkServerAddr&&packet->addr&&
        gNetworkSystem->match_addr(gNetworkServerAddr,packet->addr);
}
static int smb_net_sender_allowed(const struct Packet* packet,unsigned global) {
    if(!packet||!gNetworkSystem||global>=MAX_PLAYERS||!gNetworkPlayerLocal||
       global==gNetworkPlayerLocal->globalIndex)return 0;
    if(gNetworkSystem->requireServerBroadcast&&gNetworkType==NT_CLIENT) {
        if(!smb_net_from_server(packet))return 0;
        struct NetworkPlayer* claimed=network_player_from_global_index(global);
        return claimed&&claimed->connected&&claimed->localIndex<MAX_PLAYERS;
    }
    unsigned local=packet->localIndex;
    return local>0&&local<MAX_PLAYERS&&gNetworkPlayers[local].connected&&
        gNetworkPlayers[local].globalIndex==global;
}
static int smb_net_gateway_allowed(const struct Packet* packet) {
    if(!packet||packet->error)return 0;
    if((unsigned)packet->packetType>PACKET_MODERATOR&&packet->packetType!=PACKET_CUSTOM)return 0;
    if(packet->orderedGroupId) {
        /* Before admission the sole ordered stream is the host's empty mod
         * handshake. It is never accepted from an arbitrary unknown sender. */
        if(!smb_net_sender_allowed(packet,packet->orderedFromGlobalId)&&
           !(packet->orderedFromGlobalId==0&&smb_net_from_server(packet)))return 0;
    }
    unsigned cursor=packet->cursor;
    if(packet->packetType==PACKET_CUSTOM) {
        if(cursor>packet->dataLength||packet->dataLength-cursor<9)return 0;
        return smb_net_sender_allowed(packet,packet->buffer[cursor+8]);
    }
    if(packet->packetType==PACKET_PLAYER) {
        if(cursor>=packet->dataLength)return 0;
        return smb_net_sender_allowed(packet,packet->buffer[cursor]);
    }
    return 1;
}
#endif
