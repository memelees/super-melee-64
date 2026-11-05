/* Native DAT article presentation only. No remote article owns Item physics,
 * collision, damage, item callbacks or fighter accessory references. */
#include <melee/it/types.h>
#include <melee/it/item.h>
#include <melee/it/kinds/types.h>
#include <melee/it/it_3F14.h>
#include <pc/m3d_akaneia.h>
#include <pc/m3d_items.h>
#include <m3d_vm.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/gobjobject.h>
#include <sysdolphin/baselib/gobjgxlink.h>
#include <sysdolphin/baselib/gobjplink.h>
/* Bounds come from the pinned native ItemStateTable definitions. Compare their
 * actual pointers, rather than trusting a network animation index. Bombs have
 * one additional flashing material state selected outside their motion table. */
extern ItemStateTable it_803F6C88[];
extern ItemStateTable it_803F8BD8[];
extern ItemStateTable it_803F9470[];
extern ItemStateTable it_803F8DE8[];
extern ItemStateTable it_803F57B0[];
extern ItemStateTable it_803F54D8[];
extern ItemStateTable it_803F5850[];
extern ItemStateTable it_803F5468[];
extern ItemStateTable it_803F80F8[];
extern ItemStateTable it_803F7A98[];
extern ItemStateTable it_803F7AC8[];
extern ItemStateTable it_803F76A8[];
extern ItemStateTable it_803F7668[];
extern ItemStateTable it_803F76B8[];
extern ItemStateTable it_803F7A28[];
extern ItemStateTable it_803F93C8[];
extern ItemStateTable it_803F93A8[];
extern ItemStateTable it_803F6130[];
extern ItemStateTable it_803F55D0[];
extern ItemStateTable it_803F75D0[];
extern ItemStateTable it_803F5988[];
extern ItemStateTable it_803F7DC8[];
extern ItemStateTable it_803F6728[];
extern ItemStateTable it_803F6090[];
extern ItemStateTable it_803F66C8[];
extern ItemStateTable it_803F7C10[];
extern ItemStateTable ItemStateTable_Flipper[];
extern ItemStateTable it_803F5DB0[];
extern ItemStateTable it_803F6CA8[];
extern ItemStateTable it_803F6818[];
extern ItemStateTable it_803F67D0[];
extern ItemStateTable it_803F5D28[];
extern ItemStateTable it_803F7C70[];
extern ItemStateTable it_803F82F0[];
extern ItemStateTable it_803F7938[];
extern ItemStateTable it_803F79E0[];
extern ItemStateTable it_803F78E8[];
extern ItemStateTable it_803F7898[];
extern ItemStateTable it_803F7968[];
extern ItemStateTable it_803F78D8[];
extern ItemStateTable it_803F79A0[];
extern ItemStateTable it_803F78F8[];
extern ItemStateTable it_803F79C0[];
extern ItemStateTable it_803F7918[];
extern ItemStateTable it_803F8FD0[];
extern ItemStateTable it_803F5BA8[];
extern ItemStateTable it_803F6150[];
extern ItemStateTable it_803F6640[];
extern ItemStateTable it_803F5FF0[];
extern ItemStateTable it_803F7CF8[];
extern ItemStateTable it_803F56C8[];
extern ItemStateTable it_803F83F0[];
extern ItemStateTable it_803F8270[];
extern ItemStateTable it_803F82A0[];
extern ItemStateTable it_803F8128[];
extern ItemStateTable it_803F8180[];
extern ItemStateTable it_803F7F58[];
extern ItemStateTable it_803F7FB8[];
extern ItemStateTable it_803F7AD8[];
extern ItemStateTable it_803F7B08[];
extern ItemStateTable it_803F7B48[];
extern ItemStateTable it_803F6110[];
extern ItemStateTable it_803F9450[];
extern ItemStateTable it_803F6798[];
extern ItemStateTable it_803F7958[];
extern ItemStateTable it_803F6C98[];
extern ItemStateTable it_803F9490[];
extern ItemStateTable it_803F7E28[];
extern ItemStateTable it_803F8940[];
extern ItemStateTable ItemStateTable_KoopaFlame[];
extern ItemStateTable it_803F59F8[];
extern ItemStateTable it_803F90D0[];
extern ItemStateTable it_803F9328[];
extern ItemStateTable it_803F8EB0[];
extern ItemStateTable it_803F5CC8[];
extern ItemStateTable it_803F6630[];
extern ItemStateTable it_803F64F8[];
extern ItemStateTable it_803F8468[];
extern ItemStateTable it_803F6A28[];
extern ItemStateTable it_803F6888[];
extern ItemStateTable it_803F6920[];
extern ItemStateTable it_803F6E98[];
extern ItemStateTable it_803F6998[];
extern ItemStateTable it_803F6310[];
extern ItemStateTable it_803F6680[];
extern ItemStateTable it_803F7BC0[];
extern ItemStateTable it_803F7C00[];
extern ItemStateTable it_803F8190[];
extern ItemStateTable it_803F8200[];
extern ItemStateTable it_803F7EE8[];
extern ItemStateTable it_803F7F48[];
extern ItemStateTable it_803F75C0[];
extern ItemStateTable it_803F82B0[];
extern ItemStateTable it_803F70F8[];
extern ItemStateTable it_803F6788[];
extern ItemStateTable it_803F7E78[];
extern ItemStateTable it_803F9388[];
extern ItemStateTable it_803F9378[];
extern ItemStateTable it_803F7B58[];
extern ItemStateTable it_803F7BB0[];
extern ItemStateTable it_803F83E0[];
extern ItemStateTable it_803F6488[];
extern ItemStateTable it_803F62C0[];
extern ItemStateTable it_803F7FC8[];
extern ItemStateTable it_803F80C8[];
extern ItemStateTable it_803F7750[];
extern ItemStateTable it_803F7760[];
extern ItemStateTable ItemStateTable_MsBomb[];
extern ItemStateTable it_803F6C68[];
extern ItemStateTable it_803F6B28[];
extern ItemStateTable it_803F6B60[];
extern ItemStateTable it_803F6B70[];
extern ItemStateTable it_803F6F40[];
extern ItemStateTable it_803F6BC8[];
extern ItemStateTable it_803F6C08[];
extern ItemStateTable it_803F7558[];
extern ItemStateTable it_803F85F0[];
extern ItemStateTable it_803F8B48[];
extern ItemStateTable it_803F8E90[];
extern ItemStateTable it_803F8320[];
extern ItemStateTable it_803F89C8[];
extern ItemStateTable it_803F5AB0[];
extern ItemStateTable it_803F88C0[];
extern ItemStateTable it_803F7488[];
extern ItemStateTable it_803F74F8[];
extern ItemStateTable it_803F7528[];
extern ItemStateTable it_803F7548[];
extern ItemStateTable it_803F74A8[];
extern ItemStateTable it_803F70C8[];
extern ItemStateTable it_803F71D8[];
extern ItemStateTable it_803F7190[];
extern ItemStateTable it_803F7FF8[];
extern ItemStateTable it_803F8250[];
extern ItemStateTable it_803F6270[];
extern ItemStateTable it_803F7DE8[];
extern ItemStateTable it_803F5C48[];
extern ItemStateTable it_803F7220[];
extern ItemStateTable it_803F7288[];
extern ItemStateTable it_803F73A8[];
extern ItemStateTable it_803F7340[];
extern ItemStateTable it_803F6220[];
extern ItemStateTable it_803F7438[];
extern ItemStateTable it_803F70A8[];
extern ItemStateTable it_803F6F50[];
extern ItemStateTable it_803F70B8[];
extern ItemStateTable it_803F7CA0[];
extern ItemStateTable it_803F6370[];
extern ItemStateTable it_803F5F40[];
extern ItemStateTable it_803F6568[];
extern ItemStateTable it_803F57A0[];
extern ItemStateTable it_803F5F90[];
extern ItemStateTable it_803F6530[];
extern ItemStateTable it_803F7E08[];
extern ItemStateTable it_803F5800[];
extern ItemStateTable it_803F58E0[];
extern ItemStateTable it_803F63C0[];
extern ItemStateTable it_803F7C40[];
extern ItemStateTable it_803F9000[];
extern ItemStateTable it_803F8058[];
extern ItemStateTable it_803F5740[];
extern ItemStateTable it_803F9260[];
extern ItemStateTable it_803F7A48[];
extern ItemStateTable it_803F7D60[];
extern ItemStateTable it_803F91D0[];
extern ItemStateTable it_803F8A88[];
extern ItemStateTable it_803F61B0[];
extern ItemStateTable it_803F8C8C[];
extern ItemStateTable it_803F94A0[];
extern ItemStateTable it_803F7118[];
extern ItemStateTable it_803F7158[];
extern ItemStateTable it_803F9480[];
extern ItemStateTable ItemStateTable_ZeldaDinFire[];
extern ItemStateTable it_803F7740[];
extern ItemStateTable it_803F86C8[];
extern ItemStateTable it_803F87F0[];
static const struct {const ItemStateTable* states;int max_anim;} smb_remote_item_states[]={
    {it_803F6C88,0},
    {it_803F8BD8,0},
    {it_803F9470,0},
    {it_803F8DE8,5},
    {it_803F57B0,0},
    {it_803F54D8,6},
    {it_803F5850,2},
    {it_803F5468,1},
    {it_803F80F8,2},
    {it_803F7A98,1},
    {it_803F7AC8,0},
    {it_803F76A8,0},
    {it_803F7668,0},
    {it_803F76B8,-1},
    {it_803F7A28,1},
    {it_803F93C8,-1},
    {it_803F93A8,1},
    {it_803F6130,0},
    {it_803F55D0,3},
    {it_803F75D0,5},
    {it_803F5988,1},
    {it_803F7DC8,0},
    {it_803F6728,0},
    {it_803F6090,4},
    {it_803F66C8,0},
    {it_803F7C10,2},
    {ItemStateTable_Flipper,2},
    {it_803F5DB0,-1},
    {it_803F6CA8,8},
    {it_803F6818,2},
    {it_803F67D0,1},
    {it_803F5D28,1},
    {it_803F7C70,2},
    {it_803F82F0,1},
    {it_803F7938,1},
    {it_803F79E0,1},
    {it_803F78E8,0},
    {it_803F7898,3},
    {it_803F7968,0},
    {it_803F78D8,0},
    {it_803F79A0,1},
    {it_803F78F8,1},
    {it_803F79C0,0},
    {it_803F7918,1},
    {it_803F8FD0,1},
    {it_803F5BA8,0},
    {it_803F6150,1},
    {it_803F6640,0},
    {it_803F5FF0,4},
    {it_803F7CF8,2},
    {it_803F56C8,-1},
    {it_803F83F0,2},
    {it_803F8270,1},
    {it_803F82A0,0},
    {it_803F8128,1},
    {it_803F8180,0},
    {it_803F7F58,5},
    {it_803F7FB8,0},
    {it_803F7AD8,1},
    {it_803F7B08,2},
    {it_803F7B48,0},
    {it_803F6110,0},
    {it_803F9450,-1},
    {it_803F6798,0},
    {it_803F7958,-1},
    {it_803F6C98,0},
    {it_803F9490,-1},
    {it_803F7E28,4},
    {it_803F8940,3},
    {ItemStateTable_KoopaFlame,0},
    {it_803F59F8,4},
    {it_803F90D0,3},
    {it_803F9328,2},
    {it_803F8EB0,9},
    {it_803F5CC8,0},
    {it_803F6630,0},
    {it_803F64F8,0},
    {it_803F8468,9},
    {it_803F6A28,0},
    {it_803F6888,2},
    {it_803F6920,2},
    {it_803F6E98,5},
    {it_803F6998,-1},
    {it_803F6310,1},
    {it_803F6680,1},
    {it_803F7BC0,3},
    {it_803F7C00,0},
    {it_803F8190,3},
    {it_803F8200,-1},
    {it_803F7EE8,5},
    {it_803F7F48,0},
    {it_803F75C0,0},
    {it_803F82B0,0},
    {it_803F70F8,1},
    {it_803F6788,0},
    {it_803F7E78,6},
    {it_803F9388,1},
    {it_803F9378,0},
    {it_803F7B58,1},
    {it_803F7BB0,0},
    {it_803F83E0,-1},
    {it_803F6488,1},
    {it_803F62C0,-1},
    {it_803F7FC8,1},
    {it_803F80C8,2},
    {it_803F7750,0},
    {it_803F7760,9},
    {ItemStateTable_MsBomb,2},
    {it_803F6C68,0},
    {it_803F6B28,0},
    {it_803F6B60,0},
    {it_803F6B70,2},
    {it_803F6F40,0},
    {it_803F6BC8,0},
    {it_803F6C08,0},
    {it_803F7558,-1},
    {it_803F85F0,5},
    {it_803F8B48,4},
    {it_803F8E90,0},
    {it_803F8320,4},
    {it_803F89C8,6},
    {it_803F5AB0,-1},
    {it_803F88C0,4},
    {it_803F7488,1},
    {it_803F74F8,1},
    {it_803F7528,1},
    {it_803F7548,0},
    {it_803F74A8,2},
    {it_803F70C8,0},
    {it_803F71D8,0},
    {it_803F7190,1},
    {it_803F7FF8,5},
    {it_803F8250,1},
    {it_803F6270,-1},
    {it_803F7DE8,0},
    {it_803F5C48,0},
    {it_803F7220,1},
    {it_803F7288,8},
    {it_803F73A8,-1},
    {it_803F7340,3},
    {it_803F6220,0},
    {it_803F7438,-1},
    {it_803F70A8,-1},
    {it_803F6F50,4},
    {it_803F70B8,0},
    {it_803F7CA0,1},
    {it_803F6370,0},
    {it_803F5F40,0},
    {it_803F6568,9},
    {it_803F57A0,0},
    {it_803F5F90,0},
    {it_803F6530,0},
    {it_803F7E08,0},
    {it_803F5800,1},
    {it_803F58E0,3},
    {it_803F63C0,3},
    {it_803F7C40,2},
    {it_803F9000,6},
    {it_803F8058,6},
    {it_803F5740,-1},
    {it_803F9260,9},
    {it_803F7A48,2},
    {it_803F7D60,1},
    {it_803F91D0,2},
    {it_803F8A88,7},
    {it_803F61B0,2},
    {it_803F8C8C,19},
    {it_803F94A0,-1},
    {it_803F7118,1},
    {it_803F7158,0},
    {it_803F9480,-1},
    {ItemStateTable_ZeldaDinFire,1},
    {it_803F7740,0},
    {it_803F86C8,3},
    {it_803F87F0,4},
};

