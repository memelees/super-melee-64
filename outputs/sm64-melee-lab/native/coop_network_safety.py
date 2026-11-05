"""Render fixed-content network safety patches from pinned upstream sources.

This module performs no writes. Preparation and the asset-free sanitizer
verifier use the same rendered production functions.
"""
from pathlib import Path
import re

HERE=Path(__file__).resolve().parent

def one(text,before,after):
    if text.count(before)!=1:raise ValueError('Network safety boundary: '+before[:90])
    return text.replace(before,after)

def function(text,name,replacement):
    match=re.search(r'^\w[^\n;]*\b'+re.escape(name)+r'\([^;]*?\)\s*\{',text,re.M)
    if not match:raise ValueError('Network safety function: '+name)
    start=match.start();at=match.end();depth=1
    # Ignore comments and quoted strings while finding the complete function.
    token=re.compile(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]',re.S)
    for item in token.finditer(text,at):
        if item.group()=='{':depth+=1
        elif item.group()=='}':
            depth-=1
            if not depth:return text[:start]+replacement+text[item.end():]
    raise ValueError('Unclosed function: '+name)

def patched_sources(original):
    result={}
    path='src/pc/network/packets/packet_read_write.c';source=original(path)
    source=function(source,'packet_write',r'''void packet_write(struct Packet* packet, void* data, u16 length) {
    if(!packet)return;
    if(packet->error||packet->writeError)return;
    if(!length)return; /* Native callers use NULL,0 for an empty field. */
    if(!data||packet->cursor>PACKET_LENGTH-sizeof(u32)||
       length>PACKET_LENGTH-sizeof(u32)-packet->cursor) {
        packet->error=true;packet->writeError=true;return;
    }
    memcpy(packet->buffer+packet->cursor,data,length);
    packet->cursor+=length;packet->dataLength=packet->cursor;
}''')
    source=function(source,'packet_read',r'''void packet_read(struct Packet* packet, void* data, u16 length) {
    if(!packet||packet->error)return;
    if(!length)return;
    if(!data||packet->dataLength>PACKET_LENGTH-sizeof(u32)||
       packet->cursor>packet->dataLength||length>packet->dataLength-packet->cursor) {
        packet->error=true;return;
    }
    memcpy(data,packet->buffer+packet->cursor,length);packet->cursor+=length;
}''')
    source=one(source,'u8 packet_initial_read(struct Packet* packet) {',r'''u8 packet_initial_read(struct Packet* packet) {
    if(!packet||packet->error||packet->dataLength<5||packet->dataLength>PACKET_LENGTH-sizeof(u32))return FALSE;
    packet->cursor=3;packet->orderedFromGlobalId=0;packet->orderedGroupId=0;packet->orderedSeqId=0;
    packet->courseNum=packet->actNum=packet->areaIndex=0;packet->levelNum=0;''')
    source=one(source,'    // don\'t drop packet\n    return TRUE;',r'''    if((flags&0xf0)||(packet->levelAreaMustMatch&&packet->levelMustMatch)||
       (packetIsOrdered&&(!packet->orderedGroupId||!packet->orderedSeqId||packet->orderedFromGlobalId>=MAX_PLAYERS)))packet->error=true;
    return !packet->error;''')
    source=one(source,'u32 packet_hash(struct Packet* packet) {','u32 packet_hash(struct Packet* packet) {\n    if(!packet||packet->dataLength>PACKET_LENGTH-sizeof(u32))return 0;')
    source=one(source,'bool packet_check_hash(struct Packet* packet) {','bool packet_check_hash(struct Packet* packet) {\n    if(!packet||packet->error||packet->dataLength<5||packet->dataLength>PACKET_LENGTH-sizeof(u32))return false;')
    source=one(source,'    memset(dstPacket->buffer, 0, PACKET_LENGTH);',r'''    if(!srcPacket||!dstPacket)return;
    if(srcPacket->error||srcPacket->writeError||srcPacket->dataLength<5||srcPacket->dataLength>PACKET_LENGTH-sizeof(u32)) {
        memset(dstPacket,0,sizeof *dstPacket);dstPacket->error=true;dstPacket->writeError=true;return;
    }
    memset(dstPacket->buffer, 0, PACKET_LENGTH);''')
    source=one(source,'    dstPacket->orderedGroupId = srcPacket->orderedGroupId;','    dstPacket->orderedFromGlobalId = srcPacket->orderedFromGlobalId;\n    dstPacket->orderedGroupId = srcPacket->orderedGroupId;')
    source=one(source,'    u16* seqId = (u16*)&packet->buffer[PACKET_ORDERED_SEQ_ID_OFFSET];\n    *seqId = packet->orderedSeqId;','    memcpy(&packet->buffer[PACKET_ORDERED_SEQ_ID_OFFSET],&packet->orderedSeqId,sizeof packet->orderedSeqId);')
    result[path]=source
    path='src/pc/network/packets/packet.c';source=original(path)
    source=one(source,'#include "../network.h"','#include "../network.h"\n#include "../../sm64_coop_identity.h"')
    source=function(source,'packet_compress',r'''void packet_compress(struct Packet* p, u8** compBuffer, u32* compSize) {
    if(!compBuffer||!compSize)return;
    *compBuffer=NULL;*compSize=0;
    if(!p||p->error||p->writeError||p->dataLength<5||p->dataLength>PACKET_LENGTH-sizeof(u32))return;
    uLong sourceSize=p->dataLength+sizeof(u32);uLongf compressedLen=compressBound(sourceSize);
    increase_comp_buffer(compressedLen);
    if(sCompBuffer&&compress2(sCompBuffer,&compressedLen,p->buffer,sourceSize,Z_BEST_COMPRESSION)==Z_OK) {
        *compBuffer=sCompBuffer;*compSize=compressedLen;
    }
}''')
    source=function(source,'packet_decompress',r'''bool packet_decompress(struct Packet* p, u8* compBuffer, u32 compSize) {
    if(!p||!compBuffer||!compSize||compSize>compressBound(PACKET_LENGTH))return false;
    p->dataLength=0;uLongf decompSize=PACKET_LENGTH;
    if(uncompress(p->buffer,&decompSize,compBuffer,compSize)!=Z_OK||decompSize<5+sizeof(u32)||decompSize>PACKET_LENGTH)return false;
    p->dataLength=(u16)(decompSize-sizeof(u32));return true;
}''')
    source=one(source,'void packet_process(struct Packet* p) {\n    if (gNetworkType == NT_NONE) { return; }','void packet_process(struct Packet* p) {\n    if (!p||p->error||gNetworkType == NT_NONE) { return; }')
    source=one(source,'void packet_receive(struct Packet* p) {\n    u8 packetType = (u8)p->buffer[0];',r'''void packet_receive(struct Packet* p) {
    if(!packet_initial_read(p)||!smb_net_gateway_allowed(p)) {
        static unsigned rejected;if(getenv("SMB_SMOKE")&&rejected++<16)fprintf(stderr,"SMB NET GATE reject=1 role=%u type=%u local=%u length=%u error=%u ordered=%u from=%u expected_server=%d\n",gNetworkType,p?p->packetType:0,p?p->localIndex:0,p?p->dataLength:0,p?p->error:0,p?p->orderedGroupId:0,p?p->orderedFromGlobalId:0,p?smb_net_from_server(p):0);
        return;
    }
    u8 packetType = (u8)p->buffer[0];
    /* The fixed-content edition has no remote script or file-download path. */
    if(packetType==PACKET_MOD_LIST_ENTRY||packetType==PACKET_MOD_LIST_FILE||
       packetType==PACKET_DOWNLOAD_REQUEST||packetType==PACKET_DOWNLOAD||
       packetType==PACKET_LUA_SYNC_TABLE_REQUEST||packetType==PACKET_LUA_SYNC_TABLE||
       packetType==PACKET_LUA_CUSTOM||packetType==PACKET_LUA_CUSTOM_BYTESTRING)return;''')
    source=one(source,'    if (packet_initial_read(p)) {','    if (!p->error) {')
    source=function(source,'packet_spoofed','bool packet_spoofed(struct Packet* p, u8 globalIndex) {\n    return !smb_net_sender_allowed(p,globalIndex);\n}')
    result[path]=source
    path='src/pc/network/packets/packet_ordered.c';source=original(path)
    source=one(source,'    u8 globalId = p->orderedFromGlobalId;\n    if (globalId > MAX_PLAYERS) { return; }', '    if(!p||p->error)return;\n    u8 globalId = p->orderedFromGlobalId;\n    if (globalId >= MAX_PLAYERS) { return; }')
    source=one(source,'    opl = calloc(1, sizeof(struct OrderedPacketList));','    opl = calloc(1, sizeof(struct OrderedPacketList));\n    if(!opl)return;')
    source=one(source,'    opt = malloc(sizeof(struct OrderedPacketTable));','    opt = malloc(sizeof(struct OrderedPacketTable));\n    if(!opt)return;')
    source=source.replace('if (globalIndex > MAX_PLAYERS)', 'if (globalIndex >= MAX_PLAYERS)')
    result[path]=source
    path='src/pc/network/network.c';source=original(path)
    # Direct transports pin the requested server during initialize. Clear the
    # previous session first, so that the new authenticated destination survives
    # until the pre-admission empty-content handshake arrives.
    cleanup='    if (gNetworkServerAddr != NULL) {\n        free(gNetworkServerAddr);\n        gNetworkServerAddr = NULL;\n    }\n'
    source=one(source,cleanup+'\n    // set network type','    // set network type')
    source=one(source,'    int rc = gNetworkSystem->initialize(inNetworkType, reconnecting);',cleanup+'    int rc = gNetworkSystem->initialize(inNetworkType, reconnecting);')
    source=one(source,'void network_receive(u8 localIndex, void* addr, u8* data, u16 dataLength) {',r'''void network_receive(u8 localIndex, void* addr, u8* data, u16 dataLength) {
    if(!gNetworkSystem||!data||!dataLength||dataLength>PACKET_LENGTH+64||
       (localIndex>=MAX_PLAYERS&&localIndex!=UNKNOWN_LOCAL_INDEX))return;''')
    source=one(source,'    // set destination\n',r'''    if(p->error||p->writeError||p->dataLength<5||p->dataLength>PACKET_LENGTH-sizeof(u32)||
       (localIndex>=MAX_PLAYERS&&localIndex!=PACKET_DESTINATION_SERVER))return;
    // set destination
''')
    result[path]=source
    path='src/pc/network/socket/socket.c';source=original(path)
    source=source.replace('u8 data[PACKET_LENGTH + 1];','u8 data[PACKET_LENGTH + 64];').replace('data, PACKET_LENGTH + 1,','data, sizeof data,').replace('        SOFT_ASSERT(dataLength < PACKET_LENGTH);','        if(dataLength>PACKET_LENGTH+63)continue;')
    source=one(source,'        sAddr[0].sin6_addr = addr.sin6_addr;','        sAddr[0].sin6_addr = addr.sin6_addr;\n        if(gNetworkServerAddr)free(gNetworkServerAddr);\n        gNetworkServerAddr=network_duplicate_address(0);')
    source=function(source,'ns_socket_match_addr',r'''static bool ns_socket_match_addr(void* addr1, void* addr2) {
    if(!addr1||!addr2)return false;
    const struct sockaddr_in6* a=addr1;const struct sockaddr_in6* b=addr2;
    /* BSD recvfrom fills sin6_len; padding/flowinfo are not endpoint identity. */
    return a->sin6_family==b->sin6_family&&a->sin6_port==b->sin6_port&&
        a->sin6_scope_id==b->sin6_scope_id&&!memcmp(&a->sin6_addr,&b->sin6_addr,sizeof a->sin6_addr);
}''')
    result[path]=source
    path='src/pc/network/coopnet/coopnet.c';source=original(path)
    source=one(source,'static void coopnet_on_receive(uint64_t userId, const uint8_t* data, uint64_t dataLength) {','static void coopnet_on_receive(uint64_t userId, const uint8_t* data, uint64_t dataLength) {\n    if(!data||!dataLength||dataLength>PACKET_LENGTH+64)return;')
    source=one(source,'    if (userId == coopnet_get_local_user_id() && gNetworkType == NT_CLIENT) {\n        network_send_mod_list_request();','    if (userId == coopnet_get_local_user_id() && gNetworkType == NT_CLIENT) {\n        if(gNetworkServerAddr)free(gNetworkServerAddr);\n        gNetworkServerAddr=network_duplicate_address(0);\n        network_send_mod_list_request();')
    result[path]=source
    result['src/pc/sm64_coop_identity.h']=(HERE/'sm64_coop_identity.h').read_text()
    result.update(fixed_mod_sources(original))
    path='src/pc/network/packets/packet_join.c';source=original(path)
    source=one(source,'#include "../network.h"','#include "../network.h"\n#include "../../sm64_coop_identity.h"')
    source=function(source,'network_receive_join_request',r'''void network_receive_join_request(struct Packet* p) {
    if(!p||p->error||gNetworkType!=NT_SERVER||p->cursor>p->dataLength||
       p->dataLength-p->cursor!=MAX_VERSION_LENGTH+1+sizeof(struct PlayerPalette)+MAX_CONFIG_STRING)return;
    char version[MAX_VERSION_LENGTH]={0},expected[MAX_VERSION_LENGTH]={0};
    packet_read(p,version,sizeof version);snprintf(expected,sizeof expected,"%s",get_version());
    if(p->error||memcmp(version,expected,sizeof version))return;
    packet_read(p,&sJoinRequestPlayerModel,sizeof(u8));packet_read(p,&sJoinRequestPlayerPalette,sizeof sJoinRequestPlayerPalette);
    packet_read(p,sJoinRequestPlayerName,sizeof sJoinRequestPlayerName);
    if(p->error)return;sJoinRequestPlayerName[sizeof sJoinRequestPlayerName-1]=0;
    memset(sJoinRequestDiscordId,0,sizeof sJoinRequestDiscordId);network_send_join(p);
}''')
    source=one(source,'void network_receive_join(struct Packet* p) {',r'''void network_receive_join(struct Packet* p) {
    if(!p||p->error||gNetworkType!=NT_CLIENT||!smb_net_from_server(p)||p->cursor>p->dataLength||
       p->dataLength-p->cursor!=MAX_VERSION_LENGTH+1+sizeof(s16)+11+sizeof eeprom)return;''')
    source=one(source,'    if (memcmp(version, remoteVersion, MAX_VERSION_LENGTH) != 0) {','    if (memcmp(version, remoteVersion, MAX_VERSION_LENGTH) != 0) {\n        remoteVersion[MAX_VERSION_LENGTH-1]=0; /* Bound the error message too. */')
    source=one(source,'    network_player_connected(NPT_SERVER, 0, 0, &DEFAULT_MARIO_PALETTE, "Player", "0");',r'''    if(p->error||!myGlobalIndex||myGlobalIndex>=MAX_PLAYERS||gCurrSaveFileNum<1||gCurrSaveFileNum>4||
       gServerSettings.maxPlayers<2||gServerSettings.maxPlayers>MAX_PLAYERS)return;
    network_player_connected(NPT_SERVER, 0, 0, &DEFAULT_MARIO_PALETTE, "Player", "0");''')
    result[path]=source
    path='src/pc/network/packets/packet_lua_sync_table.c';source=original(path)
    source=function(source,'network_send_lua_sync_table_request','void network_send_lua_sync_table_request(void) { /* Fixed native content: no Lua exchange. */ }')
    result[path]=source
    path='src/pc/network/packets/packet_network_players.c';source=original(path)
    source=one(source,'#include "../network.h"','#include "../network.h"\n#include "../../sm64_coop_identity.h"')
    source=one(source,'void network_receive_network_players(struct Packet *p) {','void network_receive_network_players(struct Packet *p) {\n    if(!p||p->error||!smb_net_from_server(p)||p->cursor>=p->dataLength)return;')
    source=one(source,'    packet_read(p, &connectedCount, sizeof(u8));',r'''    packet_read(p, &connectedCount, sizeof(u8));
    const unsigned entrySize=1+1+2+4*sizeof(s16)+2+sizeof(s64)+1+sizeof(struct PlayerPalette)+MAX_CONFIG_STRING+64;
    if(p->error||connectedCount>MAX_PLAYERS||p->dataLength-p->cursor!=connectedCount*entrySize)return;''')
    source=one(source,'        if (globalIndex >= MAX_PLAYERS) { continue; }','        if(p->error)return;playerName[sizeof playerName-1]=0;discordId[sizeof discordId-1]=0;\n        if (globalIndex >= MAX_PLAYERS||npType<NPT_LOCAL||npType>NPT_CLIENT) { continue; }')
    result[path]=source
    return result

