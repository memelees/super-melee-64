/* Test-only object: uses SM64's normal dynamic-surface loader and transforms. */
#include "engine/behavior_script.h"
#include "surface_terrains.h"
static const Collision smb_platform_collision[]={
    COL_INIT(),COL_VERTEX_INIT(8),
    COL_VERTEX(-250,20,-250),COL_VERTEX(-250,20,250),COL_VERTEX(250,20,250),COL_VERTEX(250,20,-250),
    COL_VERTEX(-250,-300,-250),COL_VERTEX(-250,-300,250),COL_VERTEX(250,-300,250),COL_VERTEX(250,-300,-250),
    COL_TRI_INIT(SURFACE_DEFAULT,12),
    COL_TRI(0,1,2),COL_TRI(0,2,3),COL_TRI(4,6,5),COL_TRI(4,7,6),
    COL_TRI(0,4,5),COL_TRI(0,5,1),COL_TRI(1,5,6),COL_TRI(1,6,2),
    COL_TRI(2,6,7),COL_TRI(2,7,3),COL_TRI(3,7,4),COL_TRI(3,4,0),
    COL_TRI_STOP(),COL_END()
};
static void smb_platform_tick(void) {
    struct Object* object=gCurrentObject;
    object->collisionData=smb_platform_collision;object->oCollisionDistance=6000;object->oDrawingDistance=6000;object->oDistanceToMario=1000;
    object->oVelX=2;object->oVelZ=1;object->oAngleVelYaw=0x100;
    object->oPosX+=2;object->oPosZ+=1;object->oFaceAngleYaw+=0x100;
    load_object_collision_model();
}
static const BehaviorScript smb_platform_behavior[]={
    ((uintptr_t)OBJ_LIST_SURFACE<<16), /* BEGIN */
    0x08000000, /* BEGIN_LOOP */
    0x0c000000,(uintptr_t)smb_platform_tick, /* CALL_NATIVE */
    0x09000000 /* END_LOOP */
};
