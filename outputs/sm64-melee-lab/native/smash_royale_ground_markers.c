#include <stdlib.h>
/* Native source port of the shared melee3d overlay; first stage gate: FD. */
/// Optional presentation banks. Collision and character simulation stay separate.
#include "m3d.h"
#include "pc/m3d_menu.h"
#include <sysdolphin/baselib/gobjgxlink.h>

#include <dolphin/gx.h>
#include <dolphin/dvd.h>
#include <dolphin/os.h>
#include <math.h>
#include <melee/lb/lbfile.h>
#include <melee/cm/camera.h>
#include <melee/ft/ftlib.h>
#include <melee/ft/types.h>
#include <melee/pl/player.h>
#include <melee/lb/types.h>
#include <sysdolphin/baselib/shadow.h>
#include <sysdolphin/baselib/tobj.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjuserdata.h>
#include <sysdolphin/baselib/initialize.h>
#include <sysdolphin/baselib/memory.h>
#include <sysdolphin/baselib/state.h>
#include <sysdolphin/baselib/tev.h>

typedef struct {
    u32 magic, version, materials, triangles, size, sky, data, reserved;
} VisualHeader;
typedef struct {
    u32 width, height, wrap_s, wrap_t, offset, size, reserved[2];
} VisualMaterial;
typedef struct { Vec3 pos; GXColor color; float u, v; } VisualVertex;
typedef struct { u32 material; VisualVertex vertices[3]; } VisualTriangle;

static u8* visual_bank;
static VisualHeader* visual;
static VisualMaterial* materials;
static VisualTriangle* triangles;
typedef struct { Vec3 center; u32 reserved; } VisualBillboard;
static VisualBillboard* billboards;
static GXTexObj textures[128];
static int shadow_receivers;
static int marker_alpha[GM_MAX_PLAYERS];
int m3d_GroundMarkerAlpha(int slot) { return slot>=0 && slot < GM_MAX_PLAYERS ? marker_alpha[slot] : 0; }
int m3d_ArenaShadowReceivers(void) { return shadow_receivers; }

