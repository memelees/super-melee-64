#include <melee/ft/ftmetal.h>
#include <melee/ft/ft_0C88.h>
static unsigned smb_cap_flags;
int smb_embed_powerups(unsigned flags) {
    smb_cap_flags=flags;return 0;
}
void smb_native_powerups(void) {
    for(unsigned sub=0;sub<2;sub++) {
        HSD_GObj* object=Player_GetEntityAtIndex(0,sub);if(!object)continue;
        Fighter* fp=object->user_data;
        int metal=(smb_cap_flags&1)!=0,cloak=(smb_cap_flags&2)!=0;
        int was_metal=fp->is_metal,was_cloak=fp->x2226_b4;
        /* SM64's timer is authoritative: native damage and timers cannot
         * prematurely consume a world cap. Native rendering/physics still run. */
        if(metal)ftCo_800C8348(object,4,1000000);
        else if(was_metal)ftCo_800C8540(object);
        if(cloak)ftCo_800C88D4(object,4,false);
        else if(was_cloak)ftCo_800C8A64(object);
        if(was_metal!=metal||was_cloak!=cloak)fprintf(stderr,"SMB POWERUP kind=%d sub=%u metal=%d cloak=%d native_metal=%d native_cloak=%d\n",fp->kind,sub,metal,cloak,fp->is_metal,fp->x2226_b4);
    }
}

/* Retail cloak samples Melee's EFB, which has no SM64 world behind it.
 * Use the same native fighter material's opacity channel in this host pass,
 * preserving the real cloak state/timer and SM64 vanish interactions. */
static struct {HSD_GObj* object;unsigned char opacity,cloak,overlay;GXColor color;} cap_render[GM_MAX_PLAYERS*2];
int smb_native_cap_translucent(HSD_GObj* object) {return object&&((Fighter*)object->user_data)->x2226_b4;}
void smb_native_cap_render(HSD_GObj* object,int begin) {
    if(!object)return;Fighter* fp=object->user_data;
    if(begin) {
        if(!fp->x2226_b4)return;
        for(unsigned i=0;i<GM_MAX_PLAYERS*2;i++)if(!cap_render[i].object) {
            cap_render[i].object=object;cap_render[i].opacity=fp->x61D;cap_render[i].cloak=fp->x2226_b5;
            cap_render[i].overlay=fp->x2223_b2;cap_render[i].color=fp->x610_color_rgba[1];
            fp->x2226_b5=false;fp->x61D=80;
            static int logged;if(!logged++){fprintf(stderr,"SMB CLOAK RENDER native_opacity=80 host_transmission=1\n");}return;
        }
        abort();
    } else for(unsigned i=0;i<GM_MAX_PLAYERS*2;i++)if(cap_render[i].object==object) {
        fp->x2223_b2=cap_render[i].overlay;fp->x610_color_rgba[1]=cap_render[i].color;
        fp->x61D=cap_render[i].opacity;fp->x2226_b5=cap_render[i].cloak;cap_render[i].object=NULL;return;
    }
}
