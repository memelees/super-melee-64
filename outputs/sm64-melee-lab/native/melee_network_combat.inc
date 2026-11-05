extern int smb_native_grab_pair(Fighter*,Fighter*);
extern void smb_native_grab_interrupt(Fighter*);
extern int smb_native_grab_borrowed(unsigned,unsigned);
/* Owners resolve network contacts against their real native hurt capsules.
 * Positions are world-space; damage, weight, angles, shield stun and launch
 * reactions use the same native routines as an ordinary local match. */
#include <melee/ft/kinds/ftCommon/ftCo_Guard.h>
#include <melee/lb/lbcollision.h>
static struct {uint32_t data[5];float point[3];} net_contact_queue[2][32];
static unsigned net_contact_read[2],net_contact_write[2];
static void smb_net_queue(unsigned type,unsigned peer,const SmbHit* hit,unsigned seat,unsigned sub,Fighter* fp){unsigned q=type-1;if(net_contact_write[q]-net_contact_read[q]>=32)return;unsigned i=net_contact_write[q]++%32;uint32_t* d=net_contact_queue[q][i].data;d[0]=peer;d[1]=hit->source;d[2]=type==2?hit->owner_sub:hit->index;d[3]=seat;d[4]=sub;memcpy(net_contact_queue[q][i].point,&fp->cur_pos,12);}
int smb_embed_net_event(unsigned type,uint32_t* data,float* point){if(type<1||type>2||!data||!point)return -1;unsigned q=type-1;if(net_contact_read[q]==net_contact_write[q])return 0;unsigned i=net_contact_read[q]++%32;memcpy(data,net_contact_queue[q][i].data,20);memcpy(point,net_contact_queue[q][i].point,12);return 1;}
static unsigned net_attack_serial[4][2],net_attack_bits[4][2],net_attack_motion[4][2];
int smb_embed_net_snapshot(unsigned seat,SmbNetFrame* frame) {
    if(!ready||seat>3||!frame)return -1;
    memset(frame,0,sizeof *frame);frame->version=SMB_MAGIC;frame->sequence=ticks;
    HSD_GObj* primary=Player_GetEntity(seat);
    if(!primary||((Fighter*)primary->user_data)->is_sleeping)return 0;
    if(!seat) {
        for(unsigned i=0;i<output.hit_count;i++)if(output.hit[i].owner==seat)frame->hit[frame->hit_count++]=output.hit[i];
    } else {
        SmbOutput caps={0};
        for(unsigned sub=0;sub<2;sub++) {
            HSD_GObj* object=Player_GetEntityAtIndex(seat,sub);if(!object)continue;
            Fighter* fp=object->user_data;if(fp->is_sleeping)continue;
            unsigned bits=0;for(unsigned h=0;h<4;h++)if(fp->x914[h].state)bits|=1u<<h;
            if(bits&&(bits!=net_attack_bits[seat][sub]||fp->motion_id!=net_attack_motion[seat][sub]))net_attack_serial[seat][sub]++;
            net_attack_bits[seat][sub]=bits;net_attack_motion[seat][sub]=fp->motion_id;
            for(unsigned h=0;h<4;h++)smb_hit(&caps,&fp->x914[h],0x80000000u|(sub<<30)|net_attack_serial[seat][sub],fp->kind,
                fp->x914[h].element==HitElement_Catch?16:0,fp->x34_scale.y,h,seat,sub);
        }
        memcpy(frame->hit,caps.hit,caps.hit_count*sizeof(SmbHit));frame->hit_count=caps.hit_count;
        for(unsigned i=0;i<output.hit_count;i++)if(output.hit[i].owner==seat&&!(output.hit[i].source&0x80000000u)&&frame->hit_count<SMB_HITS)
            frame->hit[frame->hit_count++]=output.hit[i];
    }
    for(unsigned i=0;i<output.item_count;i++)if(output.item[i].owner==seat&&frame->item_count<SMB_ITEMS)frame->item[frame->item_count++]=output.item[i];
    return 0;
}
static float smb_net_dot(Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
static Vec3 smb_net_sub(Vec3 a,Vec3 b){return (Vec3){a.x-b.x,a.y-b.y,a.z-b.z};}
static Vec3 smb_net_add(Vec3 a,Vec3 b,float t){return (Vec3){a.x+b.x*t,a.y+b.y*t,a.z+b.z*t};}
static float smb_net_clamp(float n){return fmaxf(0,fminf(1,n));}
static float smb_net_capsule_distance(Vec3 p,Vec3 q,Vec3 a,Vec3 b) {
    Vec3 d=smb_net_sub(q,p),e=smb_net_sub(b,a),r=smb_net_sub(p,a);
    float dd=smb_net_dot(d,d),ee=smb_net_dot(e,e),dr=smb_net_dot(d,r),er=smb_net_dot(e,r),de=smb_net_dot(d,e),s=0,t=0;
    if(dd<.00001f&&ee<.00001f)return smb_net_dot(r,r);
    if(dd<.00001f)t=smb_net_clamp(er/ee);
    else if(ee<.00001f)s=smb_net_clamp(-dr/dd);
    else {float denominator=dd*ee-de*de;if(denominator>.00001f)s=smb_net_clamp((de*er-dr*ee)/denominator);
        t=(de*s+er)/ee;if(t<0){t=0;s=smb_net_clamp(-dr/dd);}else if(t>1){t=1;s=smb_net_clamp((de-dr)/dd);}}
    Vec3 delta=smb_net_sub(smb_net_add(p,d,s),smb_net_add(a,e,t));return smb_net_dot(delta,delta);
}
static int smb_net_hit_valid(const SmbHit* hit) {
    if(hit->index>3||hit->owner>3||hit->owner_sub>1||hit->element>31||hit->severity>2||hit->sound>13||hit->growth>1000||hit->base>1000||hit->fixed>1000||hit->kb_angle<0||hit->kb_angle>367||
       !isfinite(hit->damage)||hit->damage<0||hit->damage>500||!isfinite(hit->radius)||hit->radius<=0||hit->radius>500)return 0;
    for(unsigned k=0;k<3;k++)if(!isfinite(hit->start[k])||!isfinite(hit->end[k])||fabsf(hit->start[k])>10000||fabsf(hit->end[k])>10000)return 0;
    return 1;
}
static struct {uint32_t source,at;unsigned used;} net_victims[19][19][2][32];
static unsigned net_victim_cursor[19][19][2];
static int smb_net_seen(unsigned peer,unsigned seat,unsigned sub,const SmbHit* hit,int mark) {
    for(unsigned i=0;i<32;i++)if(net_victims[peer][seat][sub][i].used&&net_victims[peer][seat][sub][i].source==hit->source) {
        unsigned since=ticks-net_victims[peer][seat][sub][i].at;
        if(!hit->rehit||since<hit->rehit)return 1;
        if(mark)net_victims[peer][seat][sub][i].at=ticks;return 0;
    }
    if(mark){unsigned i=net_victim_cursor[peer][seat][sub]++%32;net_victims[peer][seat][sub][i].used=1;net_victims[peer][seat][sub][i].source=hit->source;net_victims[peer][seat][sub][i].at=ticks;}return 0;
}
static int smb_net_touches(Fighter* fp,const SmbHit* hit) {
    Vec3 start,end;memcpy(&start,hit->start,12);memcpy(&end,hit->end,12);
    if(!(hit->flags&16)&&ftLib_IsShielding(fp->gobj)&&fp->shield_hit.size>0){float r=hit->radius+fp->shield_hit.size;if(smb_net_capsule_distance(start,end,fp->shield_hit.pos,fp->shield_hit.pos)<=r*r)return 1;}
    for(unsigned n=0;n<fp->hurt_capsules_len;n++) {
        HurtCapsule* hurt=&fp->hurt_capsules[n].capsule;
        if(hurt->state!=HurtCapsule_Enabled||((hit->flags&16)&&!fp->hurt_capsules[n].is_grabbable))continue;
        float radius=hit->radius+hurt->scale*fp->x34_scale.y;
        if(smb_net_capsule_distance(start,end,hurt->a_pos,hurt->b_pos)<=radius*radius)return 1;
    }
    return 0;
}
static void smb_net_damage(Fighter* fp,const SmbHit* state,Fighter* attacker) {
    HSD_GObj* object=fp->gobj;HitCapsule hit={0};
    hit.damage=state->damage;hit.kb_angle=state->kb_angle;hit.x24=state->growth;hit.x28=state->fixed;hit.x2C=state->base;
    hit.element=state->element;hit.sfx_severity=state->severity;hit.sfx_kind=state->sound;
    HSD_GObj* previous=HSD_GObj_804D7814;HSD_GObj_804D7814=object;
    if(!(state->flags&16)&&ftLib_IsShielding(object)) {
        fp->x19A0_shieldDamageTaken+=(int)ceilf(state->damage);fp->x19A4=(int)ceilf(state->damage);
        fp->x19BC_shieldDamageTaken3=attacker?attacker->player_idx:0;fp->x221F_b6=attacker?attacker->is_sub_fighter:0;
        fp->x19A8=attacker?attacker->gobj:NULL;fp->x19B0=state->element;fp->specialn_facing_dir=-1;
        /* Fighter_procCollResolve owns shield health, stun, break and effects. */
        fprintf(stderr,"SMB NET SHIELD seat=%u damage=%.2f health=%.2f\n",fp->player_idx,state->damage,fp->shield_health);
    } else {
        Vec3 origin=attacker?attacker->cur_pos:(Vec3){state->start[0],state->start[1],state->start[2]};
        float x=fp->cur_pos.x-origin.x,z=fp->cur_pos.z-origin.z,length=hypotf(x,z);
        if(length>.001f){m3d_SetLane(fp,x/length,z/length);fp->facing_dir=1;}
        fp->dmg.x1838_percentTemp=state->damage;
        fp->dmg.kb_applied=ftColl_80079AB0(fp,&hit,(unsigned)ceilf(state->damage),1,1,1,smb_native_weight(fp));
        Fighter_TakeDamage_8006CC7C(fp,state->damage);fp->dmg.facing_dir_1=-1;fp->dmg.x1848_kb_angle=state->kb_angle;
        fp->dmg.x184c_damaged_hurtbox=1;fp->dmg.x1860_element=state->element;fp->dmg.x1854_collpos=fp->cur_pos;
        float authored_knockback=fp->dmg.kb_applied;ftCo_Damage_CalcKnockback(fp);ftCo_8008DCE0(object,-1,0);smb_native_damage_voice(fp);fp->dmg.x1838_percentTemp=0;
        fp->dmg.x195c_hitlag_frames=ftCommon_CalcHitlag((int)ceilf(state->damage),fp->motion_id,fp->hitlag_mul);Fighter_8006CFE0(object);
        ftColl_80078538(object,&fp->cur_pos,state->severity,state->damage,50);lbColl_80005BB0(&hit,0x72);
        fprintf(stderr,"SMB NET HIT victim=%u sub=%u source=%u damage=%.2f percent=%.2f angle=%d knockback=%.3f vx=%.3f vy=%.3f vz=%.3f\n",fp->player_idx,fp->is_sub_fighter,state->source,state->damage,fp->dmg.x1830_percent,state->kb_angle,authored_knockback,fp->x8c_kb_vel.x,fp->x8c_kb_vel.y,fp->x8c_kb_vel.z);
    }
    HSD_GObj_804D7814=previous;
}
/* A remote actor never takes damage on this machine. Its owner is the only
 * writer of its percentage and knockback, preventing duplicate network hits. */
int smb_embed_net_hits(unsigned peer,const SmbHit* hits,unsigned count) {
    if(!ready||peer>18||count>SMB_HITS||(count&&!hits))return -1;
    if(!count&&!hits){memset(net_victims[peer],0,sizeof net_victims[peer]);memset(net_victim_cursor[peer],0,sizeof net_victim_cursor[peer]);return 0;}
    unsigned source_seat=!peer?0:peer<16?peer+3:peer-15;
    HSD_GObj* actor=Player_GetEntity(source_seat);if(!actor)return 0;
    Fighter* attacker=actor->user_data;int contacts=0;
    for(unsigned seat=0;seat<19;seat++)for(unsigned sub=0;sub<2;sub++) {
        if(!peer&&(seat<4||!smb_native_grab_borrowed(seat,sub)))continue;
        if(seat==source_seat)continue;
        if(seat>3&&!smb_native_grab_borrowed(seat,sub))continue;
        if(peer>15&&seat>0&&seat<4)continue; /* Same-team campaign CPUs do not fight. */
        HSD_GObj* object=Player_GetEntityAtIndex(seat,sub);if(!object)continue;
        Fighter* fp=object->user_data;if(fp->is_sleeping||smb_native_remote_controlled(fp)||fp->stamina_dead||fp->x1988||fp->x198C)continue;
        const SmbHit* best=NULL;Fighter* best_attacker=attacker;
        for(unsigned h=0;h<count;h++) {
            const SmbHit* hit=&hits[h];if(!smb_net_hit_valid(hit)||smb_net_seen(peer,seat,sub,hit,0)||!smb_net_touches(fp,hit))continue;
            HSD_GObj* source_object=Player_GetEntityAtIndex(source_seat,hit->owner_sub);Fighter* source=source_object?source_object->user_data:attacker;
            if(smb_native_grab_pair(source,fp))continue;
            /* Native grabs need an ownership handshake; do not turn catches
             * into artificial punches or damage a shield with a catch. */
            if(hit->flags&16){if(fp->victim_gobj||fp->x2219_b1||fp->x222A_b0||fp->x221D_b6||(fp->x1A6A&source->x1A68))continue;smb_net_seen(peer,seat,sub,hit,1);smb_net_queue(2,peer,hit,seat,sub,fp);continue;}
            if(!best||hit->damage>best->damage){best=hit;best_attacker=source;}
        }
        if(best){smb_net_seen(peer,seat,sub,best,1);smb_native_grab_interrupt(fp);smb_net_damage(fp,best,best_attacker);smb_net_queue(1,peer,best,seat,sub,fp);contacts++;}
    }
    return contacts;
}