void m3d_ArenaVisualLoad(bool enabled)
{
    VisualHeader* h;
    HSD_GObj* owner;
    int size, i, j, entry, capacity;
    size_t file_size;
    char filename[64];
    shadow_receivers = 0;
    visual = NULL;
    visual_bank = NULL;
    if (!enabled) return;
    m3d_ArenaFilename("_vis.bin", filename);
    entry = DVDConvertPathToEntrynum(filename);
    if (entry < 0) return;
    file_size = lbFile_8001634C(entry);
    if (file_size < 32 || file_size > 2097152) {
        OSReport("M3D ERR invalid presentation size\n");
        return;
    }
    capacity = ((file_size + 31) & ~31) + 64;
    visual_bank = OSAllocFromHeap(HSD_GetHeap(), capacity);
    if (visual_bank == NULL) {
        OSReport("M3D PRESENTATION plain fallback: allocation unavailable\n");
        return;
    }
    /* Match-owned storage: native scene teardown frees this owner (or resets
     * the scene heap). No persistent asset buffer consumes menu heap space. */
    owner = GObj_Create(3, 5, 0);
    GObj_InitUserData(owner, 4, HSD_Free, visual_bank);
    h = (VisualHeader*) visual_bank;
    size = m3d_LoadFile(filename, (char*) visual_bank,
                       capacity);
    if (size < 0) return; /* Older/private builds can keep their plain mesh. */
    if (size < 32 || h->magic != 0x4D335631 || (h->version < 1 || h->version > 3) ||
        h->materials < 1 || h->materials > 128 || h->triangles < 1 ||
        h->triangles > 16384 || h->size != size || (h->sky >= h->materials && !(h->version == 3 && h->sky == 0xFFFFFFFF)) ||
        (h->version == 1 && h->reserved) || h->reserved > 255 ||
        h->data & 31 || h->data > h->size ||
        h->data < 32 + h->materials * 32 + h->triangles * 76 +
                      h->reserved * sizeof(VisualBillboard))
    {
        OSReport("M3D ERR invalid presentation header\n");
        return;
    }
    materials = (VisualMaterial*) (h + 1);
    triangles = (VisualTriangle*) (materials + h->materials);
    billboards = (VisualBillboard*) (triangles + h->triangles);
    for (i = 0; i < h->reserved; i++) {
        VisualBillboard* b = &billboards[i];
        if (!(fabsf(b->center.x) <= 10000) ||
            !(fabsf(b->center.y) <= 10000) ||
            !(fabsf(b->center.z) <= 10000) || b->reserved)
        {
            OSReport("M3D ERR invalid presentation billboard\n");
            return;
        }
    }
    for (i = 0; i < h->materials; i++) {
        VisualMaterial* m = &materials[i];
        u32 format = h->version == 3 ? m->reserved[0] : GX_TF_RGB5A3;
        u32 bytes = m->width * m->height;
        bool valid_format = format <= 6 || format == GX_TF_CMPR;
        u32 tile_width = format <= GX_TF_IA4 || format == GX_TF_CMPR ? 8 : 4;
        u32 tile_height = format == GX_TF_I4 || format == GX_TF_CMPR ? 8 : 4;
        if (format == GX_TF_I4 || format == GX_TF_CMPR) bytes /= 2;
        else if (format == GX_TF_RGBA8) bytes *= 4;
        else if (format >= GX_TF_IA8) bytes *= 2;
        if (m->width < 4 || m->width > 1024 || m->height < 4 ||
            m->height > 1024 || m->width % tile_width || m->height % tile_height ||
            m->wrap_s > 2 || m->wrap_t > 2 || m->offset & 31 ||
            m->offset < h->data || m->offset > h->size ||
            !valid_format || m->size != bytes ||
            m->size > h->size - m->offset ||
            (h->version < 3 && (m->reserved[0] || m->reserved[1])) ||
            (h->version == 3 && m->reserved[1] > 7))
        {
            OSReport("M3D ERR invalid presentation texture\n");
            return;
        }
    }
    for (i = 0; i < h->triangles; i++) {
        VisualTriangle* t = &triangles[i];
        if ((t->material & 65535) >= h->materials ||
            (t->material & 65535) == h->sky ||
            (t->material >> 16) > h->reserved) {
            OSReport("M3D ERR invalid presentation material\n");
            return;
        }
        for (j = 0; j < 3; j++) {
            VisualVertex* v = &t->vertices[j];
            if (!(fabsf(v->pos.x) <= 10000) || !(fabsf(v->pos.y) <= 10000) ||
                !(fabsf(v->pos.z) <= 10000) || !(fabsf(v->u) <= 10000) ||
                !(fabsf(v->v) <= 10000))
            {
                OSReport("M3D ERR invalid presentation vertex\n");
                return;
            }
        }
    }
    DCFlushRange(visual_bank, (size + 31) & ~31);
    GXInvalidateTexAll();
    for (i = 0; i < h->materials; i++) {
        VisualMaterial* m = &materials[i];
        GXInitTexObj(&textures[i], visual_bank + m->offset, m->width,
                     m->height, h->version == 3 ? m->reserved[0] : GX_TF_RGB5A3,
                     m->wrap_s, m->wrap_t, GX_FALSE);
        GXInitTexObjLOD(&textures[i], GX_LINEAR, GX_LINEAR, 0, 0, 0,
                        GX_FALSE, GX_FALSE, GX_ANISO_1);
    }
    visual = h;
    OSReport("M3D PRESENTATION textures=%d triangles=%d sky=%d\n",
             h->materials, h->triangles, h->sky);
}

