/* World enemies have SM64 damage values; Melee owns the resulting reaction. */
#include <melee/ft/fighter.h>
#include <melee/ft/ftcoll.h>
#include <melee/ft/kinds/ftCommon/ftCo_Damage.h>
#include <math.h>
extern float smb_native_weight(Fighter*);
extern void smb_native_damage_voice(Fighter*);
extern void smb_native_damage_reset(Fighter*);
int smb_embed_health(float change,float away_x,float away_z) {
    HSD_GObj* object=Player_GetEntity(0);if(!ready||!object||!isfinite(change))return -1;
    Fighter* fp=object->user_data;
    if(change>0&&ftLib_IsShielding(object)){fprintf(stderr,"SMB WORLD SHIELD blocked=%.2f\n",change);return 1;}
    if(change<=-1000){smb_native_reset_world_items();smb_native_damage_reset(fp);}
    if(change<=0) {
        fp->dmg.x1830_percent=fmaxf(0,fp->dmg.x1830_percent+change);
        Player_SetHPByIndex(fp->player_idx,fp->is_sub_fighter,fp->dmg.x1830_percent);
    } else {
        float distance=hypotf(away_x,away_z);
        if(distance>.001f)m3d_SetLane(fp,away_x/distance,away_z/distance);
        /* World enemies map to a solid Melee hit. Native weight, percentage,
         * launch angle, hitstun and damage-flight states own the response. */
        HitCapsule hit={0};hit.x24=100;hit.x2C=60;
        fp->dmg.x1838_percentTemp=change;
        float weight=smb_native_weight(fp);
        fp->dmg.kb_applied=ftColl_80079AB0(fp,&hit,(unsigned)change,1,1,1,weight);
        Fighter_TakeDamage_8006CC7C(fp,change);
        fp->dmg.facing_dir_1=-1;fp->dmg.x1848_kb_angle=45;
        fp->dmg.x184c_damaged_hurtbox=1;fp->dmg.x1860_element=HitElement_Normal;
        fp->dmg.x1854_collpos=fp->cur_pos;
        HSD_GObj* previous=HSD_GObj_804D7814;HSD_GObj_804D7814=object;
        ftCo_Damage_CalcKnockback(fp);float knockback=fp->dmg.kb_applied;ftCo_8008DCE0(object,-1,0);
        /* The adapter bypasses the retail hit dispatch. Resolve its pending
         * damage voice now, with the character's small-hit table as fallback.
         * Missing/empty tables stay silent. Never substitute Mario's voice. */
        smb_native_damage_voice(fp);
        HSD_GObj_804D7814=previous;
        fp->dmg.x1838_percentTemp=0; /* Already applied at the host boundary. */
        fprintf(stderr,"SMB WORLD LAUNCH percent=%.2f knockback=%.3f vx=%.3f vy=%.3f weight=%.1f\n",fp->dmg.x1830_percent,knockback,fp->x8c_kb_vel.x,fp->x8c_kb_vel.y,weight);
    }
    fprintf(stderr,"SMB WORLD HEALTH change=%.2f percent=%.2f motion=%d\n",change,fp->dmg.x1830_percent,fp->motion_id);
    return 0;
}
