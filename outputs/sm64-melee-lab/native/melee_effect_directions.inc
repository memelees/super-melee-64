/* Smash Royale's authored effect-direction fixes (754422a00). Detached native
 * effects need the same XZ lane as the move; attachment position has no yaw. */
void efLib_Cb_SetRotY_FromFighterDir(EF_Effect* effect) {
    Fighter* fp=GET_FIGHTER(effect->parent_gobj);
    float yaw=fp->facing_dir<0?-M_PI_2_F:M_PI_2_F,lx,lz;
    if(m3d_GetLane(fp,&lx,&lz))yaw+=atan2f(lx,lz)-M_PI_2_F;
    HSD_JObjSetRotationY(GET_JOBJ(effect->gobj),yaw);
}
void efLib_Cb_SetRotYZ_FromFighter(EF_Effect* effect) {
    Fighter* fp=GET_FIGHTER(effect->parent_gobj);HSD_JObj* jobj=GET_JOBJ(effect->gobj);
    float pitch=M_PI_2_F-fp->mv.co.common.x4.x;
    float yaw=fp->facing_dir<0?-M_PI_2_F:M_PI_2_F,lx,lz;
    if(m3d_GetLane(fp,&lx,&lz))yaw+=atan2f(lx,lz)-M_PI_2_F;
    HSD_JObjSetRotationY(jobj,yaw);HSD_JObjSetRotationZ(jobj,fp->facing_dir<0?pitch:-pitch);
}