static void vertex(VisualVertex* v, Mtx view, u32 group)
{
    if (group) {
        Vec3* c = &billboards[group - 1].center;
        /* Native billboard basis: camera right/up, centred on the source
         * object. Camera motion changes orientation, never its world anchor. */
        GXPosition3f32(c->x + view[0][0] * v->pos.x +
                                  view[1][0] * v->pos.y + view[2][0] * v->pos.z,
                      c->y + view[0][1] * v->pos.x +
                                  view[1][1] * v->pos.y + view[2][1] * v->pos.z,
                      c->z + view[0][2] * v->pos.x +
                                  view[1][2] * v->pos.y + view[2][2] * v->pos.z);
    } else {
        GXPosition3f32(v->pos.x, v->pos.y, v->pos.z);
    }
    GXColor4u8(v->color.r, v->color.g, v->color.b, v->color.a);
    GXTexCoord2f32(v->u, v->v);
}

static void sky_vertex(Mtx view, float x, float y, float tangent, float aspect,
                       float center_yaw)
{
    float pi = 3.14159265f;
    float dx = x * tangent * aspect, dy = y * tangent;
    float wx = view[0][0] * dx + view[1][0] * dy - view[2][0];
    float wy = view[0][1] * dx + view[1][1] * dy - view[2][1];
    float wz = view[0][2] * dx + view[1][2] * dy - view[2][2];
    float yaw = atan2f(wx, wz), delta = yaw - center_yaw;
    float pitch = atan2f(wy, sqrtf(wx * wx + wz * wz));
    if (delta > pi) delta -= 2 * pi;
    if (delta < -pi) delta += 2 * pi;
    GXPosition3f32(dx * 1000, dy * 1000, -1000);
    GXColor4u8(255, 255, 255, 255);
    /* The native SM64 tilemap covers 360 degrees horizontally and 240
     * vertically. Camera translation never moves this distant background. */
    GXTexCoord2f32(1 - (center_yaw + delta) / (2 * pi),
                   0.5f - pitch / (pi * 4.0f / 3.0f));
}

/* Reuse Melee's animated silhouette maps. Custom immediate-mode meshes do
 * not have HSD materials to receive them, so project the same native texture
 * onto source floor triangles in a multiplicative pass. White border texels
 * leave the surface unchanged; the native silhouette supplies the darkness. */
static void render_shadows(void)
{
    HSD_GObj* gobj;
    GXTexObj texture;
    int i, j;
    shadow_receivers = 0;
    GXSetBlendMode(GX_BM_BLEND, GX_BL_ZERO, GX_BL_SRCCLR, GX_LO_NOOP);
    GXSetZMode(GX_ENABLE, GX_LEQUAL, GX_DISABLE);
    GXSetCullMode(GX_CULL_NONE);
    GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GXSetTevOp(GX_TEVSTAGE0, GX_REPLACE);
    for (gobj = HSD_GObjPLinkHead[HSD_GOBJ_PLINK_FIGHTER]; gobj;
         gobj = gobj->next)
    {
        Fighter* fp = gobj->user_data;
        LbShadow* lbs = ftLib_GetShadow(gobj);
        HSD_Shadow* shadow;
        HSD_ImageDesc* image;
        if (!lbs || !(shadow = lbs->shadow) || !shadow->active ||
            !shadow->texture || !(image = shadow->texture->imagedesc) ||
            !image->image_ptr) continue;
        GXInitTexObj(&texture, DP(void,image->image_ptr), image->width, image->height,
                     image->format, GX_CLAMP, GX_CLAMP, GX_FALSE);
        GXInitTexObjLOD(&texture, GX_LINEAR, GX_LINEAR, 0, 0, 0, GX_FALSE,
                        GX_FALSE, GX_ANISO_1);
        GXLoadTexObj(&texture, GX_TEXMAP0);
        for (i = 0; i < visual->triangles; i++) {
            VisualTriangle* t = &triangles[i];
            Vec3 a, b;
            float nx, ny, nz, uv[3][2], low[2], high[2];
            bool above = true;
            if (t->material >> 16 ||
                (visual->version == 3 &&
                 (materials[t->material].reserved[1] & 3))) continue;
            a.x = t->vertices[1].pos.x-t->vertices[0].pos.x;
            a.y = t->vertices[1].pos.y-t->vertices[0].pos.y;
            a.z = t->vertices[1].pos.z-t->vertices[0].pos.z;
            b.x = t->vertices[2].pos.x-t->vertices[0].pos.x;
            b.y = t->vertices[2].pos.y-t->vertices[0].pos.y;
            b.z = t->vertices[2].pos.z-t->vertices[0].pos.z;
            nx = a.y*b.z-a.z*b.y;
            ny = a.z*b.x-a.x*b.z;
            nz = a.x*b.y-a.y*b.x;
            /* Walls, billboards and sky decoration are not floor receivers. */
            if (ny*ny < 0.01f*(nx*nx+ny*ny+nz*nz)) continue;
            low[0] = low[1] = 1.0e20f;
            high[0] = high[1] = -1.0e20f;
            for (j = 0; j < 3; j++) {
                Vec3* p = &t->vertices[j].pos;
                int k;
                if (p->y <= fp->cur_pos.y + 0.1f) above = false;
                for (k = 0; k < 2; k++) {
                    uv[j][k] = shadow->texture->mtx[k][0]*p->x +
                               shadow->texture->mtx[k][1]*p->y +
                               shadow->texture->mtx[k][2]*p->z +
                               shadow->texture->mtx[k][3];
                    if (uv[j][k] < low[k]) low[k] = uv[j][k];
                    if (uv[j][k] > high[k]) high[k] = uv[j][k];
                }
            }
            if (above || high[0] < 0 || high[1] < 0 ||
                low[0] > 1 || low[1] > 1) continue;
            shadow_receivers++;
            GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
            for (j = 0; j < 3; j++) {
                Vec3* p = &t->vertices[j].pos;
                GXPosition3f32(p->x, p->y + 0.02f, p->z);
                GXColor4u8(255, 255, 255, 255);
                GXTexCoord2f32(uv[j][0], uv[j][1]);
            }
            GXEnd();
        }
    }
}

