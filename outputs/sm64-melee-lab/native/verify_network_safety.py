#!/usr/bin/env python3
"""Sanitizer regression checks for the prepared host's production packet code.

This applies the production safety patch to pinned Git originals in memory and
extracts those packet routines verbatim into a temporary C harness. --prepared
instead checks existing generated files. Neither mode prepares or changes the
checkout, opens a socket, or builds/runs an engine. Game globals are fixtures;
logging, time and packet dispatch are stubbed.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import shlex
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]

PREFIX = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int16_t s16;
typedef int32_t s32;
typedef float f32;
#define TRUE 1
#define FALSE 0
#define SOFT_ASSERT(condition) ((void)(condition))
#define LOG_INFO(...) ((void)0)
#define LOG_ERROR(...) ((void)0)
static unsigned processed;
static const char* test_name;
static unsigned checks;
#define CHECK(condition) do { checks++; if (!(condition)) { \
    fprintf(stderr,"FAIL %s:%d: %s\n",test_name,__LINE__,#condition); exit(1); \
} } while (0)
'''

ENVIRONMENT = r'''
struct NetworkPlayer {u8 globalIndex;};
static struct NetworkPlayer local_player;
static struct NetworkPlayer* gNetworkPlayerLocal = &local_player;
static s16 gCurrCourseNum=1,gCurrActStarNum=1,gCurrLevelNum=9,gCurrAreaIndex=1;
static f32 clock_elapsed(void) {return 1;}
static void packet_process(struct Packet* packet) {(void)packet;processed++;}
'''

TESTS = r'''
static void put_u16(u8* to,u16 value) {memcpy(to,&value,sizeof value);}
static void header(struct Packet* p,u8 flags) {
    memset(p,0,sizeof *p);p->cursor=3;p->dataLength=5;
    p->buffer[0]=PACKET_CUSTOM;p->buffer[3]=flags;
    p->buffer[4]=PACKET_DESTINATION_BROADCAST;
    unsigned at=5;
    if(flags&4) {
        p->buffer[at++]=0;put_u16(&p->buffer[at],1);at+=2;
        put_u16(&p->buffer[at],1);at+=2;
    }
    if(flags&1) {
        p->buffer[at++]=1;p->buffer[at++]=1;
        put_u16(&p->buffer[at],9);at+=2;p->buffer[at++]=1;
    } else if(flags&8) {
        p->buffer[at++]=1;p->buffer[at++]=1;
        put_u16(&p->buffer[at],9);at+=2;
    }
    p->dataLength=at;
}
static void compress_bytes(const u8* source,unsigned length,u8* output,uLongf* size) {
    *size=compressBound(PACKET_LENGTH+1);
    CHECK(compress2(output,size,source,length,Z_BEST_SPEED)==Z_OK);
}
static void test_compress(void) {
    test_name="production compression allocation and incompressible round trip";
    struct Packet sent={0},received={0};header(&sent,0);
    u32 hash=packet_hash(&sent);memcpy(sent.buffer+sent.dataLength,&hash,sizeof hash);
    u8* compressed=NULL;u32 size=0;packet_compress(&sent,&compressed,&size);
    CHECK(compressed&&size&&sCompBufferLen<PACKET_LENGTH);
    CHECK(packet_decompress(&received,compressed,size)&&packet_check_hash(&received));
    /* Xorshift bytes are deterministic and incompressible at this size. The
     * real zlib output exceeds PACKET_LENGTH, exercising compressBound's
     * larger allocation and the helper's reallocation of the small buffer. */
    sent.dataLength=PACKET_LENGTH-sizeof(u32);u32 random=0x6d2b79f5u;
    for(unsigned i=5;i<sent.dataLength;i++) {
        random^=random<<13;random^=random>>17;random^=random<<5;
        sent.buffer[i]=(u8)(random>>24);
    }
    hash=packet_hash(&sent);memcpy(sent.buffer+sent.dataLength,&hash,sizeof hash);
    packet_compress(&sent,&compressed,&size);
    CHECK(compressed&&size>PACKET_LENGTH&&size<=compressBound(PACKET_LENGTH));
    CHECK(sCompBufferLen>=size);
    CHECK(packet_decompress(&received,compressed,size));
    CHECK(received.dataLength==PACKET_LENGTH-sizeof(u32));
    CHECK(!memcmp(received.buffer,sent.buffer,PACKET_LENGTH));
    CHECK(packet_check_hash(&received));
    const unsigned invalid_lengths[]={0,4,PACKET_LENGTH-3,PACKET_LENGTH,UINT16_MAX};
    for(unsigned i=0;i<sizeof invalid_lengths/sizeof *invalid_lengths;i++) {
        sent.dataLength=invalid_lengths[i];compressed=sent.buffer;size=123;
        packet_compress(&sent,&compressed,&size);CHECK(!compressed&&!size);
    }
    sent.dataLength=5;
    sent.error=true;compressed=sent.buffer;size=123;packet_compress(&sent,&compressed,&size);
    CHECK(!compressed&&!size);
    sent.error=false;sent.writeError=true;compressed=sent.buffer;size=123;packet_compress(&sent,&compressed,&size);
    CHECK(!compressed&&!size);
    compressed=sent.buffer;size=123;packet_compress(NULL,&compressed,&size);CHECK(!compressed&&!size);
}
static void test_decompress(void) {
    test_name="decompression boundaries";
    u8 raw[PACKET_LENGTH+1]={0};
    u8 compressed[PACKET_LENGTH+128];uLongf size;
    for(unsigned length=0;length<9;length++) {
        compress_bytes(raw,length,compressed,&size);
        struct Packet p={0};
        CHECK(!packet_decompress(&p,compressed,(u32)size));
    }
    for(unsigned length=9;length<=PACKET_LENGTH;length+=PACKET_LENGTH-9) {
        struct Packet sent={0},received={0};header(&sent,0);
        sent.dataLength=length-sizeof(u32);
        for(unsigned i=5;i<sent.dataLength;i++)sent.buffer[i]=(u8)(i*17);
        u32 hash=packet_hash(&sent);
        memcpy(&sent.buffer[sent.dataLength],&hash,sizeof hash);
        compress_bytes(sent.buffer,length,compressed,&size);
        CHECK(packet_decompress(&received,compressed,(u32)size));
        CHECK(received.dataLength==sent.dataLength);
        CHECK(!memcmp(received.buffer,sent.buffer,length));
        CHECK(packet_check_hash(&received));
    }
    compress_bytes(raw,sizeof raw,compressed,&size);
    struct Packet p={0};CHECK(!packet_decompress(&p,compressed,(u32)size));
    const u8 corrupt[]={0xff,0xff,0xff,0xff};
    CHECK(!packet_decompress(&p,(u8*)corrupt,sizeof corrupt));
}
static void test_read(void) {
    test_name="read bounds and zero-length convention";
    struct Packet p={0};u8 output[8],before[8];
    p.dataLength=5;p.cursor=3;p.buffer[3]=11;p.buffer[4]=22;
    packet_read(&p,output,2);CHECK(!p.error&&p.cursor==5&&output[0]==11&&output[1]==22);
    packet_read(&p,NULL,0);CHECK(!p.error&&p.cursor==5);
    memset(output,0xa5,sizeof output);memcpy(before,output,sizeof output);
    packet_read(&p,output,1);CHECK(p.error&&p.cursor==5&&!memcmp(output,before,sizeof output));
    packet_read(&p,NULL,0);CHECK(p.error&&p.cursor==5);
    p.cursor=0;packet_read(&p,output,1);
    CHECK(p.error&&p.cursor==0&&!memcmp(output,before,sizeof output));
    memset(&p,0,sizeof p);packet_read(&p,NULL,0);CHECK(!p.error&&!p.cursor);
    p.dataLength=1;packet_read(&p,NULL,1);CHECK(p.error&&!p.cursor);
    const unsigned cursors[]={PACKET_LENGTH-4,PACKET_LENGTH,UINT16_MAX};
    for(unsigned i=0;i<sizeof cursors/sizeof *cursors;i++) {
        memset(&p,0,sizeof p);p.dataLength=5;p.cursor=cursors[i];
        packet_read(&p,output,1);CHECK(p.error&&p.cursor==cursors[i]);
    }
    memset(&p,0,sizeof p);p.dataLength=PACKET_LENGTH-4;p.cursor=p.dataLength-1;
    p.buffer[p.cursor]=42;packet_read(&p,output,1);
    CHECK(!p.error&&p.cursor==PACKET_LENGTH-4&&output[0]==42);
    p.cursor=0;packet_read(&p,output,UINT16_MAX);CHECK(p.error&&!p.cursor);
}
static void test_write(void) {
    test_name="write bounds and checksum reservation";
    struct Packet p;u8 source[PACKET_LENGTH],before[PACKET_LENGTH];
    memset(source,0x36,sizeof source);memset(&p,0,sizeof p);
    memset(p.buffer,0xa5,sizeof p.buffer);
    packet_write(&p,source,PACKET_LENGTH-sizeof(u32));
    CHECK(!p.writeError&&!p.error&&p.cursor==PACKET_LENGTH-4&&p.dataLength==PACKET_LENGTH-4);
    CHECK(!memcmp(p.buffer,source,PACKET_LENGTH-4));
    for(unsigned i=PACKET_LENGTH-4;i<PACKET_LENGTH;i++)CHECK(p.buffer[i]==0xa5);
    packet_write(&p,NULL,0);CHECK(!p.writeError&&!p.error&&p.cursor==PACKET_LENGTH-4);
    memcpy(before,p.buffer,sizeof before);u16 cursor=p.cursor,length=p.dataLength;
    packet_write(&p,source,1);
    CHECK(p.writeError&&p.cursor==cursor&&p.dataLength==length&&!memcmp(before,p.buffer,sizeof before));
    packet_write(&p,NULL,0);CHECK(p.writeError&&p.cursor==cursor&&p.dataLength==length);
    p.cursor=0;packet_write(&p,source,1);
    CHECK(p.writeError&&!p.cursor&&p.dataLength==length&&!memcmp(before,p.buffer,sizeof before));
    const unsigned cursors[]={PACKET_LENGTH,UINT16_MAX};
    for(unsigned i=0;i<sizeof cursors/sizeof *cursors;i++) {
        memset(&p,0,sizeof p);p.cursor=cursors[i];
        packet_write(&p,source,1);CHECK(p.writeError&&p.cursor==cursors[i]&&!p.dataLength);
    }
    memset(&p,0,sizeof p);packet_write(&p,source,UINT16_MAX);
    CHECK(p.writeError&&!p.cursor&&!p.dataLength);
    memset(&p,0,sizeof p);packet_write(&p,NULL,0);CHECK(!p.error&&!p.writeError&&!p.cursor);
    packet_write(&p,NULL,1);CHECK((p.error||p.writeError)&&!p.cursor&&!p.dataLength);
}
static void test_hash(void) {
    test_name="hash boundaries and checksum verification";
    struct Packet p={0};header(&p,0);u32 hash=packet_hash(&p);
    memcpy(&p.buffer[p.dataLength],&hash,sizeof hash);CHECK(packet_check_hash(&p));
    p.buffer[0]^=1;CHECK(!packet_check_hash(&p));
    const unsigned lengths[]={PACKET_LENGTH-3,PACKET_LENGTH,UINT16_MAX};
    for(unsigned i=0;i<sizeof lengths/sizeof *lengths;i++) {
        memset(&p,0,sizeof p);p.dataLength=lengths[i];
        (void)packet_hash(&p);CHECK(!packet_check_hash(&p));
    }
}
static void test_initial_read(void) {
    test_name="base and optional header truncation";
    struct Packet complete;header(&complete,0);
    for(unsigned length=0;length<5;length++) {
        struct Packet p=complete;p.dataLength=length;
        CHECK(!packet_initial_read(&p));
    }
    struct Packet p=complete;CHECK(packet_initial_read(&p)&&!p.error&&p.cursor==5);
    p=complete;p.cursor=UINT16_MAX;
    p.orderedFromGlobalId=15;p.orderedGroupId=99;p.orderedSeqId=77;
    p.courseNum=9;p.actNum=9;p.levelNum=9;p.areaIndex=9;
    CHECK(packet_initial_read(&p)&&p.cursor==5);
    CHECK(!p.orderedFromGlobalId&&!p.orderedGroupId&&!p.orderedSeqId);
    CHECK(!p.courseNum&&!p.actNum&&!p.levelNum&&!p.areaIndex);
    const unsigned flags[]={4,1,8,5,12};
    for(unsigned i=0;i<sizeof flags/sizeof *flags;i++) {
        header(&complete,flags[i]);
        for(unsigned length=5;length<complete.dataLength;length++) {
            p=complete;p.dataLength=length;CHECK(!packet_initial_read(&p));
        }
        p=complete;CHECK(packet_initial_read(&p)&&!p.error&&p.cursor==complete.dataLength);
    }
    for(unsigned global=MAX_PLAYERS;global<=UINT8_MAX;global+=UINT8_MAX-MAX_PLAYERS) {
        header(&p,4);p.buffer[5]=global;CHECK(!packet_initial_read(&p));
    }
    const unsigned invalid_flags[]={0x10,0x80,9,13};
    for(unsigned i=0;i<sizeof invalid_flags/sizeof *invalid_flags;i++) {
        header(&p,invalid_flags[i]);CHECK(!packet_initial_read(&p));
    }
    header(&p,4);put_u16(&p.buffer[6],0);CHECK(!packet_initial_read(&p));
    header(&p,0);p.error=true;CHECK(!packet_initial_read(&p));
}
static void test_ordered(void) {
    test_name="ordered sender bounds and valid queued dispatch";
    packet_ordered_clear_all();processed=0;
    struct Packet p={0};p.orderedGroupId=1;p.orderedSeqId=1;
    const unsigned rejected[]={MAX_PLAYERS,UINT8_MAX};
    for(unsigned i=0;i<sizeof rejected/sizeof *rejected;i++) {
        p.orderedFromGlobalId=rejected[i];packet_ordered_add(&p);CHECK(!processed);
        packet_ordered_clear_table(rejected[i],1);packet_ordered_clear(rejected[i]);
        for(unsigned n=0;n<MAX_PLAYERS;n++)CHECK(!orderedPacketTable[n]);
    }
    const unsigned accepted[]={0,MAX_PLAYERS-1};
    for(unsigned i=0;i<sizeof accepted/sizeof *accepted;i++) {
        p.orderedFromGlobalId=accepted[i];p.orderedSeqId=2;unsigned start=processed;
        packet_ordered_add(&p);CHECK(processed==start);
        p.orderedSeqId=1;packet_ordered_add(&p);CHECK(processed==start+2);
        packet_ordered_add(&p);CHECK(processed==start+2);
    }
    packet_ordered_clear_all();
    for(unsigned n=0;n<MAX_PLAYERS;n++)CHECK(!orderedPacketTable[n]);
}
int main(void) {
    test_compress();test_decompress();test_read();test_write();test_hash();test_initial_read();test_ordered();
    free(sCompBuffer);sCompBuffer=NULL;
    printf("Network safety PASS: %u checks across seven groups (ASan + UBSan).\n",checks);
    return 0;
}
'''

ADMISSION_ENVIRONMENT = r'''
/* Only transport/game state is synthetic; packet and admission code below is
 * extracted verbatim from the production sources. No sockets are opened. */
/* Explicit BSD-style length and padding fields make the endpoint matcher
 * regression independent of the host platform's sockaddr_in6 layout. */
struct in6_addr {u8 s6_addr[16];};
struct sockaddr_in6 {
    u8 sin6_len,sin6_family;u16 sin6_port;u32 sin6_flowinfo;
    struct in6_addr sin6_addr;u32 sin6_scope_id;u8 padding[8];
};
struct NetworkPlayer {
    bool connected;u8 localIndex,globalIndex,onRxSeqId;
    u16 rxSeqIds[MAX_RX_SEQ_IDS];u32 rxPacketHash[MAX_RX_SEQ_IDS];
};
static struct NetworkPlayer gNetworkPlayers[MAX_PLAYERS];
static struct NetworkPlayer* gNetworkPlayerLocal=&gNetworkPlayers[0];
static struct NetworkPlayer* gNetworkPlayerServer;
static enum NetworkType gNetworkType;
static u64 host_address=123,other_address=456;
static void* gNetworkServerAddr;
static f32 gNetworkStartupTimer;
static bool same_address(void* a,void* b) {return a&&b&&*(u64*)a==*(u64*)b;}
static const char* peer_id(u8 local) {(void)local;return "fixture-peer";}
struct FixtureNetworkSystem {
    bool requireServerBroadcast;
    bool (*match_addr)(void*,void*);
    const char* (*get_id_str)(u8);
    int (*initialize)(enum NetworkType,bool);
};
static struct FixtureNetworkSystem transport={true,same_address,peer_id,NULL};
static struct FixtureNetworkSystem* gNetworkSystem=&transport;
static struct NetworkPlayer* network_player_from_global_index(u8 global) {
    for(unsigned i=0;i<MAX_PLAYERS;i++)
        if(gNetworkPlayers[i].connected&&gNetworkPlayers[i].globalIndex==global)return &gNetworkPlayers[i];
    return NULL;
}
static unsigned acknowledgements,kicks,joins,clears;
static struct Packet sent_packets[32];static u8 sent_to[32];static unsigned sent_count;
static void network_send_ack(struct Packet* p) {(void)p;acknowledgements++;}
static void network_send_kick(u8 local,enum KickReasonType why) {(void)local;(void)why;kicks++;}
static bool ban_list_contains(const char* id) {(void)id;return false;}
static bool network_allow_unknown_local_index(u8 type) {
    return type==PACKET_MOD_LIST_REQUEST||type==PACKET_JOIN_REQUEST;
}
static void network_send_to(u8 local,struct Packet* p) {
    CHECK(sent_count<32);packet_set_ordered_data(p);
    sent_to[sent_count]=local;sent_packets[sent_count++]=*p;
}
struct Mods {unsigned count;};
static struct Mods gActiveMods,gRemoteMods;
static void mods_clear(struct Mods* mods) {mods->count=0;clears++;}
static const char* get_version(void) {return "Super Melee 64 fixture beta1 ABI14";}
static void* network_duplicate_address(u8 local) {(void)local;return &host_address;}
static void network_send_join_request(void) {joins++;}
void network_receive_mod_list_request(struct Packet* p);
void network_receive_mod_list(struct Packet* p);
void network_receive_mod_list_done(struct Packet* p);
static s16 gCurrCourseNum=1,gCurrActStarNum=1,gCurrLevelNum=9,gCurrAreaIndex=1;
static f32 clock_elapsed(void) {return 1;}
static void packet_process(struct Packet* p) {
    processed++;
    switch(p->packetType) {
        case PACKET_MOD_LIST_REQUEST:network_receive_mod_list_request(p);break;
        case PACKET_MOD_LIST:network_receive_mod_list(p);break;
        case PACKET_MOD_LIST_DONE:network_receive_mod_list_done(p);break;
        default:break;
    }
}
/* network_init's unrelated engine, config and UI dependencies are inert. */
#define ACT_SELECT_HUD_ALL 1
#define DLANG(section,key) "fixture notification"
#define NPT_LOCAL 1
u8 gOverrideHideHud;
static void act_select_hud_show(int flags) {(void)flags;}
static void network_forget_all_reliable(void) {}
static void crash_handler_init(void) {}
static struct {
    int playerInteractions,bouncyLevelBounds,playerKnockbackStrength,stayInLevelAfterStar,
        skipIntro,bubbleDeath,enablePlayersInLevelDisplay,enablePlayerList,nametags,
        maxPlayers,pauseAnywhere,pvpType,headlessServer;
} gServerSettings;
static int configPlayerInteraction,configBouncyLevelBounds,configPlayerKnockbackStrength,
    configStayInLevelAfterStar,configSkipIntro,configBubbleDeath,configNametags,
    configAmountOfPlayers,configPauseAnywhere,configPvpType,configHostSaveSlot,configPlayerModel;
static struct {bool skipIntro,headless;} gCLIOpts;
static struct {bool showHealth,showSelfTag;} gNametagsSettings;
static bool gPauseMenuHidden,gNetworkSentJoin;
static void djui_popup_create(const char* text,int duration) {(void)text;(void)duration;}
static s16 gCurrSaveFileNum,gChangeLevelTransition;
static u8* gOverrideEeprom;
static struct Mods gLocalMods;
static void smlua_init(void) {}
static void dynos_behavior_hook_all_custom_behaviors(void) {}
static u8 configPlayerPalette;
static const char* configPlayerName="fixture";
static const char* get_local_discord_id(void) {return "";}
static void network_player_connected(unsigned type,u8 global,u8 model,void* palette,const char* name,const char* id) {
    (void)type;(void)global;(void)model;(void)palette;(void)name;(void)id;
}
static struct {s16 entryLevel;} gLevelValues;
static void djui_chat_box_create(void) {}
static const char* configfile_name(void) {return "fixture";}
static void configfile_save(const char* name) {(void)name;}
static struct {int base;} fixture_reload;
static struct {int base;}* gDjuiModReload=(void*)&fixture_reload;
static bool network_allow_mod_dev_mode(void) {return false;}
static void djui_base_set_visible(void* base,bool visible) {(void)base;(void)visible;}
static void* old_server_pin;
static void* initialized_server_pin;
static unsigned old_pin_frees,new_pin_frees,transport_initializations;
static void lifecycle_free(void* address) {
    if(!transport_initializations) {CHECK(address==old_server_pin);old_pin_frees++;}
    else if(address==initialized_server_pin)new_pin_frees++;
    free(address);
}
static int initialize_pinned_transport(enum NetworkType type,bool reconnecting) {
    CHECK(type==NT_CLIENT&&!reconnecting);
    CHECK(old_pin_frees==1&&!gNetworkServerAddr);
    transport_initializations++;
    u64* address=malloc(sizeof *address);CHECK(address!=NULL);
    *address=UINT64_C(0x1029384756abcdef);
    initialized_server_pin=gNetworkServerAddr=address;return 1;
}
'''

ADMISSION_TESTS = r'''
static void context(bool client,bool relay) {
    packet_ordered_end();packet_ordered_clear_all();memset(gNetworkPlayers,0,sizeof gNetworkPlayers);
    gNetworkType=client?NT_CLIENT:NT_SERVER;transport.requireServerBroadcast=relay;transport.match_addr=same_address;
    gNetworkPlayerLocal=&gNetworkPlayers[0];
    gNetworkPlayers[0]=(struct NetworkPlayer){.connected=true,.localIndex=0,.globalIndex=client?9:0};
    gNetworkPlayers[1]=(struct NetworkPlayer){.connected=true,.localIndex=1,.globalIndex=client?0:1};
    gNetworkPlayers[2]=(struct NetworkPlayer){.connected=true,.localIndex=2,.globalIndex=2};
    gNetworkPlayerServer=client?&gNetworkPlayers[1]:NULL;gNetworkServerAddr=client?&host_address:NULL;
    sent_count=processed=acknowledgements=kicks=0;smb_empty_mod_list=false;
}
static struct Packet claim(enum PacketType type,u8 global,u8 local,u8 destination,bool broadcast) {
    struct Packet p={0};packet_init(&p,type,false,PLMT_NONE);
    u8 payload[9]={0};payload[type==PACKET_CUSTOM?8:0]=global;
    packet_write(&p,payload,type==PACKET_CUSTOM?9:1);
    packet_set_destination(&p,destination);p.requestBroadcast=broadcast;packet_set_flags(&p);
    p.localIndex=local;p.addr=&host_address;return p;
}
static struct Packet ordered(u8 global,void* address) {
    struct Packet p={0};packet_ordered_begin();packet_init(&p,PACKET_MOD_LIST_DONE,false,PLMT_NONE);
    packet_set_ordered_data(&p);packet_ordered_end();
    p.orderedFromGlobalId=global;p.buffer[5]=global;p.localIndex=UNKNOWN_LOCAL_INDEX;p.addr=address;
    return p;
}
static void test_raw_host(void) {
    test_name="raw host authenticates before forwarding or ACK";
    const enum PacketType types[]={PACKET_CUSTOM,PACKET_PLAYER};
    for(unsigned i=0;i<sizeof types/sizeof *types;i++) {
        context(false,true);struct Packet p=claim(types[i],2,1,2,false);packet_receive(&p);
        CHECK(!processed&&!sent_count&&!acknowledgements);
        p=claim(types[i],1,1,2,false);packet_receive(&p);
        CHECK(!processed&&sent_count==1&&sent_to[0]==2);
        context(false,true);p=claim(types[i],2,1,PACKET_DESTINATION_BROADCAST,true);packet_receive(&p);
        CHECK(!processed&&!sent_count&&!acknowledgements);
        p=claim(types[i],1,1,PACKET_DESTINATION_BROADCAST,true);packet_receive(&p);
        CHECK(processed==1&&sent_count==1&&sent_to[0]==2);
    }
}
static void test_relay_client(void) {
    test_name="raw client accepts connected claims only through expected host";
    const enum PacketType types[]={PACKET_CUSTOM,PACKET_PLAYER};
    for(unsigned i=0;i<sizeof types/sizeof *types;i++) {
        context(true,true);struct Packet p=claim(types[i],2,1,PACKET_DESTINATION_BROADCAST,false);
        packet_receive(&p);CHECK(processed==1);
        p=claim(types[i],2,2,PACKET_DESTINATION_BROADCAST,false);packet_receive(&p);CHECK(processed==1);
        p=claim(types[i],2,UNKNOWN_LOCAL_INDEX,PACKET_DESTINATION_BROADCAST,false);packet_receive(&p);CHECK(processed==2);
        p=claim(types[i],2,UNKNOWN_LOCAL_INDEX,PACKET_DESTINATION_BROADCAST,false);p.addr=&other_address;
        packet_receive(&p);CHECK(processed==2);
        p=claim(types[i],2,1,PACKET_DESTINATION_BROADCAST,false);p.addr=&other_address;
        packet_receive(&p);CHECK(processed==2);
        gNetworkPlayers[2].connected=false;p=claim(types[i],2,1,PACKET_DESTINATION_BROADCAST,false);
        packet_receive(&p);CHECK(processed==2);
        p=claim(types[i],MAX_PLAYERS,1,PACKET_DESTINATION_BROADCAST,false);packet_receive(&p);CHECK(processed==2);
        p=claim(types[i],9,1,PACKET_DESTINATION_BROADCAST,false);packet_receive(&p);CHECK(processed==2);
    }
}
static void test_direct_peer(void) {
    test_name="CoopNet direct claims match transport peer";
    const enum PacketType types[]={PACKET_CUSTOM,PACKET_PLAYER};
    for(unsigned i=0;i<sizeof types/sizeof *types;i++) {
        context(true,false);struct Packet p=claim(types[i],2,2,PACKET_DESTINATION_BROADCAST,false);
        packet_receive(&p);CHECK(processed==1);
        p=claim(types[i],1,2,PACKET_DESTINATION_BROADCAST,false);packet_receive(&p);CHECK(processed==1);
        p=claim(types[i],2,UNKNOWN_LOCAL_INDEX,PACKET_DESTINATION_BROADCAST,false);packet_receive(&p);CHECK(processed==1);
        gNetworkPlayers[2].connected=false;p=claim(types[i],2,2,PACKET_DESTINATION_BROADCAST,false);
        packet_receive(&p);CHECK(processed==1);
    }
}
static void test_unknown_ordered(void) {
    test_name="pre-admission ordered stream belongs to expected host zero";
    context(true,true);gNetworkPlayerServer=NULL;gNetworkPlayerLocal->globalIndex=0;
    gNetworkPlayers[1].connected=gNetworkPlayers[2].connected=false;
    struct Packet p=ordered(0,&host_address);packet_receive(&p);CHECK(processed==1&&!joins);
    p=ordered(2,&host_address);packet_receive(&p);CHECK(processed==1&&!joins);
    p=ordered(0,&other_address);packet_receive(&p);CHECK(processed==1&&!joins);
    gNetworkServerAddr=NULL;p=ordered(0,&host_address);packet_receive(&p);CHECK(processed==1&&!joins);
}
static void deliver(struct Packet p) {
    p.localIndex=UNKNOWN_LOCAL_INDEX;p.addr=&host_address;packet_receive(&p);
}
static void test_empty_handshake(void) {
    test_name="fixed empty exact-version handshake joins once";
    context(true,true);joins=0;gActiveMods.count=3;gRemoteMods.count=4;
    network_send_mod_list_request();CHECK(sent_count==1&&sent_to[0]==PACKET_DESTINATION_SERVER);
    CHECK(!gActiveMods.count&&!gRemoteMods.count&&!gAllowOrderedPacketClear);
    struct Packet request=sent_packets[0];
    context(false,true);request.localIndex=UNKNOWN_LOCAL_INDEX;request.addr=&other_address;
    packet_receive(&request);CHECK(sent_count==2);
    struct Packet list=sent_packets[0],done=sent_packets[1];
    CHECK(list.packetType==PACKET_MOD_LIST&&done.packetType==PACKET_MOD_LIST_DONE);
    CHECK(list.orderedFromGlobalId==0&&list.orderedGroupId&&list.orderedSeqId==1&&done.orderedSeqId==2);
    CHECK(list.dataLength==10+MAX_VERSION_LENGTH+sizeof(u16)&&done.dataLength==10);
    CHECK(!list.buffer[list.dataLength-1]&&!list.buffer[list.dataLength-2]);
    context(true,true);gNetworkPlayerServer=NULL;gNetworkPlayerLocal->globalIndex=0;
    deliver(done);CHECK(!joins);deliver(list);CHECK(joins==1&&!smb_empty_mod_list);
    deliver(done);CHECK(joins==1);
    /* Replay the actual host list with one changed field into a fresh client. */
    for(unsigned variant=0;variant<4;variant++) {
        context(true,true);gNetworkPlayerServer=NULL;gNetworkPlayerLocal->globalIndex=0;joins=0;
        struct Packet bad=list;
        if(variant==0)bad.buffer[bad.dataLength-sizeof(u16)]=1;
        if(variant==1)bad.buffer[10]^=1;
        if(variant==2)bad.dataLength--;
        if(variant==3)bad.dataLength++;
        deliver(bad);deliver(done);CHECK(!joins&&!smb_empty_mod_list);
    }
    context(false,true);request.error=false;request.cursor=3;request.buffer[5]^=1;packet_receive(&request);
    CHECK(!sent_count);
}
static void test_initialization_pin_lifecycle(void) {
    test_name="network init preserves the transport's new server pin";
    context(true,true);joins=0;gNetworkPlayerServer=NULL;gNetworkPlayerLocal->globalIndex=0;
    gNetworkPlayers[1].connected=gNetworkPlayers[2].connected=false;
    old_server_pin=malloc(sizeof(u64));CHECK(old_server_pin!=NULL);
    *(u64*)old_server_pin=other_address;gNetworkServerAddr=old_server_pin;
    old_pin_frees=new_pin_frees=transport_initializations=0;initialized_server_pin=NULL;
    transport.initialize=initialize_pinned_transport;
    CHECK(network_init(NT_CLIENT,false));
    CHECK(old_pin_frees==1&&transport_initializations==1&&!new_pin_frees);
    CHECK(gNetworkServerAddr==initialized_server_pin&&gNetworkType==NT_CLIENT);
    /* Run the real ordered fixed-content handshake through the new address. */
    gNetworkType=NT_SERVER;network_send_mod_list();CHECK(sent_count==2);
    struct Packet list=sent_packets[0],done=sent_packets[1];gNetworkType=NT_CLIENT;
    u64 received_from=UINT64_C(0x1029384756abcdef);
    list.localIndex=done.localIndex=UNKNOWN_LOCAL_INDEX;list.addr=done.addr=&received_from;
    packet_receive(&list);CHECK(smb_empty_mod_list&&!joins);
    packet_receive(&done);CHECK(joins==1&&!smb_empty_mod_list);
    CHECK(gNetworkServerAddr==initialized_server_pin&&!new_pin_frees);
    free(initialized_server_pin);gNetworkServerAddr=NULL;
    old_server_pin=initialized_server_pin=NULL;transport.initialize=NULL;
}
static void test_socket_endpoint_match(void) {
    test_name="socket endpoint comparison ignores BSD length padding and flow info";
    struct sockaddr_in6 pinned={0};pinned.sin6_family=30;pinned.sin6_port=0x1337;pinned.sin6_scope_id=4;
    for(unsigned i=0;i<sizeof pinned.sin6_addr.s6_addr;i++)pinned.sin6_addr.s6_addr[i]=(u8)(17*i+3);
    struct sockaddr_in6 received=pinned;received.sin6_len=28;received.sin6_flowinfo=0x12345678;
    memset(received.padding,0xa5,sizeof received.padding);
    CHECK(memcmp(&pinned,&received,sizeof pinned)!=0);
    CHECK(ns_socket_match_addr(&pinned,&received)&&ns_socket_match_addr(&received,&pinned));
    for(unsigned field=0;field<4;field++) {
        struct sockaddr_in6 different=received;
        if(field==0)different.sin6_family^=1;
        if(field==1)different.sin6_port^=1;
        if(field==2)different.sin6_addr.s6_addr[15]^=1;
        if(field==3)different.sin6_scope_id^=1;
        CHECK(!ns_socket_match_addr(&pinned,&different));
        CHECK(!ns_socket_match_addr(&different,&pinned));
    }
    CHECK(!ns_socket_match_addr(NULL,&received));CHECK(!ns_socket_match_addr(&pinned,NULL));
    CHECK(!ns_socket_match_addr(NULL,NULL));
    context(true,true);gNetworkPlayerServer=NULL;gNetworkPlayerLocal->globalIndex=0;
    gNetworkPlayers[1].connected=gNetworkPlayers[2].connected=false;
    transport.match_addr=ns_socket_match_addr;gNetworkServerAddr=&pinned;
    struct Packet p=ordered(0,&received);packet_receive(&p);CHECK(processed==1);
    received.sin6_scope_id^=1;p=ordered(0,&received);packet_receive(&p);CHECK(processed==1);
    transport.match_addr=same_address;gNetworkServerAddr=NULL;
}
static void test_disabled_content(void) {
    test_name="remote metadata downloads and Lua cannot dispatch or activate";
    context(true,true);joins=0;gActiveMods.count=7;gRemoteMods.count=11;
    const enum PacketType blocked[]={PACKET_MOD_LIST_ENTRY,PACKET_MOD_LIST_FILE,PACKET_DOWNLOAD_REQUEST,
        PACKET_DOWNLOAD,PACKET_LUA_SYNC_TABLE_REQUEST,PACKET_LUA_SYNC_TABLE,PACKET_LUA_CUSTOM,PACKET_LUA_CUSTOM_BYTESTRING};
    for(unsigned i=0;i<sizeof blocked/sizeof *blocked;i++) {
        struct Packet p={0};packet_init(&p,blocked[i],false,PLMT_NONE);p.localIndex=1;p.addr=&host_address;
        packet_receive(&p);CHECK(!processed&&!sent_count&&!acknowledgements&&!joins);
        CHECK(gActiveMods.count==7&&gRemoteMods.count==11);
    }
    struct Packet bad={0};bad.cursor=bad.dataLength=UINT16_MAX;
    network_receive_mod_list_entry(&bad);network_receive_mod_list_file(&bad);
    network_start_download_requests();network_send_download_request(UINT64_MAX);
    network_receive_download_request(&bad);network_send_download(UINT64_MAX);network_receive_download(&bad);
    CHECK(!joins&&!sent_count&&gActiveMods.count==7&&gRemoteMods.count==11);
    mods_activate(&gRemoteMods);CHECK(!gActiveMods.count&&gRemoteMods.count==11&&!joins&&!sent_count);
    network_receive_mod_list_done(&bad);CHECK(!joins);
}
int main(void) {
    test_raw_host();test_relay_client();test_direct_peer();test_unknown_ordered();
    test_empty_handshake();test_initialization_pin_lifecycle();test_socket_endpoint_match();
    test_disabled_content();packet_ordered_clear_all();free(sCompBuffer);
    printf("Network admission PASS: %u checks across eight groups (ASan + UBSan).\n",checks);
    return 0;
}
'''


def block(text, pattern):
    """Extract one complete C brace block, ignoring braces in comments/strings."""
    matches = list(re.finditer(pattern, text, re.MULTILINE))
    if len(matches) != 1:
        raise ValueError(f"Expected one source boundary for {pattern!r}; found {len(matches)}")
    start = matches[0].start()
    opening = text.index("{", matches[0].start(), matches[0].end())
    depth, state, i = 0, "code", opening
    while i < len(text):
        pair, char = text[i:i + 2], text[i]
        if state == "line":
            if char == "\n":
                state = "code"
        elif state == "comment":
            if pair == "*/":
                state = "code"
                i += 1
        elif state in ("'", '"'):
            if char == "\\":
                i += 1
            elif char == state:
                state = "code"
        elif pair == "//":
            state = "line"
            i += 1
        elif pair == "/*":
            state = "comment"
            i += 1
        elif char in ("'", '"'):
            state = char
        elif char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if not depth:
                end = i + 1
                if text[end:end + 1] == ";":
                    end += 1
                return text[start:end], text[:start].count("\n") + 1
        i += 1
    raise ValueError(f"Unclosed C source block for {pattern!r}")


def strip_includes(text):
    # Preserve line numbers for #line diagnostics and leave all code unchanged.
    return re.sub(r"^[ \t]*#include[^\n]*", "", text, flags=re.MULTILINE)


def build_harness(source, overrides=None, environment=ENVIRONMENT, tests=TESTS, extra=()):
    packets = source / "src/pc/network/packets"
    paths = {name: packets / name for name in ("packet.h", "packet.c", "packet_read_write.c", "packet_ordered.c")}
    paths["network.h"] = source / "src/pc/network/network.h"
    paths["types.h"] = source / "include/types.h"
    overrides = overrides or {}
    texts = {name: overrides.get(path.relative_to(source).as_posix(), path.read_text())
             for name, path in paths.items()}
    header = texts["packet.h"]
    declarations = []
    declarations.extend(re.findall(r"^#define PACKET_[A-Z_]+[ \t]+[^\n]+", header, re.MULTILINE))
    declarations.append(re.search(r"^#define MAX_PLAYERS\s+[^\n]+", texts["types.h"], re.MULTILINE)[0])
    for name in ("SET_BIT", "GET_BIT"):
        declarations.append(re.search(rf"^#define {name}\([^\n]+", texts["network.h"], re.MULTILINE)[0])
    for pattern in (r"^enum PacketType\s*\{", r"^enum PacketLevelMatchType\s*\{", r"^struct Packet\s*\{"):
        declarations.append(block(header, pattern)[0])
    # All packet_read_write.c public declarations come from the production header.
    rw = texts["packet_read_write.c"]
    public_names = re.findall(r"^(?:void|u8|u32|bool)\s+(packet_\w+)\s*\(", rw, re.MULTILINE)
    for name in public_names:
        match = re.search(rf"^(?:void|u8|u32|bool)\s+{name}\s*\([^;]*\);", header, re.MULTILINE)
        if not match:
            raise ValueError(f"Missing production declaration: {name}")
        declarations.append(match[0])
    # Keep actual compression, decompression and their allocation helper/static
    # state, while excluding packet_process's game dispatcher.
    packet_source = texts["packet.c"]
    preamble = packet_source[:packet_source.index("void packet_compress(")]
    compressor, compressor_line = block(packet_source, r"^void packet_compress\s*\([^;]*?\)\s*\{")
    decompressor, line = block(packet_source, r"^bool packet_decompress\s*\([^;]*?\)\s*\{")
    parts = [PREFIX, "\n".join(declarations), environment,
             f'#line 1 "{paths["packet.c"]}"\n' + strip_includes(preamble),
             f'#line {compressor_line} "{paths["packet.c"]}"\n' + compressor,
             f'#line {line} "{paths["packet.c"]}"\n' + decompressor,
             f'#line 1 "{paths["packet_read_write.c"]}"\n' + strip_includes(rw),
             f'#line 1 "{paths["packet_ordered.c"]}"\n' + strip_includes(texts["packet_ordered.c"]),
             *extra, '#line 1 "network_safety_checks.c"\n' + tests]
    hashes = {str(path.relative_to(source)): hashlib.sha256(texts[name].encode()).hexdigest()
              for name, path in paths.items()}
    return "\n".join(parts), hashes


def production_patch(source):
    """Load the pure production generator without creating a bytecode cache."""
    path = HERE / "coop_network_safety.py"
    namespace = {"__name__": "network_safety_production_patch", "__file__": str(path)}
    text = path.read_text()
    exec(compile(text, str(path), "exec"), namespace)

    def original(name):
        return subprocess.check_output(["git", "-C", str(source), "show", "HEAD:" + name], text=True)

    patched = namespace["patched_sources"](original)
    if not isinstance(patched, dict) or not all(isinstance(k, str) and isinstance(v, str) for k, v in patched.items()):
        raise ValueError("Production patched_sources must return a path-to-source dictionary")
    return patched, hashlib.sha256(text.encode()).hexdigest()


def build_admission_harness(source, overrides):
    paths = ("src/pc/cliopts.h", "src/pc/network/network_player.h", "src/pc/network/version.h",
             "src/pc/sm64_coop_identity.h", "src/pc/network/packets/packet.h",
             "src/pc/network/packets/packet.c", "src/pc/network/packets/packet_mod_list.c",
             "src/pc/network/packets/packet_download.c", "src/pc/mods/mods.c", "src/pc/network/network.c",
             "src/pc/network/socket/socket.c")
    texts = {path: overrides[path] if path in overrides else (source / path).read_text() for path in paths}
    constants = [block(texts[paths[0]], r"^enum NetworkType\s*\{")[0],
                 block(texts[paths[4]], r"^enum KickReasonType\s*\{")[0]]
    for name, path in (("UNKNOWN_LOCAL_INDEX", paths[1]), ("MAX_RX_SEQ_IDS", paths[1]),
                       ("MAX_VERSION_LENGTH", paths[2])):
        constants.append(re.search(rf"^#define {name}\s+[^\n]+", texts[path], re.MULTILINE)[0])
    extra = [f'#line 1 "{source / paths[3]}"\n' + texts[paths[3]],
             f'#line 1 "{source / paths[6]}"\n' + strip_includes(texts[paths[6]])]
    for name, path in (("network_start_download_requests", paths[7]),
                       ("network_send_download_request", paths[7]),
                       ("network_receive_download_request", paths[7]),
                       ("network_send_download", paths[7]),
                       ("network_receive_download", paths[7]), ("mods_activate", paths[8]),
                       ("packet_receive", paths[5])):
        function, line = block(texts[path], rf"^void {name}\s*\([^;]*?\)\s*\{{")
        extra.append(f'#line {line} "{source / path}"\n' + function)
    initializer, line = block(texts[paths[9]], r"^bool network_init\s*\([^;]*?\)\s*\{")
    # Observe only network_init's frees. The complete production function is
    # otherwise unchanged; the wrapper delegates to the real allocator.
    extra.append('#define free lifecycle_free\n' + f'#line {line} "{source / paths[9]}"\n' +
                 initializer + '\n#undef free\n')
    matcher, line = block(texts[paths[10]], r"^static bool ns_socket_match_addr\s*\([^;]*?\)\s*\{")
    extra.append(f'#line {line} "{source / paths[10]}"\n' + matcher)
    harness, hashes = build_harness(source, overrides, "\n".join(constants) + ADMISSION_ENVIRONMENT,
                                   ADMISSION_TESTS, extra)
    hashes.update({path: hashlib.sha256(text.encode()).hexdigest() for path, text in texts.items()})
    return harness, hashes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=ROOT / "work/sm64coopdx-melee",
                        help="Pinned checkout to read; preparation is never run here")
    parser.add_argument("--prepared", action="store_true",
                        help="Check existing generated files instead of applying the production patch in memory")
    parser.add_argument("--compiler", default=os.environ.get("CC", "clang"),
                        help="C compiler with ASan/UBSan support (default: CC or clang)")
    args = parser.parse_args()
    source = args.source.resolve()
    overrides, patch_hash = ({}, None) if args.prepared else production_patch(source)
    harness, hashes = build_harness(source, overrides)
    admission_harness, admission_hashes = build_admission_harness(source, overrides)
    flags = ["-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
             "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer"]
    with tempfile.TemporaryDirectory(prefix="sm64-network-safety-") as folder:
        folder = Path(folder)
        environment = os.environ.copy()
        environment["ASAN_OPTIONS"] = "halt_on_error=1:abort_on_error=1:detect_leaks=0"
        environment["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
        results = []
        for name, text in (("network_safety", harness), ("network_admission", admission_harness)):
            test, binary = folder / (name + ".c"), folder / name
            test.write_text(text)
            subprocess.run([*shlex.split(args.compiler), *flags, str(test), "-lz", "-o", str(binary)], check=True)
            result = subprocess.run([str(binary)], capture_output=True, text=True, env=environment)
            if result.returncode:
                raise SystemExit(result.stdout + result.stderr + f"\n{name} failed with status {result.returncode}")
            results.append(result.stdout.strip())
    print(json.dumps({"passed": True, "output": results, "source": str(source),
                      "mode": "prepared files" if args.prepared else "production patch applied in memory to Git HEAD",
                      "patch_generator_sha256": patch_hash,
                      "source_sha256": hashes, "harness_sha256": hashlib.sha256(harness.encode()).hexdigest(),
                      "admission_source_sha256": admission_hashes,
                      "admission_harness_sha256": hashlib.sha256(admission_harness.encode()).hexdigest(),
                      "sanitizers": ["address", "undefined"], "groups": ["compression", "decompression", "read", "write", "hash", "initial_header", "ordered"],
                      "admission_groups": ["raw_host", "raw_client", "direct_peer", "unknown_ordered", "empty_handshake", "initialization_pin_lifecycle", "socket_endpoint_match", "disabled_content"],
                      "scope": "Verbatim production packet and admission routines; game and transport state are fixtures and logging, clock and game dispatch are stubbed. No network calls, checkout writes, preparation, engine builds or game launches."}, indent=2))


if __name__ == "__main__":
    main()
