/* SM64 supplies hostile-object geometry; native Melee supplies each actor's
 * hurt capsules, shield, weight, damage voice, hitstun and flight. */
extern int smb_native_actor_retire_partner(unsigned,unsigned);
#include <melee/ft/ftcommon.h>
#include <melee/ft/ftmetal.h>
extern int smb_embed_npc_add(unsigned,unsigned,unsigned,unsigned,const float*);
extern int smb_embed_campaign_remote(unsigned,const SmbRemote*);
static int smb_actor_cylinder_segment(Vec3 a,Vec3 b,float padding,const SmbHit* hit) {
    float low=fminf(hit->start[1],hit->end[1])-padding,high=fmaxf(hit->start[1],hit->end[1])+padding;
    float from=0,to=1,dy=b.y-a.y;
    if(fabsf(dy)<.00001f){if(a.y<low||a.y>high)return 0;}
    else {float p=(low-a.y)/dy,q=(high-a.y)/dy;if(p>q){float t=p;p=q;q=t;}from=fmaxf(from,p);to=fminf(to,q);if(from>to)return 0;}
    float dx=b.x-a.x,dz=b.z-a.z,x=a.x+dx*from-hit->start[0],z=a.z+dz*from-hit->start[2];
    float span=to-from,length=dx*dx+dz*dz,t=length>.00001f?fmaxf(0,fminf(span,-(x*dx+z*dz)/length)):0;
    x+=dx*t;z+=dz*t;float radius=hit->radius+padding;return x*x+z*z<=radius*radius;
}
static int smb_actor_cylinder_touches(Fighter* fp,const SmbHit* hit) {
    if(ftLib_IsShielding(fp->gobj)&&fp->shield_hit.size>0&&smb_actor_cylinder_segment(fp->shield_hit.pos,fp->shield_hit.pos,fp->shield_hit.size,hit))return 1;
    for(unsigned n=0;n<fp->hurt_capsules_len;n++) {
        HurtCapsule* hurt=&fp->hurt_capsules[n].capsule;
        if(hurt->state==HurtCapsule_Enabled&&smb_actor_cylinder_segment(hurt->a_pos,hurt->b_pos,hurt->scale*fp->x34_scale.y,hit))return 1;
    }
    return 0;
}
int smb_embed_actor_world_hit(unsigned seat,unsigned sub,const SmbHit* hit,unsigned flags) {
    if(!ready||seat<1||seat>3||sub>1||!hit||flags>15||!smb_net_hit_valid(hit))return -1;
    HSD_GObj* object=Player_GetEntityAtIndex(seat,sub);if(!object)return 0;
    Fighter* fp=object->user_data;
    if(fp->is_sleeping||smb_native_remote_controlled(fp)||fp->stamina_dead||fp->x1988||fp->x198C||fp->x221A_b5||fp->x2226_b4||(fp->is_metal&&!(flags&8)))return 0;
    if(!(flags&1)&&!((flags&4)?smb_actor_cylinder_touches(fp,hit):smb_net_touches(fp,hit)))return 0;
    if(flags&8)return 3; /* Contact query for fatal interactions, no hit state. */
    int shield=ftLib_IsShielding(object);
    if((flags&2)&&shield){ftCommon_8007D5D4(fp);ftCo_Fall_Enter(object);shield=0;}
    smb_native_grab_interrupt(fp);smb_net_damage(fp,hit,NULL);
    if((flags&2)&&!shield) {
        fp->x8c_kb_vel.y=fmaxf(3.5f,fp->x8c_kb_vel.y);fp->fall_fast=false;
    }
    fprintf(stderr,"SMB NPC WORLD HIT seat=%u sub=%u source=%u damage=%.2f percent=%.2f motion=%u lava=%u shield=%d\n",
        seat,sub,hit->source,hit->damage,fp->dmg.x1830_percent,fp->motion_id,(flags>>1)&1,shield);
    return shield?2:1;
}
int smb_embed_actor_world_ko(unsigned seat,unsigned sub) {
    if(!ready||seat<1||seat>3||sub>1)return -1;
    HSD_GObj* object=Player_GetEntityAtIndex(seat,sub);if(!object)return 0;
    Fighter* fp=object->user_data;if(fp->is_sleeping||smb_native_remote_controlled(fp))return 0;
    if(!sub)return 1; /* The host retires Popo and his team via the KO ledger. */
    /* Unlike a doorway suspension, a defeated Nana must never be revived by
     * the ordinary NPC rejoin scheduler while Popo remains alive. */
    int result=smb_native_actor_retire_partner(seat,sub);
    if(result>0)fprintf(stderr,"SMB NPC WORLD PARTNER KO seat=%u sub=%u kind=%u\n",seat,sub,fp->kind);return result;
}
static void smb_native_actor_world_test(void) {
    if(!getenv("SMB_SMOKE")||!getenv("SMB_SCENARIO")||strcmp(getenv("SMB_SCENARIO"),"npc-environment")||!ready)return;
    static unsigned phase,began;
    if(!phase&&ticks>400) {
        HSD_GObj* player=Player_GetEntity(0);if(!player)return;Fighter* fp=player->user_data;
        float position[3]={fp->cur_pos.x+30,fp->cur_pos.y+1,fp->cur_pos.z};
        int result=smb_embed_npc_add(3,14,0,1,position);
        if(result){fprintf(stderr,"SMB NPC ENV TEST NATIVE FAIL reason=create code=%d\n",result);phase=99;return;}
        Player_SetPlayerAndEntityCpuType(3,0);phase=1;began=ticks;return;
    }
    if(phase!=1||ticks-began<8)return;
    HSD_GObj* object=Player_GetEntity(3);if(!object){phase=99;return;}Fighter* fp=object->user_data;
    if(fp->ground_or_air!=GA_Ground&&ticks-began<120)return;
    SmbHit hit={0};hit.source=123;hit.damage=8;hit.growth=100;hit.base=60;hit.kb_angle=45;hit.severity=1;hit.sound=1;hit.radius=20;
    hit.start[0]=hit.end[0]=fp->cur_pos.x;hit.start[2]=hit.end[2]=fp->cur_pos.z;hit.start[1]=fp->cur_pos.y-5;hit.end[1]=fp->cur_pos.y+30;
    float before=fp->dmg.x1830_percent;int normal=smb_embed_actor_world_hit(3,0,&hit,4)==1&&fp->dmg.x1830_percent>=before+8;
    SmbHit miss=hit;miss.start[0]+=200;miss.end[0]+=200;before=fp->dmg.x1830_percent;
    int separated=smb_embed_actor_world_hit(3,0,&miss,4)==0&&fp->dmg.x1830_percent==before;
    fp->x1988=fp->x198C=0;fp->x221A_b5=false;ftCommon_8007D5D4(fp);ftCo_Fall_Enter(object);
    fp->input.held_buttons[0]=HSD_PAD_LR;fp->input.pressed_buttons=0;fp->shield_health=60;
    int guarded=ftCo_80091AD8(object,0);before=fp->dmg.x1830_percent;
    int shield=guarded&&smb_embed_actor_world_hit(3,0,&hit,1)==2&&fp->dmg.x1830_percent==before;
    ftCommon_8007D5D4(fp);ftCo_Fall_Enter(object);fp->input.held_buttons[0]=0;
    fp->x1988=fp->x198C=0;fp->x221A_b5=false;ftCo_800C8348(object,4,1000000);
    SmbHit lava=hit;lava.damage=24;lava.element=1;lava.kb_angle=80;
    int metal=fp->is_metal&&smb_embed_actor_world_hit(3,0,&lava,3)==0&&fp->dmg.x1830_percent==before;
    int fatal_metal=smb_embed_actor_world_hit(3,0,&hit,13)==3&&fp->dmg.x1830_percent==before;
    ftCo_800C8540(object);int bounce=smb_embed_actor_world_hit(3,0,&lava,3)==1&&fp->x8c_kb_vel.y>=3.5f;
    HSD_GObj* partner=Player_GetEntityAtIndex(3,1);
    int nana=partner&&smb_embed_actor_world_ko(3,1)>0&&((Fighter*)partner->user_data)->is_sleeping&&!fp->is_sleeping;
    fprintf(stderr,"SMB NPC ENV TEST NATIVE %s contact=%d separated=%d shield=%d metal=%d fatal_metal=%d lava=%d nana=%d\n",normal&&separated&&shield&&metal&&fatal_metal&&bounce&&nana?"PASS":"FAIL",normal,separated,shield,metal,fatal_metal,bounce,nana);
    smb_embed_campaign_remote(3,NULL);phase=2;
}