/* Circular SM64-style CPU/remote-player shadows, fitted to live floors. */
static void ground_markers(void)
{
    HSD_GObj* gobj;
    int i;
    for (i=0;i < GM_MAX_PLAYERS;i++) marker_alpha[i]=0;
    GXSetNumTexGens(0);
    GXSetNumTevStages(1);
    GXSetTevOrder(GX_TEVSTAGE0,GX_TEXCOORD_NULL,GX_TEXMAP_NULL,GX_COLOR0A0);
    GXSetTevOp(GX_TEVSTAGE0,GX_PASSCLR);
    GXSetVtxDesc(GX_VA_TEX0,GX_NONE);
    GXSetBlendMode(GX_BM_BLEND,GX_BL_SRCALPHA,GX_BL_INVSRCALPHA,GX_LO_NOOP);
    GXSetZMode(GX_ENABLE,GX_LEQUAL,GX_ENABLE);
    GXSetCullMode(GX_CULL_NONE);
    for (gobj=HSD_GObjPLinkHead[HSD_GOBJ_PLINK_FIGHTER];gobj;gobj=gobj->next) {
        Fighter* fp=gobj->user_data;
        Vec3 center, query=fp->cur_pos;
        float height, radius=3.0f;
        int alpha;
        Vec3 rim[24];
        bool supported[24];
        if (fp->is_sleeping || fp->invisible || !m3d_FighterIs3D(fp)) continue;
        query.y+=1;
        if (!m3d_GroundBelow(&query,&center)) continue;
        height=fp->cur_pos.y-center.y;
        if (height<0) height=0;
        if (height>150) height=150;
        alpha=130-(int)(height*98/150);
        marker_alpha[fp->player_idx]=alpha;
        for (i=0;i<24;i++) {
            float a=i*6.2831853f/24;
            Vec3 p=fp->cur_pos;
            p.x+=cosf(a)*radius; p.z+=sinf(a)*radius; p.y+=5;
            supported[i]=m3d_GroundBelow(&p,&rim[i]) && fabsf(rim[i].y-center.y)<=radius;
        }
        for (i=0;i<24;i++) {
            if(!fp->player_idx)continue;
            Vec3 u,v;
            if(!supported[i] || !supported[(i+1)%24])continue;
            u=rim[i];v=rim[(i+1)%24];
            GXBegin(GX_TRIANGLES,GX_VTXFMT0,3);
            GXPosition3f32(center.x,center.y+0.04f,center.z);
            GXColor4u8(0,0,0,alpha);
            GXPosition3f32(u.x,u.y+0.04f,u.z); GXColor4u8(0,0,0,0);
            GXPosition3f32(v.x,v.y+0.04f,v.z); GXColor4u8(0,0,0,0);
            GXEnd();
        }
    }
}