def fixed_mod_sources(original):
    result={}
    result['src/pc/network/packets/packet_mod_list.c']=r'''/* Fixed-content handshake: exchange an exact version and an empty mod list.
 * No host-provided paths, files, archives or scripts are accepted. */
#include <stdio.h>
#include "../network.h"
#include "../../sm64_coop_identity.h"
#include "pc/mods/mods.h"
#include "pc/network/version.h"
#include "pc/debuglog.h"
static bool smb_empty_mod_list=false;
void network_send_mod_list_request(void) {
    if(gNetworkType!=NT_CLIENT)return;
    smb_empty_mod_list=false;mods_clear(&gActiveMods);mods_clear(&gRemoteMods);
    struct Packet p={0};packet_init(&p,PACKET_MOD_LIST_REQUEST,true,PLMT_NONE);
    char version[MAX_VERSION_LENGTH]={0};snprintf(version,sizeof version,"%s",get_version());
    packet_write(&p,version,sizeof version);network_send_to(PACKET_DESTINATION_SERVER,&p);gAllowOrderedPacketClear=0;
}
void network_send_mod_list(void) {
    if(gNetworkType!=NT_SERVER)return;
    packet_ordered_begin();struct Packet p={0};packet_init(&p,PACKET_MOD_LIST,true,PLMT_NONE);
    char version[MAX_VERSION_LENGTH]={0};snprintf(version,sizeof version,"%s",get_version());u16 count=0;
    packet_write(&p,version,sizeof version);packet_write(&p,&count,sizeof count);network_send_to(0,&p);
    struct Packet done={0};packet_init(&done,PACKET_MOD_LIST_DONE,true,PLMT_NONE);network_send_to(0,&done);packet_ordered_end();
}
void network_receive_mod_list_request(struct Packet* p) {
    if(!p||gNetworkType!=NT_SERVER||p->dataLength-p->cursor!=MAX_VERSION_LENGTH)return;
    char version[MAX_VERSION_LENGTH]={0},expected[MAX_VERSION_LENGTH]={0};packet_read(p,version,sizeof version);
    snprintf(expected,sizeof expected,"%s",get_version());if(p->error||memcmp(version,expected,sizeof version))return;
    network_send_mod_list();
}
void network_receive_mod_list(struct Packet* p) {
    if(!p||gNetworkType!=NT_CLIENT||!smb_net_from_server(p)||p->dataLength-p->cursor!=MAX_VERSION_LENGTH+sizeof(u16))return;
    char version[MAX_VERSION_LENGTH]={0},expected[MAX_VERSION_LENGTH]={0};u16 count=1;
    packet_read(p,version,sizeof version);packet_read(p,&count,sizeof count);snprintf(expected,sizeof expected,"%s",get_version());
    if(p->error||count||memcmp(version,expected,sizeof version)) {smb_empty_mod_list=false;LOG_ERROR("Super Melee 64 rejected incompatible host content");return;}
    if(!gNetworkServerAddr)gNetworkServerAddr=network_duplicate_address(0);
    smb_empty_mod_list=true;
}
void network_receive_mod_list_done(struct Packet* p) {
    if(!p||gNetworkType!=NT_CLIENT||!smb_empty_mod_list||!smb_net_from_server(p)||p->error||p->cursor!=p->dataLength)return;
    smb_empty_mod_list=false;network_send_join_request();
}
void network_receive_mod_list_entry(struct Packet* p){(void)p;}
void network_receive_mod_list_file(struct Packet* p){(void)p;}
'''
    path='src/pc/network/packets/packet_download.c';source=original(path)
    for name,args in [('network_start_download_requests','void'),('network_send_download_request','u64 offset'),('network_receive_download_request','struct Packet* p'),('network_send_download','u64 requestOffset'),('network_receive_download','struct Packet* p')]:
        ignored='' if args=='void' else '(void)'+args.split()[-1]+';'
        source=function(source,name,'void '+name+'('+args+'){'+ignored+' /* Remote host content is disabled. */ }')
    result[path]=source
    path='src/pc/mods/mods.c';source=original(path)
    source=function(source,'mods_activate','void mods_activate(struct Mods* mods) {\n    (void)mods;mods_clear(&gActiveMods); /* Fixed-content edition: native bridge only. */\n}')
    result[path]=source
    return result
