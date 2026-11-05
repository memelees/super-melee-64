/* The same native stock artwork used by Melee's HUD, in SM64's lives slot. */
#include <sysdolphin/baselib/mobj.h>
#include <sysdolphin/baselib/gobjplink.h>
#include <sysdolphin/baselib/aobj.h>
#include <melee/lb/lbspdisplay.h>
static void smb_draw_lives_icon(float aspect) {
    static HSD_GObj* icon;static HSD_JObj* face;static HSD_CObj* camera;
    static int character=-1,costume=-1;static float previous=-1;
    if(character!=Player_GetPlayerCharacter(0)||costume!=Player_GetCostumeId(0)) {
        if(icon)HSD_GObjFree(icon);icon=NULL;face=NULL;previous=-1;
        character=Player_GetPlayerCharacter(0);costume=Player_GetCostumeId(0);
    }
    if(!icon) {
        icon=ifStock_802F96D0(Player_GetPlayerCharacter(0),Player_GetCostumeId(0),0,0);
        m3d_HudStock(icon->hsd_obj);lb_80011E24(icon->hsd_obj,&face,1,-1);
        if(!camera)camera=HSD_CObjAlloc();Vec3 eye={0,0,100},target={0,0,0};
        HSD_CObjSetEyePosition(camera,&eye);HSD_CObjSetInterest(camera,&target);
        HSD_CObjSetNear(camera,1);HSD_CObjSetFar(camera,200);
        HSD_CObjSetOrtho(camera,1.2f,-1.2f,-1.2f,1.2f);
        HSD_CObjSetViewportfx4(camera,44,76,30,62);camera->scissor=(Scissor){44,76,30,62};
    }
    float frame=m3d_HudStockFrame(0);
    if(previous!=frame) {
        HSD_TObjReqAnimAll(face->u.dobj->mobj->tobj,frame);
        HSD_AObjSetRate(face->u.dobj->mobj->tobj->aobj,0);HSD_JObjAnimAll(icon->hsd_obj);
        previous=frame;fprintf(stderr,"SMB LIVES ICON character=%d frame=%.0f native_stock=1\n",Player_GetPlayerCharacter(0),frame);
    }
    Vec3 origin={0,0,0};HSD_JObjSetTranslate(icon->hsd_obj,&origin);HSD_JObjSetTranslate(face,&origin);
    HSD_JObjClearFlags(face,JOBJ_HIDDEN);
    float sx=(4.f/3)/aspect;
    HSD_CObjSetViewportfx4(camera,44*sx,76*sx,30,62);camera->scissor=(Scissor){44*sx,76*sx,30,62};
    if(HSD_CObjSetCurrent(camera)) {
        smb_native_hud_rendering=1;HSD_StateInvalidate(-1);HSD_StateInitTev();
        GXSetViewport(44*sx,30,32*sx,32,0,0);GXSetScissor(44*sx,30,ceilf(32*sx),32);
        for(int pass=0;pass<3;pass++){HSD_GObj_804D7814=icon;icon->render_cb(icon,pass);}
        smb_native_hud_rendering=0;HSD_CObjEndCurrent();
    }
}