static void marker_render(HSD_GObj* gobj, intptr_t pass)
{
    GXColor clear={0,0,0,255};
    if (pass || !m3d_StageIs3D() || Camera_8003108C()!=0) return;
    HSD_StateInvalidate(-1); HSD_StateInitTev();
    GXSetFog(GX_FOG_NONE,0,0,0,0,clear);
    GXSetColorUpdate(GX_ENABLE); GXSetAlphaUpdate(GX_DISABLE);
    GXSetAlphaCompare(GX_ALWAYS,0,GX_AOP_AND,GX_ALWAYS,0);
    GXSetNumChans(1);
    GXSetChanCtrl(GX_COLOR0A0,GX_DISABLE,GX_SRC_REG,GX_SRC_VTX,0,GX_DF_NONE,GX_AF_NONE);
    GXClearVtxDesc();
    GXSetVtxAttrFmt(GX_VTXFMT0,GX_VA_POS,GX_POS_XYZ,GX_F32,0);
    GXSetVtxAttrFmt(GX_VTXFMT0,GX_VA_CLR0,GX_CLR_RGBA,GX_RGBA8,0);
    GXSetVtxDesc(GX_VA_POS,GX_DIRECT); GXSetVtxDesc(GX_VA_CLR0,GX_DIRECT);
    GXLoadPosMtxImm(HSD_CObjGetViewingMtxPtrDirect(HSD_CObjGetCurrent()),0);
    GXSetCurrentMtx(0);
    ground_markers();
    HSD_StateInvalidate(-1); HSD_StateInitTev();
}
void m3d_GroundMarkersInit(void)
{
    HSD_GObj* gobj=GObj_Create(3,5,0);
    GObj_SetupGXLink(gobj,marker_render,3,1);
}

