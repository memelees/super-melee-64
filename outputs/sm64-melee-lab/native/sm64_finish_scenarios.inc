/* Bounded live fixtures. No behavior is selected outside explicit smoke runs. */
static const Collision finish_flat_collision[]={
    COL_INIT(),COL_VERTEX_INIT(4),COL_VERTEX(-6000,0,-6000),COL_VERTEX(-6000,0,6000),
    COL_VERTEX(6000,0,6000),COL_VERTEX(6000,0,-6000),COL_TRI_INIT(SURFACE_DEFAULT,2),
    COL_TRI(0,1,2),COL_TRI(0,2,3),COL_TRI_STOP(),COL_END()
};
static void finish_flat_tick(void){gCurrentObject->collisionData=(Collision*)finish_flat_collision;gCurrentObject->oCollisionDistance=20000;gCurrentObject->oDistanceToMario=0;load_object_collision_model();}
static const BehaviorScript finish_flat_behavior[]={((uintptr_t)OBJ_LIST_SURFACE<<16),0x08000000,0x0c000000,(uintptr_t)finish_flat_tick,0x09000000};
static void smb_finish_prepare(struct MarioState* m) {
    if(!getenv("SMB_SMOKE"))return;
    const char* test=getenv("SMB_SCENARIO");if(!test)return;
    static unsigned phase;
    if(!strcmp(test,"npc-environment")||!strcmp(test,"npc-lava"))smb_host_npc_environment_test(m);
    if((!strcmp(test,"directions")||!strcmp(test,"coop-capacity"))&&!phase) {
        spawn_object_abs_with_rot(gMarioObject,0,MODEL_NONE,finish_flat_behavior,0,6000,0,0,0,0);phase=1;
    }
    if(!strcmp(test,"coop-capacity")&&frames>12&&embedded_remote) {
        static const unsigned characters[15]={38,6,8,2,20,5,12,4,3,16,17,11,0,1,9};
        static const unsigned kinds[15]={37,6,0,1,22,5,9,4,24,13,14,8,2,3,18};
        for(unsigned i=0;i<15;i++) {
            SmbRemote state={0};state.version=SMB_MAGIC;state.character=characters[i];state.kind=kinds[i];state.motion=14;
            state.frame=frames%60;state.percent=i*10;state.position[0]=sinf(i*6.2831853f/15)*35;
            state.position[1]=600.2f;state.position[2]=cosf(i*6.2831853f/15)*35;state.facing[0]=1;
            int result=embedded_remote(i+1,frames<180?&state:NULL);
            if(result){fprintf(stderr,"SMB COOP CAPACITY FAIL peer=%u code=%d\n",i+1,result);exit(2);}
        }
        if(frames==15)fprintf(stderr,"SMB COOP CAPACITY players=16 native_slots=4..18\n");
        if(frames==180)fprintf(stderr,"SMB COOP CAPACITY removed=15\n");
        if(frames<20){m->pos[0]=0;m->pos[1]=6002;m->pos[2]=0;active=0;}
    }
    if(!strcmp(test,"npc-tall")) {
        if(!phase&&frames>12){extern void initiate_warp(s16,s16,s16,s32);initiate_warp(LEVEL_BOB,1,0x0a,0);phase=1;active=0;return;}
        if(phase==1&&gCurrLevelNum==LEVEL_BOB){m->pos[0]=1600;m->pos[2]=-5500;m->pos[1]=find_floor(1600,6000,-5500,&m->floor)+2;active=0;phase=2;}
        if(phase==2&&frames>65){int result=smb_host_spawn_enemy(2,1,9);fprintf(stderr,"SMB HIGH NPC SPAWN code=%d y=%.1f\n",result,m->pos[1]);if(!result)phase=3;}
        if(phase==3&&frames%10==0){SmbNpc npc;embedded_npc(1,NULL,&npc);fprintf(stderr,"SMB HIGH NPC frame=%u y=%.1f motion=%u hits=%u target=%u percent=%.1f\n",frames,npc.position[1]*10,npc.motion,npc.hitboxes,npc.target,snapshot.percent);}
    }
    if(!strncmp(test,"swim",4)) {
        if(!phase&&frames>12){extern void initiate_warp(s16,s16,s16,s32);initiate_warp(LEVEL_JRB,1,0x0a,0);phase=1;active=0;return;}
        if(phase==1&&gCurrLevelNum==LEVEL_JRB) {
            for(int x=-5000;x<=5000;x+=250)for(int z=-5000;z<=5000;z+=250) {
                float water=find_water_level(x,z);struct Surface* floor=NULL;float y=find_floor(x,water,z,&floor);
                if(water< -10000||!floor||water-y<(!strcmp(test,"swim-cpus")?2500:800)||floor->type==SURFACE_DEATH_PLANE)continue;
                m->pos[0]=x;m->pos[1]=water-(!strcmp(test,"swim-cpus")?1600:300);m->pos[2]=z;m->waterLevel=water;
                air_remaining=!strcmp(test,"swim-drown")?0x110:0x880;active=0;set_mario_action(m,ACT_WATER_IDLE,0);phase=2;
                fprintf(stderr,"SMB WATER FIXTURE x=%d y=%.1f z=%d floor=%.1f surface=%.1f\n",x,m->pos[1],z,y,water);return;
            }
        }
        if(!strcmp(test,"swim-cpus")&&phase==2&&frames>55) {
            /* One CPU pair shares the player's real JRB water volume. */
            float point[3]={m->pos[0]*.1f+16,m->pos[1]*.1f,m->pos[2]*.1f};
            if(embedded_npc_add(1,14,0,1,point)){fprintf(stderr,"SMB WATER CPU FIXTURE FAIL spawn\n");exit(2);}
            manual_npcs=1;manual_course=gCurrCourseNum;manual_level=gCurrLevelNum;encounter_count=1;encounter_defeated=0;
            npc_ready[0]=1;npc_last_reset[0]=frames;SDL_setenv("SMB_CPU_COUNT","1",1);smb_npc_water_reset(1);
            fprintf(stderr,"SMB WATER CPU FIXTURE primary=1 nana=1\n");phase=3;
        }
        if(!strcmp(test,"swim-cpus")&&phase==3&&frames>=80) {
            SmbHit hit={0};hit.source=123;hit.damage=8;hit.growth=100;hit.base=60;
            hit.kb_angle=45;hit.radius=20;hit.severity=1;hit.sound=1;
            npc_air_valid|=3;npc_air[0][0]=npc_air[0][1]=1000;
            npc_air_hurt[0][0]=npc_air_hurt[0][1]=0;
            for(unsigned sub=0;sub<2;sub++)if(embedded_actor_world_hit(1,sub,&hit,1)!=1) {
                fprintf(stderr,"SMB WATER CPU FIXTURE FAIL damage sub=%u\n",sub);exit(2);
            }
            phase=11;
        }
        if(!strcmp(test,"swim-cpus")&&phase==11&&frames>=84) {
            int pass=npc_air[0][0]==744&&npc_air[0][1]==744&&
                npc_air_hurt[0][0]==0&&npc_air_hurt[0][1]==0;
            fprintf(stderr,"SMB WATER CPU AIR DAMAGE pass=%d popo=%u nana=%u\n",pass,npc_air[0][0],npc_air[0][1]);
            if(!pass)exit(2);phase=12;
        }
        if(!strcmp(test,"swim-cpus")&&phase==12&&frames>95) {
            /* Exercise independent Nana drowning without waiting a minute. */
            npc_air_valid|=3;npc_air[0][1]=0x101;phase=4;
        }
        if(!strcmp(test,"swim-cpus")&&phase==4&&frames>120) {
            SmbFighterState p={0},n={0};int popo=embedded_actor_state(1,0,&p),nana=embedded_actor_state(1,1,&n);
            fprintf(stderr,"SMB WATER PARTNER DROWN popo_alive=%d nana_retired=%d\n",!popo,nana!=0);
            if(popo||!nana){fprintf(stderr,"SMB WATER CPU FIXTURE FAIL partner\n");exit(2);}
            npc_air_valid|=1;npc_air[0][0]=0x101;phase=5;
        }
        if(!strcmp(test,"swim-cpus")&&phase==5&&frames>145) {
            fprintf(stderr,"SMB WATER CPU LEDGER defeated=%u ko_serial=%u\n",encounter_defeated,npc_ko_serial[0]);phase=6;
        }
    } else if(!strcmp(test,"shell-ride")) {
        if(!phase&&frames>20) {
            struct Object* shell=spawn_object_abs_with_rot(gMarioObject,0,MODEL_KOOPA_SHELL,bhvKoopaShell,
                m->pos[0],m->pos[1]+10,m->pos[2],0,0,0);
            shell->oIntangibleTimer=0;m->action=ACT_WALKING;m->forwardVel=20;phase=1;
            fprintf(stderr,"SMB SHELL FIXTURE spawned=1\n");
        }
        if(phase&&frames%15==0)fprintf(stderr,"SMB SHELL TRACE frame=%u riding=%d object=%d action=%08x x=%.1f y=%.1f z=%.1f\n",frames,!!(m->action&ACT_FLAG_RIDING_SHELL),m->riddenObj!=NULL,m->action,m->pos[0],m->pos[1],m->pos[2]);
    } else if(!strcmp(test,"wing-flight")) {
        if(!phase&&frames>20) {
            m->flags|=MARIO_WING_CAP|MARIO_CAP_ON_HEAD;m->capTimer=1200;m->pos[1]+=1200;
            m->forwardVel=32;m->vel[1]=10;m->faceAngle[0]=0;set_mario_action(m,ACT_FLYING,0);phase=1;
        }
        if(phase&&frames%15==0)fprintf(stderr,"SMB WING TRACE frame=%u flying=%d x=%.1f y=%.1f z=%.1f\n",frames,m->action==ACT_FLYING,m->pos[0],m->pos[1],m->pos[2]);
    }
}
static void smb_finish_input(struct MarioState* m,SmbInput* cmd) {
    const char* test=getenv("SMB_SCENARIO");if(!getenv("SMB_SMOKE")||!test)return;
    if(!strncmp(test,"swim",4)) {
        unsigned t=frames;cmd->stick_x=cmd->stick_y=0;cmd->buttons=0;
        if(!strcmp(test,"swim-drown")||!strcmp(test,"swim-cpus"))return;
        if(t>=65&&t<115)cmd->stick_y=.8f;
        if(t>=125&&t<129)cmd->buttons=0x208;
        if(t>=205&&t<209)cmd->buttons=0x240;
        if(t>=255&&t<285){cmd->stick_y=.6f;cmd->buttons=0x400;}
        if(t%5==0)fprintf(stderr,"SMB WATER TRACE frame=%u y=%.3f vy=%.3f motion=%u air=%d flags=%u\n",t,m->pos[1],snapshot.vy,snapshot.motion,air_remaining,cmd->flags);
    } else if(!strcmp(test,"directions")) {
        unsigned phase=frames/120,t=frames%120;cmd->camera_yaw=phase*1.5707963268f;
        cmd->stick_x=0;cmd->stick_y=t>=15&&t<85?1:0;cmd->buttons=t==45?0x100:0;
        if(t<5){active=0;m->pos[0]=0;m->pos[1]=6002;m->pos[2]=0;}
        if(t>=44&&t<75)fprintf(stderr,"SMB DIRECTION phase=%u frame=%u x=%.3f z=%.3f vx=%.4f vz=%.4f fx=%.4f fz=%.4f motion=%u\n",phase,t,snapshot.x,snapshot.z,snapshot.vx,snapshot.vz,snapshot.facing[0],snapshot.facing[2],snapshot.motion);
    }
    if(!strcmp(test,"coop-capacity")){cmd->buttons=0;cmd->stick_x=cmd->stick_y=0;}
    if(!strcmp(test,"npc-tall")){cmd->buttons=0x20;cmd->stick_x=cmd->stick_y=0;}
}
