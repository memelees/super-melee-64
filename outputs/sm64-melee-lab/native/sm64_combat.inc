/* Native attack capsules -> SM64 behavior attack messages. */
#include "game/interaction.h"
#include "object_fields.h"
#include "object_constants.h"
static unsigned smb_victims[OBJECT_POOL_CAPACITY][8],smb_victim_cursor[OBJECT_POOL_CAPACITY];
static unsigned char smb_was_active[OBJECT_POOL_CAPACITY];
static int smb_touch(const SmbHit* hit,const struct Object* object) {
    float a[3],b[3];for(int k=0;k<3;k++){a[k]=hit->start[k]*10;b[k]=hit->end[k]*10;}
    float dx=b[0]-a[0],dy=b[1]-a[1],dz=b[2]-a[2];
    if(dx*dx+dy*dy+dz*dz>800*800){memcpy(a,b,sizeof a);dx=dy=dz=0;}
    float height=object->hurtboxHeight>0?object->hurtboxHeight:object->hitboxHeight;
    float radius=object->hurtboxRadius>0?object->hurtboxRadius:object->hitboxRadius;
    float cx=object->oPosX,cy=object->oPosY-object->hitboxDownOffset+height*.5f,cz=object->oPosZ;
    float length=dx*dx+dy*dy+dz*dz;
    float t=length>1e-6f?((cx-a[0])*dx+(cy-a[1])*dy+(cz-a[2])*dz)/length:1;
    t=fmaxf(0,fminf(t,1));float x=a[0]+t*dx-cx,z=a[2]+t*dz-cz,y=a[1]+t*dy-cy;
    float r=hit->radius*10+radius;
    return x*x+z*z<=r*r&&fabsf(y)<=height*.5f+hit->radius*10;
}
static void smb_native_grabs(void) {
    if(native_grab_target)return;
    for(unsigned h=0;h<snapshot.hit_count;h++) {
        const SmbHit* grab=&snapshot.hit[h];if(!(grab->flags&16))continue;
        for(unsigned i=0;i<OBJECT_POOL_CAPACITY;i++) {
            struct Object* object=&gObjectPool[i];
            if(!(object->activeFlags&ACTIVE_FLAG_ACTIVE)||object==gMarioObject)continue;
            int tail=object->behavior==segmented_to_virtual(bhvBowserTailAnchor);
            struct Object* target=tail?object->parentObj:object;
            if(!tail&&(!(object->oInteractType&INTERACT_GRABBABLE)||object->oIntangibleTimer<0))continue;
            if(!tail&&object->behavior==segmented_to_virtual(bhvBowser))continue;
            if(target->oHeldState!=HELD_FREE||target->oInteractionSubtype&INT_SUBTYPE_NOT_GRABBABLE)continue;
            if(!smb_touch(grab,object))continue;
            if(tail) {
                s16 angle=(s16)(gMarioState->faceAngle[1]-target->oMoveAngleYaw);
                if(angle< -0x5555||angle>0x5555||target->oAction==19)continue;
                target->oIntangibleTimer=0;
            }
            native_grab_target=target;
            fprintf(stderr,"SMB NATIVE GRAB object=%u tail=%d capsule=%u\n",i,tail,h);
            return;
        }
    }
}
static void smb_host_apply_hits(void) {
    smb_native_grabs();
    for(unsigned i=0;i<OBJECT_POOL_CAPACITY;i++) {
        struct Object* object=&gObjectPool[i];
        if(!(object->activeFlags&ACTIVE_FLAG_ACTIVE)) {smb_was_active[i]=0;continue;}
        if(!smb_was_active[i]){memset(smb_victims[i],0,sizeof smb_victims[i]);smb_victim_cursor[i]=0;smb_was_active[i]=1;}
        if(object==gMarioObject||object->oInteractStatus&INT_STATUS_WAS_ATTACKED)continue;
        unsigned type=object->oInteractType;
        unsigned target=INTERACT_BOUNCE_TOP|INTERACT_BOUNCE_TOP2|INTERACT_HIT_FROM_BELOW|INTERACT_BREAKABLE|INTERACT_BULLY|INTERACT_KOOPA|INTERACT_MR_BLIZZARD|INTERACT_GRABBABLE;
        if(!(type&(target|INTERACT_DOOR|INTERACT_WARP_DOOR)))continue;
        if(object->oInteractionSubtype&INT_SUBTYPE_HOLDABLE_NPC)continue;
        for(unsigned h=0;h<snapshot.hit_count;h++) {
            const SmbHit* hit=&snapshot.hit[h];if((hit->flags&16)||hit->damage<=0||hit->radius<=0||!isfinite(hit->radius))continue;
            if(!(type&target)&&!(hit->flags&1))continue;
            int already=0;for(int v=0;v<8;v++)if(smb_victims[i][v]==hit->source)already=1;
            if(already||!smb_touch(hit,object))continue;
            smb_victims[i][smb_victim_cursor[i]++%8]=hit->source;
            if(type&target) {
                unsigned attack=(hit->flags&(2|32))?ATTACK_GROUND_POUND_OR_TWIRL:(hit->flags&1)?ATTACK_FAST_ATTACK:ATTACK_PUNCH;
                object->oInteractStatus=INT_STATUS_INTERACTED|INT_STATUS_WAS_ATTACKED|attack;
                object->oMoveAngleYaw=atan2s(hit->end[2]-hit->start[2],hit->end[0]-hit->start[0]);
                if(embedded_impact) {
                    float point[3]={object->oPosX*.1f,(object->oPosY+object->hitboxHeight*.5f)*.1f,object->oPosZ*.1f};
                    embedded_impact(hit->source,hit->index,point);
                }
                fprintf(stderr,"SMB WORLD HIT tick=%u source=%u flags=%u kind=%u damage=%.2f object=%u type=%08x\n",sequence,hit->source,hit->flags,hit->kind,hit->damage,i,type);
            }
            if((hit->flags&1)&&embedded_contact)embedded_contact(hit->source);
            break;
        }
    }
}
