/* Opt-in live regression fixture; called only after an authored native tick. */
#include <limits.h>
extern void smb_native_reset_world_items(void);
static uint64_t smb_remote_test_models(Fighter* fp) {
    uint64_t signature=1469598103934665603ull;
    SmbFighterModel model;smb_native_remote_model(fp,&model);if(!model.model_count)return 0;
    for(unsigned n=0;n<model.model_count;n++)signature=(signature^(uint32_t)(model.part[n]+1))*1099511628211ull;
    for(unsigned n=0;n<8;n++)signature=(signature^model.dobj_hidden[n])*1099511628211ull;
    return signature;
}
static void smb_remote_test_pure(Fighter* fp) {
    if(!fp||fp->cam_cb||fp->anim_cb||fp->input_cb||fp->phys_cb||fp->coll_cb||fp->accessory1_cb||fp->accessory2_cb||fp->accessory3_cb||fp->accessory4_cb||
       !smb_native_remote_controlled(fp)||fp->ground_or_air!=GA_Air||fp->victim_gobj||fp->throw_thrower)goto failed;
    for(unsigned h=0;h<4;h++)if(fp->x914[h].state!=HitCapsule_Disabled)goto failed;
    for(unsigned h=0;h<fp->hurt_capsules_len;h++)if(fp->hurt_capsules[h].capsule.state!=HurtCapsule_Disabled||fp->hurt_capsules[h].is_grabbable)goto failed;
    for(HSD_GObj* object=HSD_GObjPLinkHead[HSD_GOBJ_PLINK_ITEM];object;object=object->next) {
        Item* item=object->user_data;
        if(item&&(item->owner==fp->gobj))goto failed;
    }
    return;
failed:
    fprintf(stderr,"SMB REMOTE STATES FAIL reason=gameplay_side_effect kind=%d motion=%d\n",fp?fp->kind:-1,fp?fp->motion_id:-1);exit(2);
}
static void smb_remote_test_states(const SmbOutput* output,unsigned step) {
    if(step<430)return;
    static unsigned phase,motion,expected=UINT_MAX,common,specials,returns,changed[2],rotation_mode,wing,tumble;
    static float expected_rotation;
    static uint64_t baseline;static SmbFighterModel baseline_model;
    const char* reason="unknown";unsigned actual=UINT_MAX;
    if(phase==3)return;
    HSD_GObj* object=Player_GetEntity(6);
    if(expected!=UINT_MAX) {
        Fighter* fp=object?object->user_data:NULL;
        smb_remote_test_pure(fp);
        actual=fp->motion_id;if(actual!=expected){reason="motion_changed";goto failed;}
        if(rotation_mode) {
            if(fabsf(ftPartGetRotX(fp,ftParts_GetBoneIndex(fp,FtPart_XRotN))-expected_rotation)>.0001f){reason="flight_rotation_changed";goto failed;}
            if(rotation_mode==1)wing=1;else tumble=1;
        }
    }
    SmbRemote state={0};state.version=SMB_MAGIC;state.facing[0]=1;
    state.position[0]=output->x+30;state.position[1]=output->y+8;state.position[2]=output->z-14;
    state.character=phase==0?2:phase==1?5:38;state.kind=phase==0?Ft_Kind_Fox:phase==1?Ft_Kind_Koopa:37;
    if(!phase) {
        state.motion=motion++;state.frame=state.motion%5;
        if(motion==ftCo_MS_Count){phase=1;motion=0;}
        common++;
    } else {
        unsigned total=smb_native_motion_count(state.kind)-ftCo_MS_Count;
        if(!total||total>64)goto failed;
        if(motion==0)state.motion=ftCo_MS_Wait;
        else if(motion%2){state.motion=ftCo_MS_Count+(motion-1)/2;state.frame=8;specials++;}
        else {state.motion=ftCo_MS_Wait;state.model=baseline_model;returns++;}
        motion++;
        if(motion>=total*2+1){phase++;motion=0;}
    }
    rotation_mode=0;
    if(state.kind==Ft_Kind_Fox&&state.motion==ftCo_MS_Wait){state.caps|=4;rotation_mode=1;expected_rotation=1.5707963268f;}
    else if(state.motion>=ftCo_MS_DamageFlyHi&&state.motion<=ftCo_MS_DamageFlyRoll){state.velocity[0]=6;state.velocity[1]=4;rotation_mode=2;expected_rotation=atan2f(6,4);}
    if(smb_embed_remote(3,&state)){reason="pose_rejected";goto failed;}
    object=Player_GetEntity(6);Fighter* fp=object?object->user_data:NULL;smb_remote_test_pure(fp);
    actual=fp->motion_id;
    /* Emulate a source-side entry callback selecting a real alternate DAT
     * model. The receiver imports that authored selection without replaying
     * the callback, then restores the saved owner's Wait appearance. */
    if(state.kind!=Ft_Kind_Fox&&state.motion==ftCo_MS_Count&&fp->x5AC.xC[0]) {
        int selected=0;
        for(unsigned n=0;n<fp->x5AC.model_num;n++)if(fp->x5AC.xC[0][n].x0>1) {
            int alternate=baseline_model.part[n]==0?1:0;ftParts_80074B0C(object,n,alternate);selected=1;break;
        }
        if(!selected){reason="no_authored_alternate";goto failed;}
        smb_native_remote_model(fp,&state.model);
        if(smb_embed_remote(3,&state)){reason="authored_form_rejected";goto failed;}
    }
    uint64_t models=smb_remote_test_models(fp);if(!models)goto failed;
    unsigned form=state.kind==Ft_Kind_Koopa?0:1;
    if(state.kind!=Ft_Kind_Fox) {
        if(state.motion==ftCo_MS_Wait) {
            if(motion==1){baseline=models;smb_native_remote_model(fp,&baseline_model);}
            else if(models!=baseline){reason="default_model_mismatch";goto failed;}
        } else if(models!=baseline)changed[form]=1;
    }
    expected=state.motion;
    if(phase==3) {
        if(common!=ftCo_MS_Count||!changed[0]||!changed[1]||!wing||!tumble)goto failed;
        smb_embed_remote(3,NULL);expected=UINT_MAX;
        fprintf(stderr,"SMB REMOTE STATES PASS common=%u specials=%u default_returns=%u bowser_forms=1 sonic_forms=1 wing_orientation=1 tumble_orientation=1 callbacks=0 native_articles=0\n",common,specials,returns);
    }
    return;
failed:
    fprintf(stderr,"SMB REMOTE STATES FAIL reason=%s step=%u phase=%u cursor=%u expected=%u actual=%u common=%u specials=%u returns=%u forms=%u,%u\n",reason,step,phase,motion,expected,actual,common,specials,returns,changed[0],changed[1]);exit(2);
}
static void smb_remote_test_capture_path(const char* path) {
#ifdef _WIN32
    _putenv_s("SMB_SCENE_CAPTURE",path?path:"");
#else
    if(path)setenv("SMB_SCENE_CAPTURE",path,1);else unsetenv("SMB_SCENE_CAPTURE");
#endif
}
void smb_native_remote_test(const SmbOutput* output) {
    if(!getenv("SMB_SMOKE")||!getenv("SMB_TEST_REMOTE_ARTICLES"))return;
    static unsigned step,arrow,bomb,partner,cleared,cache_valid,shield,egg,capture_ready,animated;
    static SmbItem cache;static char capture_path[4096];step++;
    if(step==1) {
        const char* path=getenv("SMB_SCENE_CAPTURE");
        if(path&&strlen(path)<sizeof capture_path){strcpy(capture_path,path);smb_remote_test_capture_path(NULL);}
    }
    smb_remote_test_states(output,step);
    if(step>460)return;
    SmbRemote link={0};link.version=SMB_MAGIC;link.character=output->character;link.costume=output->costume;
    link.kind=output->kind;link.motion=output->motion;link.grounded=output->grounded;link.frame=output->animation;
    link.percent=output->percent;link.position[0]=output->x+18;link.position[1]=output->y;link.position[2]=output->z-12;
    memcpy(link.facing,output->facing,sizeof link.facing);
    link.velocity[0]=output->vx;link.velocity[1]=output->vy;link.velocity[2]=output->vz;
    if(step>=80&&step<110){link.caps|=8;link.motion=ftCo_MS_Guard;link.frame=-1;}
    SmbItem items[SMB_ITEMS];unsigned count=0;
    for(unsigned i=0;i<output->item_count;i++)if(output->item[i].owner==0) {
        items[count]=output->item[i];items[count].position[0]+=18;items[count].position[2]-=12;
        count++;
    }
    if(step<420) {
        if(smb_embed_remote(1,&link)||smb_embed_remote_articles(1,items,count))goto failed;
        if(step>=80&&step<110) {
            if(!smb_remote_shield_live(remote_shields[4][0],Player_GetEntity(4)))goto failed;
            if(((Fighter*)Player_GetEntity(4)->user_data)->cur_anim_frame<0)goto failed;
            shield=1;
        } else if(remote_shield_mode[4][0]||remote_shields[4][0])goto failed;
        if(step>=160&&step<190) {
            SmbRemote yoshi={0};yoshi.version=SMB_MAGIC;yoshi.character=17;yoshi.kind=Ft_Kind_Yoshi;
            yoshi.motion=ftYs_MS_GuardHold;yoshi.caps=8;yoshi.facing[0]=1;
            yoshi.position[0]=output->x+28;yoshi.position[1]=output->y;yoshi.position[2]=output->z;
            if(smb_embed_remote(3,&yoshi))goto failed;
            Fighter* ys=Player_GetEntity(6)->user_data;if(ys->x5F4_arr[0].idx!=1||!remote_shield_mode[6][0])goto failed;
            egg=1;
        } else if(step==190) {
            smb_embed_remote(3,NULL);if(remote_shield_mode[6][0]||remote_shields[6][0])goto failed;
        }
        for(unsigned i=0;i<count;i++)if(items[i].flags&6) {
            SmbRemoteArticle* clone=NULL;
            for(unsigned n=0;n<SMB_ITEMS;n++)if(remote_articles[4][n].object&&remote_articles[4][n].id==items[i].id){clone=&remote_articles[4][n];break;}
            if(!clone||!clone->object->hsd_obj||(!(items[i].model_flags&4)&&!clone->object->render_cb)||clone->object->proc)goto failed;
            SmbItem shown={0};unsigned joints=0,draws=0,visible=0;
            smb_native_item_visibility(clone->object->hsd_obj,&shown,&joints,&draws,&visible);
            if(memcmp(shown.joint_hidden,items[i].joint_hidden,sizeof shown.joint_hidden)||memcmp(shown.dobj_hidden,items[i].dobj_hidden,sizeof shown.dobj_hidden))goto failed;
            if(items[i].animation_index!=UINT32_MAX&&clone->animation==items[i].animation_index+1)animated=1;
            if(items[i].flags&4)arrow=1;if(items[i].flags&2)bomb=1;cache=items[i];cache_valid=1;
        }
        SmbRemote climbers={0};climbers.version=SMB_MAGIC;climbers.character=14;climbers.kind=Ft_Kind_Popo;
        climbers.motion=ftCo_MS_Wait;climbers.frame=step%30;climbers.percent=24;climbers.facing[0]=1;
        climbers.position[0]=output->x-22;climbers.position[1]=output->y;climbers.position[2]=output->z-10;
        climbers.partner_active=1;climbers.partner.kind=Ft_Kind_Nana;climbers.partner.motion=ftCo_MS_Fall;
        climbers.partner.frame=step%20;climbers.partner.percent=67;climbers.partner.facing[2]=1;
        climbers.partner.position[0]=output->x-30;climbers.partner.position[1]=output->y+7;climbers.partner.position[2]=output->z-16;
        if(smb_embed_remote(2,&climbers))goto failed;
        HSD_GObj* nana=Player_GetEntityAtIndex(5,1);if(!nana)goto failed;
        Fighter* fp=nana->user_data;
        if(fp->kind!=Ft_Kind_Nana||fp->motion_id!=ftCo_MS_Fall||fabsf(fp->cur_pos.x-climbers.partner.position[0])>.001f||
           fabsf(fp->cur_pos.y-climbers.partner.position[1])>.001f||fabsf(fp->dmg.x1830_percent-67)>.001f)goto failed;
        partner=1;
        /* The host's existing one-shot scene capture normally occurs at90
         * frames. Delay its path until the duplicated bomb is present, while
         * both remote fighters are still alive and before cleanup. */
        if(!capture_ready&&capture_path[0]&&bomb&&count&&step>=200&&step<300) {
            smb_remote_test_capture_path(capture_path);capture_ready=1;
            fprintf(stderr,"SMB REMOTE CAPTURE READY native_step=%u bomb=1 duplicates=%u\n",step,count);
        }
        if(step==120||step==300) {
            if(!cache_valid)goto failed;
            smb_embed_remote_articles(1,&cache,1);
            unsigned alive=0;for(unsigned i=0;i<SMB_ITEMS;i++)alive+=remote_articles[4][i].object!=NULL;
            if(!alive)goto failed;
            if(step==120)smb_embed_remote(1,NULL);else smb_native_reset_world_items();
            for(unsigned i=0;i<SMB_ITEMS;i++)if(remote_articles[4][i].object)goto failed;
            cleared++;fprintf(stderr,"SMB REMOTE TEST lifecycle=%s articles_cleared=1\n",step==120?"disconnect":"world_refresh");
        }
    } else if(step==420) {
        smb_embed_remote(1,NULL);smb_embed_remote(2,NULL);smb_embed_remote(3,NULL);
        for(unsigned seat=4;seat<=5;seat++)for(unsigned i=0;i<SMB_ITEMS;i++)if(remote_articles[seat][i].object)goto failed;
        if(!arrow||!bomb||!partner||!shield||!egg||!animated||cleared!=2||(capture_path[0]&&!capture_ready))goto failed;
        fprintf(stderr,"SMB REMOTE TEST PASS arrow=1 bomb=1 independent_nana=1 disconnect=1 world_refresh=1 shield=1 yoshi_egg=1 authored_animation=1 visibility=1\n");
    }
    return;
failed:
    fprintf(stderr,"SMB REMOTE TEST FAIL step=%u arrow=%u bomb=%u partner=%u cleared=%u\n",step,arrow,bomb,partner,cleared);exit(2);
}
#include "melee_water_test.inc"
