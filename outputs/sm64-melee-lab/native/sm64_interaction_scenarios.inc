/* Opt-in fixtures still use real native input, SM64 objects and boss actions. */
static unsigned interaction_tick,carry_tick;
static struct Object* interaction_object;
static int interaction_placed;
static unsigned launch_test_frame;
static int smb_interaction_test(void) {
    const char* s=getenv("SMB_SCENARIO");
    return s&&(!strcmp(s,"grab")||!strcmp(s,"bowser")||!strcmp(s,"shield")||!strcmp(s,"heave-ho")||!strcmp(s,"king-bobomb")||!strncmp(s,"cannon",6));
}
static void smb_world_interaction_test(struct MarioState* m) {
    if(!smb_interaction_test())return;
    const char* test=getenv("SMB_SCENARIO");int bowser=!strcmp(test,"bowser");
    if(!strncmp(test,"cannon",6)) {
        if(gCurrLevelNum!=LEVEL_BOB) {
            static int requested;if(!requested){extern void initiate_warp(s16,s16,s16,s32);initiate_warp(LEVEL_BOB,1,0x0a,0);requested=1;active=0;}return;
        }
        if(!interaction_placed&&frames>12) {
            /* Exercise a map-placed BOB cannon rather than a raised fixture
             * on empty ground. The jump key follows the live firing path. */
            float closest=1e9f;
            for(unsigned i=0;i<OBJECT_POOL_CAPACITY;i++) {
                struct Object* o=&gObjectPool[i];
                if(!(o->activeFlags&ACTIVE_FLAG_ACTIVE)||o->behavior!=segmented_to_virtual(bhvCannon))continue;
                float d=hypotf(o->oPosX+4950,o->oPosZ+3600);if(d<closest){closest=d;interaction_object=o;}
            }
            if(!interaction_object)return;
            m->pos[0]=interaction_object->oPosX;m->pos[1]=interaction_object->oPosY+80;m->pos[2]=interaction_object->oPosZ;
            interaction_placed=1;active=0;
            fprintf(stderr,"SMB INTERACTION FIXTURE name=cannon level=%d map_placed=1 x=%.1f y=%.1f z=%.1f\n",gCurrLevelNum,m->pos[0],m->pos[1],m->pos[2]);
        }
        if(interaction_placed) {
            interaction_tick++;
            if(m->action==ACT_IN_CANNON&&m->actionState==2){m->faceAngle[0]=0x2000;smb_host_key(SDL_SCANCODE_L,1);}
            else smb_host_key(SDL_SCANCODE_L,0);
            if(snapshot.motion==38&&!launch_test_frame)launch_test_frame=frames;
            if(interaction_tick%10==0)fprintf(stderr,"SMB CANNON TRACE tick=%u native_frames=%u active=%d action=%08x state=%u object_action=%d y=%.2f motion=%u\n",interaction_tick,frames,active,m->action,m->actionState,interaction_object->oAction,m->pos[1],snapshot.motion);
        }
        return;
    }
    if(!strcmp(test,"heave-ho")||!strcmp(test,"king-bobomb")) {
        int king=!strcmp(test,"king-bobomb"),level=king?LEVEL_BOB:LEVEL_WDW;
        if(gCurrLevelNum!=level) {
            static int requested;if(!requested){extern void initiate_warp(s16,s16,s16,s32);initiate_warp(level,1,0x0a,0);requested=1;active=0;}return;
        }
        /* Keep this real Wet-Dry World enemy above its fixture waterline. */
        if(!king&&gEnvironmentRegions){gEnvironmentRegions[6]=0;gEnvironmentLevels[0]=0;}
        if(!interaction_placed&&frames>12)for(unsigned i=0;i<OBJECT_POOL_CAPACITY;i++) {
            struct Object* enemy=&gObjectPool[i];
            if(!(enemy->activeFlags&ACTIVE_FLAG_ACTIVE)||enemy->behavior!=segmented_to_virtual(king?bhvKingBobomb:bhvHeaveHo))continue;
            if(!king&&(enemy->oPosY<1000||enemy->oPosY>1500))continue;
            interaction_object=enemy;
            if(king){enemy->oAction=2;enemy->oIntangibleTimer=0;enemy->oKingBobombUnk100=2;enemy->oMoveAngleYaw=0;}
            m->pos[0]=enemy->oPosX+sins(enemy->oMoveAngleYaw)*90;
            m->pos[2]=enemy->oPosZ+coss(enemy->oMoveAngleYaw)*90;
            m->pos[1]=find_floor(m->pos[0],enemy->oPosY+300,m->pos[2],&m->floor)+2;
            interaction_placed=1;active=0;
            fprintf(stderr,"SMB INTERACTION FIXTURE name=%s level=%d x=%.2f y=%.2f z=%.2f\n",test,gCurrLevelNum,m->pos[0],m->pos[1],m->pos[2]);break;
        }
        if(interaction_placed) {
            interaction_tick++;
            if(snapshot.motion==38&&!launch_test_frame)launch_test_frame=frames;
            if(interaction_tick%5==0)fprintf(stderr,"SMB ENEMY THROW name=%s tick=%u native_frames=%u active=%d action=%08x y=%.2f vy=%.2f lives=%d health=%d\n",test,interaction_tick,frames,active,m->action,m->pos[1],m->vel[1],m->numLives,m->health);
        }
        return;
    }
    if(bowser&&gCurrLevelNum!=LEVEL_BOWSER_1) {
        static int requested;if(!requested){extern void initiate_warp(s16,s16,s16,s32);initiate_warp(LEVEL_BOWSER_1,1,0x0a,0);requested=1;active=0;}return;
    }
    if(!interaction_placed&&frames>12) {
        if(bowser) {
            for(unsigned i=0;i<OBJECT_POOL_CAPACITY;i++) {
                struct Object* tail=&gObjectPool[i];
                if(!(tail->activeFlags&ACTIVE_FLAG_ACTIVE)||tail->behavior!=segmented_to_virtual(bhvBowserTailAnchor))continue;
                struct Object* boss=tail->parentObj;
                if(boss->oAction==4)continue;
                interaction_object=boss;
                m->pos[0]=tail->oPosX-sins(boss->oMoveAngleYaw)*90;
                m->pos[2]=tail->oPosZ-coss(boss->oMoveAngleYaw)*90;
                m->pos[1]=find_floor(m->pos[0],boss->oPosY+300,m->pos[2],&m->floor)+2;
                m->faceAngle[1]=boss->oMoveAngleYaw;
                interaction_placed=1;active=0;break;
            }
        } else {
            m->pos[0]=-1328;m->pos[1]=270;m->pos[2]=4664;active=0;
            int shield=!strcmp(test,"shield");
            interaction_object=spawn_object_abs_with_rot(gMarioObject,0,shield?MODEL_GOOMBA:MODEL_BREAKABLE_BOX_SMALL,
                shield?bhvGoomba:bhvBreakableBoxSmall,-1328,270,shield?4500:4580,0,0,0);
            interaction_object->parentObj=interaction_object;interaction_placed=1;
        }
        if(interaction_placed)fprintf(stderr,"SMB INTERACTION FIXTURE name=%s x=%.2f y=%.2f z=%.2f\n",test,m->pos[0],m->pos[1],m->pos[2]);
    }
    if(!interaction_placed)return;
    interaction_tick++;
    if(!strcmp(test,"shield")&&(interaction_tick==45||interaction_tick==115)) {
        interaction_object->oPosX=m->pos[0]+20;interaction_object->oPosY=m->pos[1];interaction_object->oPosZ=m->pos[2];
        interaction_object->oIntangibleTimer=0;
    }
    if(m->heldObj) {
        carry_tick++;
        if(bowser&&m->action==ACT_HOLDING_BOWSER){m->intendedMag=64;m->intendedYaw=(s16)(carry_tick*0x3000);}
        if(carry_tick==(bowser?110:40))m->input|=INPUT_B_PRESSED;
    }
    if(interaction_tick%10==0)fprintf(stderr,"SMB INTERACTION TRACE name=%s tick=%u native_frames=%u held=%d carry=%u action=%08x spin=%d object_state=%d shield=%u percent=%.2f\n",test,interaction_tick,frames,m->heldObj!=NULL,carry_tick,m->action,m->angleVel[1],interaction_object?interaction_object->oHeldState:-1,snapshot.shielding,snapshot.percent);
}
static int smb_interaction_input(struct MarioState* m,SmbInput* cmd) {
    if(!smb_interaction_test())return 0;
    cmd->stick_x=cmd->stick_y=0;cmd->buttons=0;
    if(!interaction_placed)return 1;
    const char* test=getenv("SMB_SCENARIO");
    if(!strcmp(test,"heave-ho")||!strcmp(test,"king-bobomb")||!strncmp(test,"cannon",6)) {
        if(strcmp(test,"cannon-range")&&launch_test_frame&&frames>=launch_test_frame+20&&frames<launch_test_frame+24)cmd->buttons=0x208;
        return 1;
    }
    cmd->camera_yaw=!strcmp(test,"bowser")?interaction_object->oMoveAngleYaw*(6.28318530718f/65536.f)+3.14159265f:0;
    if(!strcmp(test,"shield"))cmd->buttons=interaction_tick>=20&&interaction_tick<90?0x20:0;
    else if(interaction_tick>=20&&interaction_tick<25)cmd->buttons=0x10;
    return 1;
}