bool m3d_ArenaVisualRender(void)
{
    Mtx view, identity;
    HSD_CObj* cobj;
    GXColor clear = { 0, 0, 0, 255 };
    float tangent, aspect, yaw, x, y, step = 0.125f;
    int i, j, bound = -1;
    if (!visual) return false;
    cobj = HSD_CObjGetCurrent();
    HSD_CObjGetViewingMtx(cobj, view);
    tangent = tanf(HSD_CObjGetFov(cobj) * 3.14159265f / 360.0f);
    aspect = HSD_CObjGetAspect(cobj);
    yaw = atan2f(-view[2][0], -view[2][2]);
    HSD_StateInvalidate(-1);
    HSD_StateInitTev();
    GXSetFog(GX_FOG_NONE, 0, 0, 0, 0, clear);
    GXSetColorUpdate(GX_ENABLE);
    GXSetAlphaUpdate(GX_DISABLE);
    GXSetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_NOOP);
    GXSetAlphaCompare(GX_GREATER, 0, GX_AOP_AND, GX_GREATER, 0);
    GXSetZCompLoc(GX_DISABLE);
    GXSetNumTexGens(1);
    GXSetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
    GXSetNumTevStages(1);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
    GXSetTevOp(GX_TEVSTAGE0, GX_MODULATE);
    GXSetNumChans(1);
    GXSetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX, 0,
                  GX_DF_NONE, GX_AF_NONE);
    GXSetCullMode(GX_CULL_NONE);
    GXClearVtxDesc();
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GXSetCurrentMtx(0);
    PSMTXIdentity(identity);
    GXLoadPosMtxImm(identity, 0);
    if (visual->sky != 0xFFFFFFFF) {
    GXLoadTexObj(&textures[visual->sky], GX_TEXMAP0);
    GXSetZMode(GX_DISABLE, GX_ALWAYS, GX_DISABLE);
    GXBegin(GX_QUADS, GX_VTXFMT0, 16 * 16 * 4);
    for (y = -1; y < 1; y += step) {
        for (x = -1; x < 1; x += step) {
            sky_vertex(view, x, y, tangent, aspect, yaw);
            sky_vertex(view, x + step, y, tangent, aspect, yaw);
            sky_vertex(view, x + step, y + step, tangent, aspect, yaw);
            sky_vertex(view, x, y + step, tangent, aspect, yaw);
        }
    }
    GXEnd();
    }
    GXLoadPosMtxImm(view, 0);
    GXSetZMode(GX_ENABLE, GX_LEQUAL, GX_ENABLE);
    for (i = 0; i < visual->triangles; i++) {
        VisualTriangle* t = &triangles[i];
        if (bound != (t->material & 65535)) {
            GXLoadTexObj(&textures[t->material & 65535], GX_TEXMAP0);
            bound = t->material & 65535;
            GXSetCullMode(visual->version == 3 && !(materials[bound].reserved[1] & 4)
                          ? GX_CULL_BACK : GX_CULL_NONE);
            if (visual->version == 3 && (materials[bound].reserved[1] & 3)) {
                GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA,
                              materials[bound].reserved[1] & 2 ? GX_BL_ONE : GX_BL_INVSRCALPHA,
                              GX_LO_NOOP);
                GXSetZMode(GX_ENABLE, GX_LEQUAL, GX_DISABLE);
            } else {
                GXSetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_NOOP);
                GXSetZMode(GX_ENABLE, GX_LEQUAL, GX_ENABLE);
            }
        }
        GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
        for (j = 0; j < 3; j++) vertex(&t->vertices[j], view, t->material >> 16);
        GXEnd();
    }
    render_shadows();
    HSD_StateInvalidate(-1);
    HSD_StateInitTev();
    return true;
}

int m3d_ArenaShadowTexels(void)
{
    HSD_GObj* gobj;
    int count = 0, total = 0;
    if (!visual) return 0;
    /* Inspect the completed previous frame; waiting for a new GPU token in
     * the pad/script callback would block the render submission thread. */
    for (gobj = HSD_GObjPLinkHead[HSD_GOBJ_PLINK_FIGHTER]; gobj;
         gobj = gobj->next) {
        LbShadow* lbs = ftLib_GetShadow(gobj);
        HSD_ImageDesc* image;
        u8* bytes;
        int i, size;
        if (!lbs || !lbs->shadow || !lbs->shadow->active ||
            !lbs->shadow->texture ||
            !(image = lbs->shadow->texture->imagedesc) ||
            !image->image_ptr || image->format != GX_TF_I4) continue;
        size = image->width*image->height/2;
        total += size*2;
        DCInvalidateRange(DP(void,image->image_ptr), size);
        bytes = DP(u8,image->image_ptr);
        for (i = 0; i < size; i++) {
            if ((bytes[i] & 15) < 15) count++;
            if ((bytes[i] >> 4) < 15) count++;
        }
    }
    /* An untouched RAM backing store is not evidence of a silhouette. */
    return count == total ? 0 : count;
}

void smb_native_ground_markers(void) {
    marker_render(NULL,0);
    HSD_GObj* g=Player_GetEntity(0);static unsigned frame;
    if(g && ++frame%30==0) {
        Fighter* f=g->user_data;Vec3 q=f->cur_pos,ground;q.y+=1;
        if(m3d_GroundBelow(&q,&ground))fprintf(stderr,"SMB GROUND MARKER frame=%u alpha=%d height=%.3f floor=%.3f line=0 live_sm64=1\n",frame,m3d_GroundMarkerAlpha(0),f->cur_pos.y-ground.y,ground.y);
    }
}
