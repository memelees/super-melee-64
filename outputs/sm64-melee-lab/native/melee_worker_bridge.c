/* Native Melee worker boundary; gameplay simulation remains original. */
#include "bridge_protocol.h"
#include <melee/ft/types.h>
#include <melee/ft/ftparts.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/state.h>
#include <sysdolphin/baselib/tev.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/controller.h>
#include <pc/world/m3d.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>

void m3d_lab_tick_original(void);
void m3d_lab_pad_original(void);
static int connection=-1;
static unsigned ticks, previous_buttons;
static SmbInput input;

static void transfer(void* p,size_t n,int writing) {
    unsigned char* b=p;
    while(n) {
        ssize_t size=writing?send(connection,b,n,0):recv(connection,b,n,0);
        if(size<0&&(errno==EINTR||(!writing&&(errno==EAGAIN||errno==EWOULDBLOCK))))continue;
        if(size<=0) { fprintf(stderr,"SMB WORKER CLOSED\n");exit(0); }
        b+=size;n-=size;
    }
}

/* The imported-world shadow texture currently has an opaque rectangle on this
 * backend. Use an explicit feathered contact shadow in this private adapter.
 * No gameplay state, character pose, or original Melee source is changed. */
int smb_contact_shadow_render(HSD_GObj* object,intptr_t pass) {
    (void)object;
    if(!getenv("SM64_MELEE_CONTACT_SHADOW")||!m3d_DelfinoActive())return 0;
    if(pass!=0)return 1;
    HSD_StateInvalidate(-1);HSD_StateInitTev();
    GXSetColorUpdate(GX_ENABLE);GXSetAlphaUpdate(GX_DISABLE);
    GXSetBlendMode(GX_BM_BLEND,GX_BL_SRCALPHA,GX_BL_INVSRCALPHA,GX_LO_NOOP);
    GXSetZMode(GX_ENABLE,GX_LEQUAL,GX_DISABLE);GXSetZCompLoc(GX_ENABLE);GXSetCullMode(GX_CULL_NONE);
    GXSetAlphaCompare(GX_ALWAYS,0,GX_AOP_AND,GX_ALWAYS,0);
    GXSetNumTexGens(0);GXSetNumTevStages(1);GXSetNumChans(1);
    GXSetChanCtrl(GX_COLOR0A0,GX_DISABLE,GX_SRC_REG,GX_SRC_VTX,0,GX_DF_NONE,GX_AF_NONE);
    GXSetTevOrder(GX_TEVSTAGE0,GX_TEXCOORD_NULL,GX_TEXMAP_NULL,GX_COLOR0A0);
    GXSetTevOp(GX_TEVSTAGE0,GX_PASSCLR);
    GXClearVtxDesc();GXSetVtxDesc(GX_VA_POS,GX_DIRECT);GXSetVtxDesc(GX_VA_CLR0,GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0,GX_VA_POS,GX_POS_XYZ,GX_F32,0);
    GXSetVtxAttrFmt(GX_VTXFMT0,GX_VA_CLR0,GX_CLR_RGBA,GX_RGBA8,0);
    GXLoadPosMtxImm(HSD_CObjGetViewingMtxPtrDirect(HSD_CObjGetCurrent()),0);GXSetCurrentMtx(0);
    extern int m3d_lab_count(void);
    for(int i=0;i<m3d_lab_count();i++) {
        HSD_GObj* fighter=Player_GetEntity(i);if(!fighter)continue;
        Fighter* fp=fighter->user_data;Vec3 pos=fp->cur_pos,ground;
        if(!m3d_DelfinoFloor(&pos,&ground))continue;
        float altitude=fmaxf(0,pos.y-ground.y);
        if(altitude>180)continue;
        float radius=8+fminf(altitude*.03f,4),alpha=100/(1+altitude*.015f);
        for(int j=0;j<24;j++) {
            Vec3 points[3]={ground,ground,ground};
            for(int k=1;k<3;k++) {
                float angle=(j+k-1)*(6.28318530718f/24);
                Vec3 query={pos.x+cosf(angle)*radius,pos.y+1,pos.z+sinf(angle)*radius};
                if(!m3d_DelfinoFloor(&query,&points[k]))points[k]=ground;
            }
            GXBegin(GX_TRIANGLES,GX_VTXFMT0,3);
            for(int k=0;k<3;k++) {
                GXPosition3f32(points[k].x,points[k].y+.1f,points[k].z);
                GXColor4u8(0,0,0,k?0:(uint8_t)alpha);
            }
            GXEnd();
        }
    }
    HSD_StateInvalidate(-1);return 1;
}
static void connect_host(void) {
    const char* path=getenv("SM64_MELEE_SOCKET");
    if(!path||strlen(path)>=sizeof(((struct sockaddr_un*)0)->sun_path))abort();
    int server=socket(AF_UNIX,SOCK_STREAM,0);if(server<0)abort();
    struct sockaddr_un address={0};address.sun_family=AF_UNIX;strcpy(address.sun_path,path);
    /* A fresh per-run private path is supplied by the launcher. */
    if(bind(server,(struct sockaddr*)&address,sizeof address)||listen(server,1))abort();
    fprintf(stderr,"SMB WORKER READY tick=%u\n",ticks);fflush(stderr);
    connection=accept(server,NULL,NULL);close(server);if(connection<0)abort();
    int yes=1;setsockopt(connection,SOL_SOCKET,SO_NOSIGPIPE,&yes,sizeof yes);
    struct timeval timeout={15,0};setsockopt(connection,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof timeout);
}
static void bones(HSD_JObj* joint,SmbOutput* out,uintptr_t root_id,int hidden) {
    for(;joint;joint=joint->next) {
        if(out->bones>=SMB_BONES)abort();
        unsigned index=out->bones;
        HSD_JObjSetupMatrix(joint);
        memcpy(out->matrices[out->bones++],joint->mtx,sizeof(float)*12);
        uintptr_t id=joint->id;
        out->joint_ids[index]=id>=root_id&&id-root_id<0x1000000?(uint32_t)(id-root_id):UINT32_MAX;
        int invisible=hidden||(joint->flags&JOBJ_HIDDEN);
        unsigned ordinal=0;
        for(HSD_DObj* d=HSD_JObjGetDObj(joint);d;d=d->next,ordinal++) {
            if(ordinal>=128)abort();
            if(!invisible&&!(d->flags&DOBJ_HIDDEN))out->visible[index][ordinal/32]|=1u<<(ordinal%32);
        }
        if(joint->child)bones(joint->child,out,root_id,invisible);
    }
}
void m3d_lab_tick(void) {
    m3d_lab_tick_original();
    if(!getenv("SM64_MELEE_SOCKET")||++ticks<180)return;
    HSD_GObj* object=Player_GetEntity(0);if(!object)return;
    Fighter* fp=object->user_data;
    if(connection<0)connect_host();
    else {
        SmbOutput out={0};out.magic=SMB_MAGIC;out.sequence=input.sequence;
        out.kind=fp->kind;out.motion=fp->motion_id;out.grounded=fp->ground_or_air==0;
        out.x=fp->cur_pos.x;out.y=fp->cur_pos.y;out.z=fp->cur_pos.z;
        out.vx=fp->self_vel.x;out.vy=fp->self_vel.y;out.vz=fp->self_vel.z;
        out.percent=fp->dmg.x1830_percent;out.animation=fp->cur_anim_frame;
        for(int i=0;i<4;i++)if(fp->x914[i].state)out.hitboxes|=1u<<i;
        HSD_JObj* root=object->hsd_obj;
        /* Part masks are normally applied immediately before native drawing,
         * after this simulation callback. Apply that same normal render group
         * before taking the snapshot, so unused weapons/alternate hands stay
         * hidden in the SM64 renderer as they do in Melee. */
        ftParts_800750C8(fp,1,0);ftParts_800750C8(fp,2,0);
        ftParts_800750C8(fp,4,0);ftParts_800750C8(fp,0,1);
        bones(root,&out,root->id,0);
        if(input.sequence==1) {
            fprintf(stderr,"SMB VIS root_id=%llu model_num=%d cleared=%d,%d,%d,%d,%d masks=%08x,%08x,%08x,%08x\n",
                (unsigned long long)root->id,fp->x5AC.model_num,fp->x5AC.cleared[0],fp->x5AC.cleared[1],fp->x5AC.cleared[2],fp->x5AC.cleared[3],fp->x5AC.cleared[4],
                out.visible[0][0],out.visible[0][1],out.visible[0][2],out.visible[0][3]);
            for(unsigned j=0;j<out.bones;j++)if(out.visible[j][0]||out.visible[j][1]||out.visible[j][2])fprintf(stderr,"SMB VIS BONE %u id=%u mask=%08x,%08x,%08x\n",j,out.joint_ids[j],out.visible[j][0],out.visible[j][1],out.visible[j][2]);
        }
        transfer(&out,sizeof out,1);
    }
    transfer(&input,sizeof input,0);
    if(input.magic!=SMB_MAGIC)abort();
    m3d_CameraSetYaw(input.camera_yaw);
    if(input.flags&1) {
        fp->cur_pos=(Vec3){input.x,input.y,input.z};
        fp->prev_pos=fp->coll_data.cur_pos=fp->coll_data.prev_pos=fp->coll_data.last_pos=fp->cur_pos;
        fp->self_vel=(Vec3){0};fp->gr_vel=0;
        HSD_JObjSetTranslate(object->hsd_obj,&fp->cur_pos);
    }
}
void m3d_lab_pad(void) {
    m3d_lab_pad_original();if(connection<0)return;
    HSD_PadStatus* pad=&HSD_PadGameStatus[0];memset(pad,0,sizeof *pad);
    pad->button=input.buttons;pad->last_button=previous_buttons;
    pad->trigger=pad->button&~previous_buttons;pad->release=previous_buttons&~pad->button;
    pad->repeat=pad->trigger;pad->nml_stickX=input.stick_x;pad->nml_stickY=input.stick_y;
    pad->stickX=(int8_t)(input.stick_x*80);pad->stickY=(int8_t)(input.stick_y*80);
    previous_buttons=pad->button;
}
