#include <melee/it/types.h>
#include <melee/it/item.h>
#include <melee/it/kinds/itlinkarrow.h>
#include <melee/it/kinds/itlinkbomb.h>
#include <melee/it/kinds/itlinkhookshot.h>
#include <melee/it/kinds/itsamusgrapple.h>
#include <melee/ft/kinds/ftLink/ftlinkspecialn.h>
static struct {Item* item;HSD_GObj* object;unsigned id;} smb_items[128];
static unsigned smb_next_item=1,smb_attack[2]={1,1},smb_previous_hits[2],smb_previous_motion[2];
extern void m3d_ItemInit_original(Item*,HSD_GObj*);
extern void m3d_ItemForget_original(Item*);
void m3d_ItemInit(Item* item,HSD_GObj* parent) {
    m3d_ItemInit_original(item,parent);
    for(unsigned i=0;i<128;i++)if(!smb_items[i].item){smb_items[i].item=item;smb_items[i].object=item->entity;smb_items[i].id=smb_next_item++;return;}
    /* A crowded fight can exhaust the export registry. Native items still
     * simulate normally; do not terminate a game over an optional snapshot. */
    fprintf(stderr,"SMB ITEM REGISTRY full=128 snapshot_omitted=1\n");
}
void smb_native_item_retiring(void* raw) {
    HSD_GObj* retiring=raw;if(!retiring)return;
    /* A world refresh can retire a held arrow without retail's owner callback.
     * Clear exact owner references before the GObj slot can be reused. Otherwise
     * Link's next hitstun/landing cleanup attempts to destroy a freed arrow. */
    for(unsigned seat=0;seat<GM_MAX_PLAYERS;seat++)for(unsigned sub=0;sub<2;sub++) {
        HSD_GObj* object=Player_GetEntityAtIndex(seat,sub);if(!object)continue;
        Fighter* fp=object->user_data;if(!fp||(fp->kind!=Ft_Kind_Link&&fp->kind!=Ft_Kind_CLink))continue;
        if(fp->u.lk.arrow_gobj==retiring) {
            ftLk_SpecialN_UnsetArrow(object);
            fprintf(stderr,"SMB ITEM OWNER RELEASE seat=%u kind=%d arrow=1\n",seat,fp->kind);
        }
        if(fp->u.lk.x14==retiring) {
            ftLk_SpecialN_UnsetFv14(object);
            fprintf(stderr,"SMB ITEM OWNER RELEASE seat=%u kind=%d bow=1\n",seat,fp->kind);
        }
    }
}
void m3d_ItemForget(Item* item) {
    smb_native_item_retiring(item->entity);
    for(unsigned i=0;i<128;i++)if(smb_items[i].item==item)memset(&smb_items[i],0,sizeof smb_items[i]);
    m3d_ItemForget_original(item);
}
static unsigned smb_item_flags(Item* item) {
    unsigned flags=1;
    if(item->kind==It_Kind_Link_Bomb||item->kind==It_Kind_CLink_Bomb)flags|=2;
    if(item->kind==It_Kind_Link_Arrow||item->kind==It_Kind_CLink_Arrow)flags|=4;
    if(item->xDC8_word.flags.x13)flags|=8;
    return flags;
}
static void smb_hit(SmbOutput* out,HitCapsule* hit,unsigned source,unsigned kind,unsigned flags,float scale,unsigned index,unsigned owner,unsigned owner_sub) {
    if(!hit->state)return;
    if(out->hit_count>=SMB_HITS)return;
    SmbHit* target=&out->hit[out->hit_count++];
    target->source=source;target->kind=kind;target->flags=flags;target->index=index;
    target->radius=hit->scale*(hit->x43_b1?1:scale);target->damage=hit->damage;
    target->kb_angle=hit->kb_angle;target->growth=hit->x24;target->fixed=hit->x28;target->base=hit->x2C;
    target->element=hit->element;target->severity=hit->sfx_severity;target->sound=hit->sfx_kind;
    target->owner=owner;target->owner_sub=owner_sub;target->rehit=hit->x40_b4;
    memcpy(target->start,&hit->x58,12);memcpy(target->end,&hit->x4C,12);
}
static void smb_collect_items(SmbOutput* out,Fighter* fighter) {
    HSD_GObj* partner=smb_partner();
    Fighter* fighters[]={fighter,partner?partner->user_data:NULL};
    for(unsigned n=0;n<2;n++)if(fighters[n]) {
        Fighter* fp=fighters[n];unsigned hits=0;
        for(unsigned i=0;i<4;i++)if(fp->x914[i].state)hits|=1u<<i;
        if(hits&&(hits!=smb_previous_hits[n]||fp->motion_id!=smb_previous_motion[n]))smb_attack[n]++;
        unsigned move=fp->x2068_attackID;
        int down=move==FtMoveId_AttackLw3||move==FtMoveId_AttackLw4||move==FtMoveId_AttackAirLw;
        if(!n&&down&&fp->motion_id>=ftCo_MS_Attack11&&fp->motion_id<=ftCo_MS_AttackAirLw)out->abilities|=SMB_DOWN_ATTACK;
        smb_previous_hits[n]=hits;smb_previous_motion[n]=fp->motion_id;
        for(unsigned i=0;i<4;i++) {
            HitCapsule* hit=&fp->x914[i];unsigned flags=(hit->element==HitElement_Catch?16:0)|(down?32:0);
            if((flags&16)&&hit->state)out->grab_active=1;
            smb_hit(out,hit,0x80000000u|(n<<30)|smb_attack[n],fp->kind,flags,fp->x34_scale.y,i,fp->player_idx,fp->is_sub_fighter?1:0);
        }
    }
    for(unsigned i=0;i<128;i++)if(smb_items[i].item) {
        Item* item=smb_items[i].item;unsigned flags=smb_item_flags(item);
        if(out->item_count>=SMB_ITEMS)break;
        SmbItem* dest=&out->item[out->item_count++];dest->id=smb_items[i].id;dest->kind=item->kind;dest->motion=item->msid;dest->flags=flags;
        memcpy(dest->position,&item->pos,12);memcpy(dest->velocity,&item->x40_vel,12);
        dest->owner=UINT32_MAX;dest->owner_sub=0;
        for(unsigned seat=0;seat<GM_MAX_PLAYERS;seat++)for(unsigned sub=0;sub<2;sub++)
            if(item->owner&&item->owner==Player_GetEntityAtIndex(seat,sub)){dest->owner=seat;dest->owner_sub=sub;}
        extern void smb_native_item_animation(Item*,SmbItem*);
        smb_native_item_animation(item,dest);
        HSD_JObj* root=smb_items[i].object->hsd_obj;
        if(root) {
            memcpy(dest->rotation,&root->rotate,sizeof dest->rotation);memcpy(dest->scale,&root->scale,sizeof dest->scale);
            dest->model_flags|=((root->flags&JOBJ_HIDDEN)?1:0)|((root->flags&JOBJ_USE_QUATERNION)?2:0);
        }
        for(unsigned h=0;h<4;h++)smb_hit(out,&item->x5D4_hitboxes[h].hit,dest->id,item->kind,flags|(item->x5D4_hitboxes[h].hit.element==HitElement_Catch?16:0),item->scl,h,dest->owner,dest->owner_sub);
    }
    if(getenv("SMB_SCENARIO")&&out->hit_count) {
        static unsigned traces;
        if(traces++<20)for(unsigned i=0;i<out->hit_count;i++){SmbHit* h=&out->hit[i];fprintf(stderr,"SMB CAPSULE source=%u flags=%u damage=%.2f radius=%.2f a=%.2f,%.2f,%.2f b=%.2f,%.2f,%.2f\n",h->source,h->flags,h->damage,h->radius,h->start[0],h->start[1],h->start[2],h->end[0],h->end[1],h->end[2]);}
    }
    static unsigned last=UINT32_MAX;
    if(out->item_count!=last){last=out->item_count;fprintf(stderr,"SMB ITEMS tick=%u count=%u hits=%u\n",ticks,out->item_count,out->hit_count);
        for(unsigned i=0;i<out->item_count;i++)fprintf(stderr,"SMB ITEM id=%u kind=%u flags=%u motion=%u x=%.3f y=%.3f z=%.3f\n",out->item[i].id,out->item[i].kind,out->item[i].flags,out->item[i].motion,out->item[i].position[0],out->item[i].position[1],out->item[i].position[2]);
    }
}
int smb_embed_contact(unsigned id) {
    for(unsigned i=0;i<128;i++)if(smb_items[i].item&&smb_items[i].id==id) {
        Item* item=smb_items[i].item;HSD_GObj* object=smb_items[i].object;
        unsigned flags=smb_item_flags(item);
        if(flags&4){itLinkArrow_Logic98_DmgDealt(object);Item_8026A8EC(object);}
        else if(flags&2){if(item->msid!=5)it_8029F69C(object);}
        else {extern int smb_native_item_contact(HSD_GObj*);return smb_native_item_contact(object);}
        return 0;
    }
    return -1;
}
static void smb_destroy_world_item(HSD_GObj* object) {
    Item* item=object->user_data;
    /* Tethers own chain GObjs and a fighter-side reference. Their native
     * teardown clears both; generic item deletion leaves a dangling callback. */
    if(item->kind==It_Kind_Samus_GBeam) {
        fprintf(stderr,"SMB WORLD ITEM REMOVE kind=%d native_tether=1\n",item->kind);
        it_802B7B84(object);
    } else if(item->kind==It_Kind_Link_HShot||item->kind==It_Kind_CLink_HShot) {
        fprintf(stderr,"SMB WORLD ITEM REMOVE kind=%d native_tether=1\n",item->kind);
        it_802A2B10(object);
    } else Item_8026A8EC(object);
}
void smb_native_reset_world_items(void) {
    extern void smb_native_reset_remote_articles(void);smb_native_reset_remote_articles();
    for(unsigned i=0;i<128;i++)if(smb_items[i].object)smb_destroy_world_item(smb_items[i].object);
}
void smb_native_clear_npc_items(unsigned slot) {
    for(unsigned i=0;i<128;i++)if(smb_items[i].item) {
        HSD_GObj* owner=smb_items[i].item->owner;
        if(owner&&(owner==Player_GetEntityAtIndex(slot,0)||owner==Player_GetEntityAtIndex(slot,1)))smb_destroy_world_item(smb_items[i].object);
    }
}

