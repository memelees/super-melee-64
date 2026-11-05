/* The native shield model is safe to attach without running Guard's input,
 * shield-health drain, reflector or collision callbacks on a proxy. */
#include <melee/ef/types.h>
#include <melee/ef/eflib.h>
#include <melee/ef/efsync.h>
#include <melee/ft/fighter.h>
#include <melee/ft/kinds/ftCommon/ftCo_Guard.h>
#include <melee/ft/kinds/ftYoshi/ftyoshi.h>
#include <sysdolphin/baselib/gobjplink.h>
static HSD_GObj* remote_shields[GM_MAX_PLAYERS][2];
static unsigned char remote_shield_mode[GM_MAX_PLAYERS][2];
static int smb_remote_shield_live(HSD_GObj* candidate,HSD_GObj* owner) {
    if(!candidate)return 0;
    const unsigned links[]={HSD_GOBJ_PLINK_EFFECT_UNK0,HSD_GOBJ_PLINK_EFFECT_UNK1};
    for(unsigned n=0;n<2;n++)for(HSD_GObj* object=HSD_GObjPLinkHead[links[n]];object;object=object->next)
        if(object==candidate){EF_Effect* effect=object->user_data;return effect&&effect->parent_gobj==owner&&effect->stored_alpha_variant==1;}
    return 0;
}
static void smb_native_remote_shield(unsigned seat,unsigned sub,int shielding) {
    HSD_GObj* owner=Player_GetEntityAtIndex(seat,sub);Fighter* fp=owner?owner->user_data:NULL;
    if(!fp)shielding=0;
    int live=smb_remote_shield_live(remote_shields[seat][sub],owner);
    if(!shielding) {
        if(live)HSD_GObjFree(remote_shields[seat][sub]);remote_shields[seat][sub]=NULL;
        if(remote_shield_mode[seat][sub]) {
            for(unsigned i=0;i<EF_PARAM_TABLE_SIZE;i++)if(efLib_ParamTable[i].gobj==owner&&efLib_ParamTable[i].gfx_id==1048)
                efLib_ParamTable[i]=(EF_ParamEntry){NULL,0,255};
            if(fp&&fp->kind==Ft_Kind_Yoshi){ftParts_80074B0C(owner,0,0);ftYs_Init_8012B918(owner);}
            if(fp&&fp->kind==Ft_Kind_Mars)ftParts_80074B0C(owner,1,0);
            remote_shield_mode[seat][sub]=0;
            fprintf(stderr,"SMB COOP SHIELD seat=%u sub=%u active=0\n",seat,sub);
        }
        return;
    }
    fp->shield_health=p_ftCommonData->x260_startShieldHealth;fp->lightshield_amount=0;
    ftCo_80091D58(fp);
    if(fp->kind==Ft_Kind_Yoshi) {
        ftParts_80074B0C(owner,0,1);ftYs_Init_8012B8A4(owner);
    } else {
        if(fp->kind==Ft_Kind_Mars)ftParts_80074B0C(owner,1,1);
        if(!live) {
            HSD_JObj* joint=fp->parts[DP(struct ftData_x8,fp->ft_data->x8)->x11].joint;
            unsigned palette=Player_GetUnk45(seat)%4;u8* color=Fighter_804D650C+palette*4;
            unsigned rgb=((unsigned)color[0]<<16)|((unsigned)color[1]<<8)|color[2];
            EF_Effect* effect=efSync_Spawn(1048,owner,joint,rgb);
            if(effect){effect->lifetime=0;effect->expire_flags=EF_DOES_NOT_EXPIRE;remote_shields[seat][sub]=effect->gobj;}
        }
        efLib_SetParamAlpha(owner,(u8)p_ftCommonData->x2F4);
    }
    if(!remote_shield_mode[seat][sub])fprintf(stderr,"SMB COOP SHIELD seat=%u sub=%u active=1 native_model=1 egg=%u drain=0\n",seat,sub,fp->kind==Ft_Kind_Yoshi);
    remote_shield_mode[seat][sub]=1;
}