/* Article descriptors are big-endian DAT records. This helper is compiled
 * by GCC; the coroutine's Clang translation unit must never read their slots.
 * Visibility indices use the same joint/DObj preorder on both native trees. */
static void smb_native_item_visibility(HSD_JObj* joint,SmbItem* state,unsigned* joints,unsigned* draws,unsigned* visible) {
    for(;joint;joint=joint->next) {
        unsigned index=(*joints)++;
        if(index<128&&(joint->flags&JOBJ_HIDDEN))state->joint_hidden[index/32]|=1u<<(index%32);
        for(HSD_DObj* draw=HSD_JObjGetDObj(joint);draw;draw=draw->next) {
            unsigned draw_index=(*draws)++;
            if(draw_index<128&&(draw->flags&DOBJ_HIDDEN))state->dobj_hidden[draw_index/32]|=1u<<(draw_index%32);
            if(!(joint->flags&JOBJ_HIDDEN)&&!(draw->flags&DOBJ_HIDDEN))*visible=1;
        }
        if(joint->child)smb_native_item_visibility(joint->child,state,joints,draws,visible);
    }
}
void smb_native_item_animation(Item* item,SmbItem* state) {
    state->frame=item->x5CC_currentAnimFrame;state->animation_index=UINT32_MAX;state->model_flags=0;
    memset(state->joint_hidden,0,sizeof state->joint_hidden);memset(state->dobj_hidden,0,sizeof state->dobj_hidden);
    if(item->anim_id!=-1&&item->xC4_article_data&&item->xC4_article_data->xC_itemStates&&item->xD0_itemStateDesc) {
        ItemStateDesc* start=DP(ItemStateDesc,item->xC4_article_data->xC_itemStates);
        uintptr_t delta=(uintptr_t)item->xD0_itemStateDesc-(uintptr_t)start;
        if(delta<32*sizeof(*start)&&!(delta%sizeof(*start)))state->animation_index=delta/sizeof(*start);
    }
    if(item->kind==It_Kind_Peach_Turnip) {
        unsigned variant=(unsigned)item->xDD4_itemVar.peachturnip.xDD8;
        if(variant>7)variant=0;state->model_flags|=variant<<8;
        /* The native one-shot material sampler intentionally does not update
         * Item's running animation frame. Preserve its actual face sample. */
        if(state->animation_index==0)state->frame=variant;
    }
    unsigned joints=0,draws=0,visible=0;smb_native_item_visibility(item->entity->hsd_obj,state,&joints,&draws,&visible);
    if(!visible)state->model_flags|=4;
}
typedef struct {
    HSD_GObj* object;Article* article;unsigned id,kind,animation,seen,variant;int max_anim;
} SmbRemoteArticle;
static SmbRemoteArticle remote_articles[GM_MAX_PLAYERS][SMB_ITEMS];
static void smb_native_remote_article_remove(SmbRemoteArticle* item) {
    if(item->object)HSD_GObjFree(item->object);memset(item,0,sizeof *item);
}
static int smb_remote_custom_article(unsigned kind,int* max_anim) {
    if(kind<237||kind>=301||!m3d_akaneia_enabled())return 0;
    uint32_t metadata=m3d_vm_metadata();if(!metadata)return 0;
    uint32_t fighter=m3d_vm_read32(metadata+8);if(!fighter)return 0;
    for(unsigned seat=0;seat<19;seat++) {
        HSD_GObj* owner=Player_GetEntity(seat);if(!owner)continue;
        Fighter* fp=owner->user_data;if(fp->kind<33||fp->kind>=40)continue;
        int internal=m3d_akaneia_internal(fp->kind);if(internal<0||internal>=41)continue;
        uint32_t row=m3d_vm_read32(fighter+0x4c)+internal*8;
        unsigned count=m3d_vm_read32(row);uint32_t ids=m3d_vm_read32(row+4);if(count>64||!ids)continue;
        for(unsigned index=0;index<count;index++) {
            unsigned char bytes[2];m3d_vm_copy_out(bytes,ids+index*2,2);
            if((((unsigned)bytes[0]<<8)|bytes[1])!=kind)continue;
            /* The pinned itFunction arrays omit unused metadata article rows. */
            static const unsigned module_counts[7]={2,3,3,10,1,4,1};
            if(index>=module_counts[fp->kind-33])return 0;
            unsigned states=m3d_vm_article_states(fp->kind-33,index);
            uint32_t table=m3d_vm_article(fp->kind-33,index,0);if(states>64||!table)return 0;
            *max_anim=-1;
            for(unsigned n=0;n<states;n++) {
                int animation=(int32_t)m3d_vm_read32(table+n*16);
                if(animation>=0&&animation<32&&animation>*max_anim)*max_anim=animation;
            }
            return 1;
        }
    }
    return 0;
}
static Article* smb_remote_article_lookup(unsigned kind,int* max_anim) {
    *max_anim=-1;
    /* Level and monster articles are owned by another engine. The bridge only
     * instantiates native playable-character and common item descriptors. */
    if(kind>=237) {if(!smb_remote_custom_article(kind,max_anim))return NULL;}
    else if(kind>=It_Kind_Old_Kuri||kind>=It_PKind_Start)return NULL;
    Item probe={0};HSD_GObj object={0};probe.kind=kind;object.user_data=&probe;
    Item_80267978(&object);
    Article* article=probe.xC4_article_data;
    if(!article||!article->x10_modelDesc)return NULL;
    if(kind<237)for(unsigned i=0;i<sizeof smb_remote_item_states/sizeof smb_remote_item_states[0];i++)
        if(probe.xBC_itemStateContainer==smb_remote_item_states[i].states){*max_anim=smb_remote_item_states[i].max_anim;break;}
    if(kind==It_Kind_Link_Bomb||kind==It_Kind_CLink_Bomb)*max_anim=3;
    return article;
}
static void smb_remote_article_render(HSD_GObj* object,intptr_t pass) {
    HSD_JObjDispAll(object->hsd_obj,NULL,HSD_GObj_80390EB8(pass),0);
}
static int smb_remote_article_valid(const SmbItem* item) {
    if(!item->id||item->owner_sub>1||item->kind>=301||(item->model_flags&~0x707u)||
       !isfinite(item->frame)||item->frame<0||item->frame>20000||
       (item->animation_index!=UINT32_MAX&&item->animation_index>=32))return 0;
    for(unsigned k=0;k<3;k++)if(!isfinite(item->position[k])||fabsf(item->position[k])>10000||
       !isfinite(item->scale[k])||fabsf(item->scale[k])>100)return 0;
    for(unsigned k=0;k<4;k++)if(!isfinite(item->rotation[k])||fabsf(item->rotation[k])>10000)return 0;
    return 1;
}
static void smb_remote_article_visibility(HSD_JObj* joint,const SmbItem* state,unsigned* joints,unsigned* draws) {
    for(;joint;joint=joint->next) {
        unsigned index=(*joints)++;
        if(index<128) {
            if(state->joint_hidden[index/32]&(1u<<(index%32)))HSD_JObjSetFlags(joint,JOBJ_HIDDEN);
            else HSD_JObjClearFlags(joint,JOBJ_HIDDEN);
        }
        for(HSD_DObj* draw=HSD_JObjGetDObj(joint);draw;draw=draw->next) {
            unsigned draw_index=(*draws)++;
            if(draw_index<128) {
                if(state->dobj_hidden[draw_index/32]&(1u<<(draw_index%32)))draw->flags|=DOBJ_HIDDEN;
                else draw->flags&=~DOBJ_HIDDEN;
            }
        }
        if(joint->child)smb_remote_article_visibility(joint->child,state,joints,draws);
    }
}
static void smb_remote_article_animate(SmbRemoteArticle* proxy,const SmbItem* state) {
    HSD_JObj* root=proxy->object->hsd_obj;
    unsigned animation=state->animation_index;
    if(state->kind==It_Kind_Peach_Turnip&&proxy->article->xC_itemStates) {
        unsigned variant=(state->model_flags>>8)&7;
        if(proxy->variant!=variant+1) {
            ItemStateDesc* desc=DP(ItemStateDesc,proxy->article->xC_itemStates);
            HSD_JObjRemoveAnimAll(root);
            HSD_JObjAddAnimAll(root,DP(HSD_AnimJoint,desc->x0_anim_joint),DP(HSD_MatAnimJoint,desc->x4_matanim_joint),DP(HSD_ShapeAnimJoint,desc->x8_parameters));
            HSD_JObjReqAnimAll(root,(float)variant);HSD_JObjAnimAll(root);HSD_JObjRemoveAnimAll(root);
            proxy->animation=0;proxy->variant=variant+1;
        }
    }
    if(animation!=UINT32_MAX&&animation<=(unsigned)proxy->max_anim&&proxy->max_anim>=0&&proxy->article->xC_itemStates) {
        ItemStateDesc* desc=&DP(ItemStateDesc,proxy->article->xC_itemStates)[animation];
        if(proxy->animation!=animation+1) {
            HSD_JObjRemoveAnimAll(root);
            HSD_JObjAddAnimAll(root,DP(HSD_AnimJoint,desc->x0_anim_joint),DP(HSD_MatAnimJoint,desc->x4_matanim_joint),DP(HSD_ShapeAnimJoint,desc->x8_parameters));
            proxy->animation=animation+1;
        }
        HSD_JObjReqAnimAll(root,state->frame);HSD_JObjAnimAll(root);
    } else if(proxy->animation){HSD_JObjRemoveAnimAll(root);proxy->animation=0;}
    if(state->model_flags&2)HSD_JObjSetFlags(root,JOBJ_USE_QUATERNION);else HSD_JObjClearFlags(root,JOBJ_USE_QUATERNION);
    unsigned joints=0,draws=0;smb_remote_article_visibility(root,state,&joints,&draws);
    /* A hidden parent does not suppress HSD child meshes. The exported masks
     * preserve per-node hiding; wholly hidden models also skip traversal. */
    proxy->object->render_cb=(state->model_flags&4)?NULL:smb_remote_article_render;
    Quaternion rotation;Vec3 scale,position;
    memcpy(&rotation,state->rotation,sizeof rotation);memcpy(&scale,state->scale,sizeof scale);memcpy(&position,state->position,sizeof position);
    HSD_JObjSetRotation(root,&rotation);HSD_JObjSetScale(root,&scale);HSD_JObjSetTranslate(root,&position);
}
static void smb_native_remote_articles(unsigned seat,const SmbItem* items,unsigned count) {
    if(!seat||seat>=19||count>SMB_ITEMS||(!items&&count)||(count&&!remote_active[seat]))return;
    SmbRemoteArticle* pool=remote_articles[seat];
    for(unsigned i=0;i<SMB_ITEMS;i++)pool[i].seen=0;
    unsigned visible=0;
    for(unsigned i=0;i<count;i++) {
        const SmbItem* state=&items[i];if(!smb_remote_article_valid(state))continue;
        unsigned index=SMB_ITEMS;
        for(unsigned n=0;n<SMB_ITEMS;n++)if(pool[n].object&&pool[n].id==state->id){index=n;break;}
        if(index<SMB_ITEMS&&pool[index].kind!=state->kind){smb_native_remote_article_remove(&pool[index]);index=SMB_ITEMS;}
        if(index==SMB_ITEMS)for(unsigned n=0;n<SMB_ITEMS;n++)if(!pool[n].object){index=n;break;}
        if(index==SMB_ITEMS)break;
        SmbRemoteArticle* proxy=&pool[index];
        if(!proxy->object) {
            int max_anim;Article* article=smb_remote_article_lookup(state->kind,&max_anim);if(!article)continue;
            ItemModelDesc* model=DP(ItemModelDesc,article->x10_modelDesc);if(!model->x0_joint)continue;
            HSD_JObj* root=HSD_JObjLoadJoint(DP(HSD_Joint,model->x0_joint));if(!root)continue;
            HSD_GObj* object=GObj_Create(3,5,0);if(!object){HSD_JObjRemoveAll(root);continue;}
            HSD_GObjObject_80390A70(object,HSD_GObj_JObjKind,root);GObj_SetupGXLink(object,smb_remote_article_render,6,0);
            proxy->object=object;proxy->id=state->id;proxy->kind=state->kind;proxy->article=article;proxy->max_anim=max_anim;
            fprintf(stderr,"SMB COOP ARTICLE seat=%u id=%u kind=%u native_model=1 callbacks=0\n",seat,state->id,state->kind);
        }
        proxy->seen=1;visible++;smb_remote_article_animate(proxy,state);
    }
    for(unsigned i=0;i<SMB_ITEMS;i++)if(pool[i].object&&!pool[i].seen)smb_native_remote_article_remove(&pool[i]);
    (void)visible;
}
int smb_embed_remote_articles(unsigned peer,const SmbItem* items,unsigned count) {
    if(peer<1||peer>18||count>SMB_ITEMS||(!items&&count))return -1;
    unsigned seat=peer<=15?peer+3:peer-15;
    smb_native_remote_articles(seat,items,count);return 0;
}

