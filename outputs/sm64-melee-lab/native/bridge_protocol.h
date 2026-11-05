#ifndef SM64_MELEE_BRIDGE_PROTOCOL_H
#define SM64_MELEE_BRIDGE_PROTOCOL_H
#include <stdint.h>
#define SMB_MAGIC 0x534d423eu
#define SMB_BONES 128
#define SMB_ITEMS 64
#define SMB_HITS 268
#define SMB_SOUNDS 128
typedef struct {uint32_t sequence,source,sound;uint8_t owner,owner_sub,type,volume,pan,flags;uint16_t duration;float position[3];} SmbSound;
typedef struct {uint32_t id,kind,motion,flags;float position[3],velocity[3],frame,rotation[4],scale[3];uint32_t animation_index,model_flags,owner,owner_sub,joint_hidden[4],dobj_hidden[4];} SmbItem;
typedef struct {uint32_t source,kind,flags,index;float start[3],end[3],radius,damage;int32_t kb_angle;uint32_t growth,fixed,base,element,severity,sound,owner,owner_sub,rehit;} SmbHit;
/* Private same-host little-endian boundary, versioned and fixed-width.
 * Version 14 adds an encounter-round serial for repeatable authored rounds.
 * Version 13 adds actual-damage events, cumulative grab damage and pending
 * CPU air damage during encounter-authority handoff.
 * Version 12 adds independent swim-depth controls and resource-aware water
 * transitions. Version 11 adds per-actor environment sampling and transfers CPU air
 * reserves with encounter authority. Version 10 introduced independent partners,
 * article presentation, authored combat, model selection and sound data. Network packets serialize bounded
 * chunks of these fields. */
#define SMB_SWIM_UP 0x10000u
#define SMB_SWIM_DOWN 0x20000u
#define SMB_PAD_BUTTONS 0xffffu
#define SMB_DOWN_ATTACK 1u
#define SMB_WALL_KICK 2u
#define SMB_ENV_WATER 512u
#define SMB_ENV_SHELL 1024u
#define SMB_ENV_WING 2048u
#define SMB_ENV_METAL 4096u
#define SMB_ENV_CAP_MANAGED 8192u
#define SMB_ENV_HOSTED (SMB_ENV_SHELL|SMB_ENV_WING)
typedef struct {
    uint32_t magic, sequence, buttons, flags;
    float stick_x, stick_y, camera_yaw, x, y, z, platform_yaw;
    /* Host units -> Melee units per 60 Hz tick. Flag 32: slide travel;
     * flag 128: one-shot launch velocity; flag 256: cannon launch source. */
    float slope_velocity[3],spawn_headroom;
    float look_x,look_y;
    float water_level;
} SmbInput;
typedef struct {float eye[3],target[3],fov,yaw;} SmbCamera;
typedef struct {uint32_t kind,motion,level,target,hitboxes;float position[3],percent;uint32_t suspended,grounded,damage_flight,costume;float knockback;} SmbNpc;
typedef struct {uint32_t character[3],costume[3];} SmbEncounter;
/* Coop Deluxe transports this beside its normal authoritative player state.
 * Peers send authored motion/frame, never native pointers or disc structures. */
typedef struct {uint32_t model_count;int32_t part[12];uint32_t dobj_hidden[8];} SmbFighterModel;
typedef struct {
    uint32_t kind,motion,grounded,caps;
    float frame,percent,position[3],facing[3],velocity[3];
    SmbFighterModel model;
} SmbFighterState;
typedef struct {
    uint32_t version,character,costume,kind,motion,grounded,caps;
    float frame,percent,position[3],facing[3],velocity[3];
    SmbFighterModel model;
    uint32_t partner_active;SmbFighterState partner;
} SmbRemote;
typedef struct {
    uint32_t version,sequence,authority,level,area,act,course,key,wave,waves,count,active_mask,defeated_mask;
    uint32_t ko_serial[3],ko_star_mask;float ko_position[3][3];
    uint32_t cpu_level[3],mode,next_wave_delay;SmbRemote fighter[3];
    uint32_t npc_air_valid,npc_air[3][2];float npc_air_hurt[3][2];
    uint32_t round_serial;
} SmbCampaign;
typedef struct {
    uint32_t version,sequence,item_count,hit_count;
    SmbItem item[SMB_ITEMS];SmbHit hit[SMB_HITS];
} SmbNetFrame;
typedef struct {
    uint32_t version,id,phase,catcher,catcher_sub,victim,victim_sub;
    SmbFighterState catcher_state,victim_state;
    float self_velocity[3],knockback_velocity[3],hitstun,grab_timer;
    uint32_t damage_motion;float kb_applied,hitlag;int32_t kb_angle;uint32_t element;
    float damage_total;
} SmbGrabState;
typedef struct {
    uint32_t magic, sequence, kind, motion, grounded, hitboxes, bones, reserved;
    float x, y, z, vx, vy, vz, percent, animation;
    float matrices[SMB_BONES][12];
    uint32_t joint_ids[SMB_BONES], visible[SMB_BONES][4];
    uint32_t item_count,hit_count,grab_active,shielding,abilities,character,costume;float facing[3];SmbItem item[SMB_ITEMS];SmbHit hit[SMB_HITS];
    SmbFighterModel model;uint32_t partner_active;SmbFighterState partner;
} SmbOutput;
typedef struct {
    float vertices[9],normal[3];uint32_t type,flags;
} SmbSurface;
typedef struct {
    uint32_t width,height;
    float view[12],projection[16],position[3],yaw;uint32_t flags;
} SmbRender;
#endif