/* World enemies are not Melee fighters, so their hits cannot enter Melee's
 * normal fighter-vs-fighter presentation callback. Reuse the source capsule's
 * real hit-sound data and the common native hit-spark routine at world contact. */
#include <melee/ft/ftcoll.h>
#include <melee/lb/lbcollision.h>
int smb_embed_impact(unsigned source,unsigned index,const float* position) {
    if(!ready||index>=4||!position)return -1;
    HSD_GObj* owner=Player_GetEntity(0);HitCapsule* hit=NULL;
    if(source&0x80000000u) {
        owner=Player_GetEntityAtIndex(0,(source>>30)&1);
        if(owner&&!((Fighter*)owner->user_data)->is_sleeping)hit=&((Fighter*)owner->user_data)->x914[index];
    } else for(unsigned i=0;i<128;i++)if(smb_items[i].item&&smb_items[i].id==source) {
        hit=&smb_items[i].item->x5D4_hitboxes[index].hit;break;
    }
    if(!hit||!owner)return -1;
    Vec3 point={position[0],position[1],position[2]};
    HSD_GObj* previous=HSD_GObj_804D7814;HSD_GObj_804D7814=owner;
    ftColl_80078538(owner,&point,(unsigned)hit->sfx_severity,hit->damage,50.f);
    int sound=lbColl_80005BB0(hit,0x72);
    HSD_GObj_804D7814=previous;
    fprintf(stderr,"SMB WORLD IMPACT source=%u index=%u severity=%d sound_kind=%d audio_handle=%d native_spark=1\n",source,index,hit->sfx_severity,hit->sfx_kind,sound);
    return 0;
}

/* Once SM64 takes ownership of a carried object, close a tether and retain
 * the fighter's native holding pose. Simulation resets normally on return. */
#include <melee/ft/fighter.h>
#include <melee/it/kinds/itlinkhookshot.h>
#include <melee/it/kinds/itsamusgrapple.h>
int smb_embed_hold(void) {
    HSD_GObj* object=Player_GetEntity(0);if(!object)return -1;
    Fighter* fp=object->user_data;
    /* Native teardown retires chain links, owner references and accessory
     * callbacks together. Deleting only the item leaves a dangling tether. */
    if(fp->kind==Ft_Kind_Link||fp->kind==Ft_Kind_CLink)it_802A7AAC(object);
    else if(fp->kind==Ft_Kind_Samus)it_802BAC3C(object);
    Fighter_ChangeMotionState(object,ftCo_MS_CatchWait,Ft_MF_None,0,1,0,NULL);
    fprintf(stderr,"SMB HOLD POSE native_catch_wait=1\n");return 0;
}