void smb_native_reset_remote_articles(void) {
    for(unsigned seat=1;seat<19;seat++)smb_native_remote_articles(seat,NULL,0);
}

/* SM64 contacts have no native Fighter victim. These native projectile
 * callbacks only use their own item/owner state; tether and custom callback
 * contracts remain explicit rather than receiving an invented fighter. */
int smb_native_item_contact(HSD_GObj* object) {
    if(!object||!object->user_data)return -1;Item* item=object->user_data;
    unsigned kind=item->kind;
    switch(kind) {
    case It_Kind_Fox_Laser:case It_Kind_Falco_Laser:
    case It_Kind_Kirby_FoxLaser:case It_Kind_Kirby_FalcoLaser:
    case It_Kind_Mario_Fire:case It_Kind_DrMario_Vitamin:case It_Kind_Luigi_Fire:
    case It_Kind_Kirby_MarioFire:case It_Kind_Kirby_DrMarioVitamin:case It_Kind_Kirby_LuigiFire:
    case It_Kind_IceClimber_Ice:case It_Kind_Kirby_IceClimberIce:
    case It_Kind_Samus_Bomb:case It_Kind_Samus_Charge:case It_Kind_Samus_Missile:
    case It_Kind_Ness_PKFire:case It_Kind_Peach_Turnip:case It_Kind_Peach_ToadSpore:
    case It_Kind_Kirby_PeachToadSpore:case It_Kind_Mewtwo_ShadowBall:case It_Kind_Kirby_MewtwoShadowBall:
    case It_Kind_GameWatch_Chef:case It_Kind_Kirby_CBeam:
    case It_Kind_Seak_NeedleThrow:
        break;
    default: {
        static unsigned char skipped[301];
        if(kind<301&&!skipped[kind]){skipped[kind]=1;fprintf(stderr,"SMB ITEM CONTACT kind=%u skipped=1 reason=requires_unaudited_victim_contract\n",kind);}
        return -2;
    }}
    HSD_GObjPredicate callback=item->xB8_itemLogicTable?item->xB8_itemLogicTable->dmg_dealt:NULL;
    if(!callback)return 0;
    item->atk_victim=NULL;
    unsigned generation=item->x1C;HSD_GObj* previous=HSD_GObj_804D7814;HSD_GObj_804D7814=object;
    int retire=callback(object);HSD_GObj_804D7814=previous;
    int alive=0;
    for(HSD_GObj* live=HSD_GObjPLinkHead[HSD_GOBJ_PLINK_ITEM];live;live=live->next)
        if(live==object&&live->user_data==item&&((Item*)live->user_data)->x1C==generation){alive=1;break;}
    if(alive&&retire){item->destroy_type=2;Item_8026A8EC(object);}
    static unsigned traces;if(traces++<40)fprintf(stderr,"SMB ITEM CONTACT kind=%u native_callback=1 retired=%u victim_pointer=none\n",kind,!alive||retire);
    return 0;
}
