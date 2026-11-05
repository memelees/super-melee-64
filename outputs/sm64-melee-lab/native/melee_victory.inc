/* Real result/demo fighters use their native result archives and command
 * scripts. They animate only during SM64's star dance; gameplay fighters and
 * the SM64 cutscene/save/exit timing are preserved. GCC owns DAT access. */
#include <melee/ft/ftdemo.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftdata.h>
#include <melee/gm/gm_1601.h>
#include <melee/lb/lbarchive.h>
#include <melee/pl/player.h>
#include <melee/pl/types.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <sysdolphin/baselib/gobjplink.h>
static HSD_GObj* smb_victory[2];
static int smb_victory_active,smb_victory_kind=-1;
static unsigned smb_victory_ticks;
int smb_native_victory_item_hidden(HSD_GObj* owner) {
    return !smb_victory_active&&owner&&(owner==smb_victory[0]||owner==smb_victory[1]);
}
HSD_GObj* smb_native_celebrant(unsigned n) {return smb_victory_active&&n<2?smb_victory[n]:NULL;}
int smb_embed_celebrate(int enabled,const float* position) {
    HSD_GObj* live=Player_GetEntity(0);if(!live)return -1;
    Fighter* game=live->user_data;
    if(!enabled) {
        if(smb_victory_active)fprintf(stderr,"SMB VICTORY END kind=%d ticks=%u\n",smb_victory_kind,smb_victory_ticks);
        smb_victory_active=0;
        if(smb_victory[0]||smb_victory[1]) {
            StaticPlayer* player=Player_GetPtrForSlot(0);StaticPlayer saved=*player;
            for(unsigned i=0;i<2;i++)if(smb_victory[i]){HSD_GObjFree(smb_victory[i]);smb_victory[i]=NULL;}
            *player=saved;smb_victory_kind=-1;
            fprintf(stderr,"SMB VICTORY CLEANUP gameplay_preserved=%d demos_remaining=0\n",Player_GetEntity(0)==live);
        }
        return 0;
    }
    if(!smb_victory_active) {
        if(smb_victory_kind!=game->kind) {
            /* Demo unload also unregisters a player's entity. These presentation
             * fighters share slot 0, so preserve the real gameplay slot. */
            StaticPlayer* player=Player_GetPtrForSlot(0);StaticPlayer saved=*player;
            for(unsigned i=0;i<2;i++)if(smb_victory[i]){HSD_GObjFree(smb_victory[i]);smb_victory[i]=NULL;}
            *player=saved;
            fprintf(stderr,"SMB VICTORY GAMEPLAY PRESERVED kind=%d entity=%d\n",game->kind,Player_GetEntity(0)==live);
            CharacterKind kind=Player_GetPlayerCharacter(0);
            /* Zelda/Sheik can transform without changing the select-screen ID. */
            if(game->kind==7)kind=CKind_Seak;else if(game->kind==19)kind=CKind_Zelda;
            const char* archive=gm_80160438(kind);
            if(!archive)return -2;
            Player_80036E20(kind,lbArchive_LoadArchive(archive),0);
            for(unsigned i=0;i<2;i++) {
                HSD_GObj* source=i?Player_GetEntityAtIndex(0,1):live;
                if(!source||(i&&((Fighter*)source->user_data)->is_sleeping))continue;
                Fighter* fp=source->user_data;
                plAllocInfo2 info={0};info.internal_id=fp->kind;info.slot=0;info.unk8=0;
                info.xC.has_transformation=i!=0;
                smb_victory[i]=ftDemo_CreateFighter(&info);
                HSD_GObjProc_RemoveAllProcs(smb_victory[i]);
                Fighter* demo=smb_victory[i]->user_data;
                fprintf(stderr,"SMB VICTORY LOAD kind=%d sub=%u archive=%s animation=%d tree=%d\n",demo->kind,i,archive,demo->anim_id,demo->x590!=NULL);
            }
            smb_victory_kind=game->kind;
        }
        smb_victory_ticks=0;smb_victory_active=1;
        for(unsigned i=0;i<2;i++)if(smb_victory[i]) {
            Fighter* fp=smb_victory[i]->user_data;fp->is_sleeping=false;
            Fighter_ChangeMotionState(smb_victory[i],0,Ft_MF_None,0,1,0,NULL);
        }
        fprintf(stderr,"SMB VICTORY BEGIN kind=%d native_result=1\n",game->kind);
    }
    HSD_GObj* previous=HSD_GObj_804D7814;
    for(unsigned t=0;t<2;t++)for(unsigned i=0;i<2;i++)if(smb_victory[i]) {
        HSD_GObj* object=smb_victory[i];Fighter* fp=object->user_data;
        fp->cur_pos=(Vec3){position[0]+(i?5:0),position[1],position[2]};fp->prev_pos=fp->cur_pos;
        HSD_JObjSetTranslate(object->hsd_obj,&fp->cur_pos);
        HSD_GObj_804D7814=object;Fighter_procAnim(object);Fighter_procDynamics(object);
    }
    HSD_GObj_804D7814=previous;smb_victory_ticks+=2;
    if(smb_victory_ticks%30==0) {
        Fighter* fp=smb_victory[0]->user_data;
        fprintf(stderr,"SMB VICTORY ANIMATE kind=%d ticks=%u frame=%.3f tree=%d\n",fp->kind,smb_victory_ticks,fp->cur_anim_frame,fp->x590!=NULL);
    }
    return 0;
}
