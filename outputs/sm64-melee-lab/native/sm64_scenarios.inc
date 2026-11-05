#include "game/object_helpers.h"
#include "behavior_data.h"
#include "model_ids.h"
#include "game/save_file.h"
#include "course_table.h"
#include "sm64_platform_fixture.inc"
/* Real door and painting interactions, then valid same-level area warps.
 * Fixtures position the player near entrances; CPUs use production following. */
static void smb_npc_follow_test(struct MarioState* m,SmbInput* cmd) {
    static unsigned phase,start,room_entered;
    static int positioned,from_room,to_room;
    static s16 yaw;
    if(phase==0&&gCurrLevelNum==LEVEL_CASTLE_GROUNDS) {
        if(!positioned)for(unsigned i=0;i<OBJECT_POOL_CAPACITY;i++) {
            struct Object* o=&gObjectPool[i];
            if(!(o->activeFlags&ACTIVE_FLAG_ACTIVE)||!(o->oInteractType&INTERACT_WARP_DOOR)||o->oPosY<0)continue;
            yaw=o->oMoveAngleYaw;m->pos[0]=o->oPosX+sins(yaw)*250;m->pos[1]=o->oPosY;m->pos[2]=o->oPosZ+coss(yaw)*250;
            active=0;positioned=1;break;
        }
        cmd->camera_yaw=yaw*(6.28318530718f/65536.f);cmd->stick_y=.95f;return;
    }
    if(phase==0&&gCurrLevelNum==LEVEL_CASTLE){phase=1;positioned=0;start=frames;}
    if(phase==1&&gCurrLevelNum==LEVEL_CASTLE) {
        if(!positioned)for(unsigned i=0;i<OBJECT_POOL_CAPACITY;i++) {
            struct Object* o=&gObjectPool[i];
            if(!(o->activeFlags&ACTIVE_FLAG_ACTIVE)||!(o->oInteractType&INTERACT_DOOR)||fabsf(o->oPosX+3122)>30)continue;
            yaw=o->oMoveAngleYaw;
            struct Surface *a=NULL,*b=NULL;
            float px=o->oPosX+sins(yaw)*250,pz=o->oPosZ+coss(yaw)*250;
            float py=find_floor(px,o->oPosY+200,pz,&a);
            find_floor(o->oPosX-sins(yaw)*250,o->oPosY+200,o->oPosZ-coss(yaw)*250,&b);
            if(!a||!b||a->room<=0||b->room<=0||a->room==b->room)continue;
            from_room=a->room;to_room=b->room;m->pos[0]=px;m->pos[1]=py+2;m->pos[2]=pz;active=0;cmd->flags|=4;
            positioned=1;start=frames;
            fprintf(stderr,"SMB FOLLOW DOOR level=%d area=%d from=%d to=%d object=%u\n",gCurrLevelNum,gCurrentArea->index,from_room,to_room,i);break;
        }
        if(!positioned){fprintf(stderr,"SMB HOST FAIL follow room fixture missing\n");exit(2);}
        if(m->floor&&m->floor->room==to_room&&!room_entered) {
            room_entered=frames;
            fprintf(stderr,"SMB FOLLOW ROOM ENTER frame=%u room=%d actual_door=1\n",frames,to_room);
        }
        cmd->camera_yaw=yaw*(6.28318530718f/65536.f);cmd->stick_y=room_entered?0:.95f;
        if(room_entered)cmd->buttons=0x20;
        if(room_entered&&frames-room_entered>=120){phase=2;positioned=0;}
        if(!room_entered&&frames-start>150){fprintf(stderr,"SMB HOST FAIL follow room door not crossed\n");exit(2);}
        return;
    }
    if(phase==2&&gCurrLevelNum==LEVEL_CASTLE&&!positioned&&gCurrentArea->paintingWarpNodes) {
        for(int i=0;i<gSurfacesAllocated;i++) {
            struct Surface* s=&sSurfacePool[i];int n=s->type-SURFACE_PAINTING_WARP_D3;
            if(n<0||n>=45||gCurrentArea->paintingWarpNodes[n].destLevel!=LEVEL_BOB)continue;
            m->pos[0]=(s->vertex1[0]+s->vertex2[0]+s->vertex3[0])/3.f;
            m->pos[1]=(s->vertex1[1]+s->vertex2[1]+s->vertex3[1])/3.f+1;
            m->pos[2]=(s->vertex1[2]+s->vertex2[2]+s->vertex3[2])/3.f;
            active=0;positioned=1;fprintf(stderr,"SMB FOLLOW PAINTING surface=%d\n",i);break;
        }
        return;
    }
    if(phase==2&&gCurrLevelNum==LEVEL_BOB){phase=3;start=frames;}
    if(phase==4&&gCurrLevelNum==LEVEL_CASTLE&&gCurrentArea->index==2){phase=5;start=frames;}
    if(phase==6&&gCurrLevelNum==LEVEL_CASTLE&&gCurrentArea->index==3){phase=7;start=frames;}
    if(phase==8&&gCurrLevelNum==LEVEL_BOB){phase=9;fprintf(stderr,"SMB FOLLOW ROUTE COMPLETE frame=%u\n",frames);}
    if(phase>=3)cmd->buttons=0x20;
    int arrived=(phase==3&&gCurrLevelNum==LEVEL_BOB)||
        (phase==5&&gCurrLevelNum==LEVEL_CASTLE&&gCurrentArea->index==2)||
        (phase==7&&gCurrLevelNum==LEVEL_CASTLE&&gCurrentArea->index==3);
    if(arrived&&frames-start>=150) {
        extern void initiate_warp(s16,s16,s16,s32);
        int area=phase==3?2:3,level=phase==7?LEVEL_BOB:LEVEL_CASTLE;
        if(phase==7)area=1;
        fprintf(stderr,"SMB FOLLOW WARP frame=%u from_level=%d from_area=%d to_level=%d to_area=%d\n",frames,gCurrLevelNum,gCurrentArea->index,level,area);
        initiate_warp(level,area,phase==7?0x0a:0,0);active=0;phase++;
    }
}
/* Scripted integration checks only; never enabled by the normal launcher. */
static void smb_scenario(struct MarioState* m,SmbInput* cmd) {
    const char* test=getenv("SMB_SCENARIO");if(!test)return;
    static int door_started,painting_started,last_level=-1;
    if(last_level!=gCurrLevelNum){last_level=gCurrLevelNum;fprintf(stderr,"SMB SCENARIO LEVEL name=%s level=%d frames=%u\n",test,gCurrLevelNum,frames);}
    if(!strcmp(test,"controls")||!strcmp(test,"controller"))return;
    cmd->stick_x=cmd->stick_y=0;cmd->buttons=0;
    if(!strcmp(test,"stage-stars")) {
        static unsigned round,phase,entered,generation;
        /* Isolate progression and post-menu controls from CPU attacks. Holding
         * shield through three fixtures would eventually cause shield break. */
        if(embedded_npc_wait)for(unsigned i=1;i<=3;i++)embedded_npc_wait(i);
        if(gCurrLevelNum!=LEVEL_BOB)return;
        if(!phase){entered=frames;phase=1;}
        if(phase==1&&frames>=entered+40&&round<3) {
            m->pos[0]=-6500;m->pos[1]=20;m->pos[2]=6200;active=0;cmd->flags=4;
            struct Object* star=spawn_object_abs_with_rot(gMarioObject,0,MODEL_STAR,bhvStar,-6500,40,6200,0,0,0);
            star->oBehParams=(round==2?6:round)<<24;star->parentObj=star;
            if(round==2)star->oInteractionSubtype|=INT_SUBTYPE_NO_EXIT;
            generation=world_generation;phase=2;
            fprintf(stderr,"SMB STAGE STAR COLLECT round=%u index=%u act=%d\n",round,round==2?6:round,gCurrActNum);
        }
        if(phase==2&&world_generation!=generation) {
            fprintf(stderr,"SMB STAGE STAR REENTER round=%u act=%d flags=%02x stars=%d\n",round,gCurrActNum,save_file_get_star_flags(gCurrSaveFileNum-1,COURSE_BOB-1),m->numStars);
            round++;entered=frames;phase=round<3?1:3;
        }
        if(phase==3) {
            unsigned t=frames-entered;
            if(t<25)cmd->stick_x=.7f;
            if(t>=30&&t<34)cmd->buttons=0x400;
            if(t>=48&&t<52)cmd->buttons=0x100;
            if(t%10==0)fprintf(stderr,"SMB STAGE STAR CONTROL tick=%u motion=%u hits=%u grounded=%u\n",t,snapshot.motion,snapshot.hitboxes,snapshot.grounded);
        }
        return;
    }
    if(!strcmp(test,"stage")) {
        unsigned stage=getenv("SMB_START_STAGE")?atoi(getenv("SMB_START_STAGE")):99;
        if(stage<27&&gCurrLevelNum==(int)stage_starts[stage].level&&frames%30==0)
            fprintf(stderr,"SMB STAGE READY stage=%u level=%d kind=%u frame=%u\n",stage,gCurrLevelNum,snapshot.kind,frames);
        cmd->buttons=0x20;
        return;
    }
    if(!strcmp(test,"wing-entrance")) {
        static int phase;
        if(!phase){extern void initiate_warp(s16,s16,s16,s32);initiate_warp(LEVEL_CASTLE,1,0,0);active=0;phase=1;return;}
        if(phase==1&&gCurrLevelNum==LEVEL_CASTLE)for(int i=0;i<gSurfacesAllocated;i++) {
            struct Surface* f=&sSurfacePool[i];if(f->type!=SURFACE_LOOK_UP_WARP)continue;
            m->pos[0]=(f->vertex1[0]+f->vertex2[0]+f->vertex3[0])/3.f;
            m->pos[1]=(f->vertex1[1]+f->vertex2[1]+f->vertex3[1])/3.f+2;
            m->pos[2]=(f->vertex1[2]+f->vertex2[2]+f->vertex3[2])/3.f;active=0;cmd->flags=4;phase=2;break;
        }
        smb_host_key(SDL_SCANCODE_UP,phase==2);
        if(gCurrLevelNum==LEVEL_TOTWC&&phase==2){smb_host_key(SDL_SCANCODE_UP,0);fprintf(stderr,"SMB CAMERA WING ENTRY complete=1\n");phase=3;}
        return;
    }
    if(!strcmp(test,"camera-follow")) {
        /* Replay a stale under-player/overhead Lakitu history, then real native
         * jumping and recovery. This is only enabled by an explicit test run. */
        if(frames==40||frames==100||frames==190) {
            if(frames==190){m->pos[1]+=1500;active=0;cmd->flags=4;}
            set_camera_mode(gCamera,CAMERA_MODE_FREE_ROAM,1);
            float y=m->pos[1]+125+(frames==100?2200:-1400);
            gCamera->pos[1]=gLakituState.pos[1]=gLakituState.curPos[1]=gLakituState.goalPos[1]=y;
            gCamera->focus[1]=gLakituState.focus[1]=gLakituState.curFocus[1]=gLakituState.goalFocus[1]=m->pos[1]+125;
            fprintf(stderr,"SMB CAMERA FOLLOW FIXTURE frame=%u side=%s player_y=%.1f\n",frames,frames==100?"above":"below",m->pos[1]);
        }
        if(frames>=145&&frames<148)cmd->buttons=0x400;
        if(frames>=156&&frames<159)cmd->buttons=0x208;
        return;
    }
    if(!strcmp(test,"camera-look")) {
        unsigned t=frames%180;
        smb_host_key(SDL_SCANCODE_UP,t>=20&&t<50);
        smb_host_key(SDL_SCANCODE_DOWN,t>=70&&t<100);
        if(t==49||t==99)fprintf(stderr,"SMB CAMERA LOOK peak=%.5f tick=%u\n",camera_pitch,t);
        if(t>=165)fprintf(stderr,"SMB CAMERA LOOK neutral=%d first_person=%d pitch=%.6f mode=%d\n",fabsf(camera_pitch)<.001f,!!(gCameraMovementFlags&CAM_MOVE_C_UP_MODE),camera_pitch,gCamera->mode);
        return;
    }
    if(!strcmp(test,"entrance-snow")||!strcmp(test,"entrance-lava")) {
        static int phase;static unsigned entered,jumped;static float target[3];
        int target_level=!strcmp(test,"entrance-snow")?LEVEL_CCM:LEVEL_LLL;
        if(!phase) {extern void initiate_warp(s16,s16,s16,s32);initiate_warp(LEVEL_CASTLE,target_level==LEVEL_LLL?3:1,0,0);active=0;phase=1;return;}
        if(phase==1&&gCurrLevelNum==LEVEL_CASTLE&&frames==40) {
            fprintf(stderr,"SMB ENTRANCE DEBUG area=%d nodes=%p surfaces=%d\n",gCurrentArea->index,(void*)gCurrentArea->paintingWarpNodes,gSurfacesAllocated);
            for(int k=0;k<45;k++)if(gCurrentArea->paintingWarpNodes)fprintf(stderr,"SMB ENTRANCE NODE index=%d level=%d\n",k,gCurrentArea->paintingWarpNodes[k].destLevel);
        }
        if(phase==1&&gCurrLevelNum==LEVEL_CASTLE&&gCurrentArea->paintingWarpNodes) {
            for(int i=0;i<gSurfacesAllocated;i++) {
                struct Surface* f=&sSurfacePool[i];int n=f->type-SURFACE_PAINTING_WARP_D3;
                if(n<0||n>=45||(gCurrentArea->paintingWarpNodes[n].destLevel&127)!=target_level)continue;
                target[0]=(f->vertex1[0]+f->vertex2[0]+f->vertex3[0])/3.f;
                target[1]=(f->vertex1[1]+f->vertex2[1]+f->vertex3[1])/3.f;
                target[2]=(f->vertex1[2]+f->vertex2[2]+f->vertex3[2])/3.f;
                /* Wobble sensors immediately border their painting's warp
                 * floor. Start on the room side, walk through the real face. */
                struct Surface* face=NULL;float closest=1e20f;
                for(int j=0;j<gSurfacesAllocated;j++) {
                    struct Surface* w=&sSurfacePool[j];
                    if(fabsf(w->normal.y)>.01f||w->type!=SURFACE_CAMERA_BOUNDARY)continue;
                    float x=(w->vertex1[0]+w->vertex2[0]+w->vertex3[0])/3.f,z=(w->vertex1[2]+w->vertex2[2]+w->vertex3[2])/3.f;
                    float d=hypotf(x-target[0],z-target[2]);if(d<closest){closest=d;face=w;}
                }
                if(!face) {
                    /* The wobble strip is a floor sensor, not a wall. */
                    float distance=1e20f;struct Surface* strip=NULL;
                    for(int j=0;j<gSurfacesAllocated;j++) {
                        struct Surface* w=&sSurfacePool[j];if(w->type<SURFACE_PAINTING_WOBBLE_A6||w->type>SURFACE_PAINTING_WOBBLE_D2)continue;
                        float x=(w->vertex1[0]+w->vertex2[0]+w->vertex3[0])/3.f,z=(w->vertex1[2]+w->vertex2[2]+w->vertex3[2])/3.f;
                        float d=hypotf(x-target[0],z-target[2]);if(d<distance){distance=d;strip=w;}
                    }
                    if(!strip)continue;
                    static struct Surface toward_room;
                    memset(&toward_room,0,sizeof toward_room);
                    float dx=(strip->vertex1[0]+strip->vertex2[0]+strip->vertex3[0])/3.f-target[0];
                    float dz=(strip->vertex1[2]+strip->vertex2[2]+strip->vertex3[2])/3.f-target[2];
                    float length=hypotf(dx,dz);toward_room.normal.x=dx/length;toward_room.normal.z=dz/length;
                    toward_room.vertex1[0]=target[0]+dx;toward_room.vertex1[2]=target[2]+dz;face=&toward_room;
                }
                float distance=face->normal.x*(target[0]-face->vertex1[0])+face->normal.z*(target[2]-face->vertex1[2]);
                m->pos[0]=target[0]+face->normal.x*(220-distance);m->pos[2]=target[2]+face->normal.z*(220-distance);
                struct Surface* floor=NULL;float y=find_floor(m->pos[0],target[1]+200,m->pos[2],&floor);
                m->pos[1]=floor?y+10:target[1]+10;active=0;cmd->flags=4;phase=2;
                fprintf(stderr,"SMB ENTRANCE FIXTURE target=%d face=%x normal=%.2f,%.2f x=%.1f y=%.1f z=%.1f aim=%.1f,%.1f\n",target_level,face->type,face->normal.x,face->normal.z,m->pos[0],m->pos[1],m->pos[2],target[0],target[2]);break;
            }
        }
        if(phase==2&&gCurrLevelNum==LEVEL_CASTLE){cmd->camera_yaw=(float)atan2((double)m->pos[0]-target[0],(double)m->pos[2]-target[2]);cmd->stick_y=.9f;
            if(!jumped&&hypotf(m->pos[0]-target[0],m->pos[2]-target[2])<440)jumped=frames;
            if(jumped&&frames-jumped<4)cmd->buttons=0x400;
        }
        if(gCurrLevelNum==target_level){if(!entered)entered=frames;if(phase!=3&&frames-entered>=100){fprintf(stderr,"SMB ENTRANCE ENTERED target=%d frames=%u\n",target_level,frames);phase=3;}}
        return;
    }
    if(!strcmp(test,"no-fast-fall")) {
        unsigned phase=frames/100,t=frames%100;
        if(!t){m->pos[0]=-1328;m->pos[1]=270;m->pos[2]=4664;active=0;cmd->flags=4;}
        if(t>=20&&t<24)cmd->buttons=0x400;
        if((phase==1&&t>=30)||(phase==2&&t>=50))cmd->buttons|=0x40;
        return;
    }
    if(!strcmp(test,"ground-pound")) {
        static int entered,placed;static unsigned start;static struct Object* post;
        if(!entered){extern void initiate_warp(s16,s16,s16,s32);initiate_warp(LEVEL_BOB,1,0x0a,0);entered=1;active=0;return;}
        if(gCurrLevelNum!=LEVEL_BOB)return;
        if(!placed&&frames>20)for(unsigned i=0;i<OBJECT_POOL_CAPACITY;i++) {
            struct Object* o=&gObjectPool[i];if(!(o->activeFlags&ACTIVE_FLAG_ACTIVE)||o->behavior!=segmented_to_virtual(bhvWoodenPost))continue;
            post=o;m->pos[0]=o->oPosX;m->pos[2]=o->oPosZ;
            m->pos[1]=o->oPosY+410*o->header.gfx.scale[1]+150;
            active=0;cmd->flags=4;placed=1;start=frames;
            fprintf(stderr,"SMB POUND FIXTURE post=%u level=%d start_y=%.1f top=%.1f\n",i,gCurrLevelNum,m->pos[1],m->floorHeight);break;
        }
        if(!placed)return;
        unsigned t=frames-start;
        if(t>=3&&t<8)cmd->buttons=0x140;
        if(post&&t%5==0)fprintf(stderr,"SMB POUND POST tick=%u offset=%.1f pounding=%d platform=%d action=%08x motion=%u down=%u\n",t,post->oWoodenPostOffsetY,post->oWoodenPostMarioPounding,m->floor&&m->floor->object==post,m->action,snapshot.motion,(snapshot.abilities&SMB_DOWN_ATTACK)!=0);
        return;
    }
    if(!strcmp(test,"powerups")) {
        unsigned stage=frames/40,flags=stage==1?MARIO_METAL_CAP:stage==2?MARIO_VANISH_CAP:stage==3?MARIO_METAL_CAP|MARIO_VANISH_CAP:0;
        m->flags=(m->flags&~(MARIO_METAL_CAP|MARIO_VANISH_CAP))|flags;m->capTimer=1000;
        if(embedded_powerups)embedded_powerups(((flags&MARIO_METAL_CAP)?1:0)|((flags&MARIO_VANISH_CAP)?2:0));
        if(frames%40==0)fprintf(stderr,"SMB CAP FIXTURE stage=%u metal=%d vanish=%d\n",stage,!!(flags&MARIO_METAL_CAP),!!(flags&MARIO_VANISH_CAP));
        return;
    }
    if(!strcmp(test,"character-switch")) {
        if(frames==50)m->numCoins=7;
        if(frames==175&&smb_course_enemies())SDL_setenv("SMB_TEST_ENCOUNTER_HIT","1",1);
        unsigned elapsed=frames-character_changed_at;
        if(character_changed_at&&elapsed>=10&&elapsed<35)cmd->stick_y=.7f;
        if(character_changed_at&&elapsed>=40&&elapsed<44)cmd->buttons=0x400;
        if(character_changed_at&&elapsed>=60&&elapsed<64)cmd->buttons=0x100;
        if(frames%30==0)fprintf(stderr,"SMB SWITCH CONTROL frame=%u elapsed=%u kind=%u motion=%u hits=%u grounded=%u percent=%.1f defeated=%u\n",frames,elapsed,snapshot.kind,snapshot.motion,snapshot.hitboxes,snapshot.grounded,snapshot.percent,encounter_defeated);
        return;
    }
    if(!strcmp(test,"star-return")) {
        static unsigned round,phase,start;static float origin[3];
        if(phase==0){extern void initiate_warp(s16,s16,s16,s32);initiate_warp(LEVEL_BOB,1,0x0a,0);active=0;phase=1;}
        if(phase==1&&gCurrLevelNum==LEVEL_BOB) {
            float p[3]={-6500,20,6200};memcpy(m->pos,p,sizeof p);active=0;cmd->flags=4;
            struct Object* star=spawn_object_abs_with_rot(gMarioObject,0,MODEL_STAR,bhvStar,-6500,40,6200,0,0,0);
            star->oBehParams=(round%6)<<24;star->parentObj=star;phase=2;
            fprintf(stderr,"SMB STAR RETURN COLLECT round=%u\n",round);
        }
        if(phase==2&&gCurrLevelNum==LEVEL_CASTLE){phase=3;start=frames;memcpy(origin,m->pos,sizeof origin);}
        if(phase==3) {
            unsigned t=frames-start;cmd->camera_yaw=0;
            if(t<24)cmd->stick_x=.7f;
            if(t>=30&&t<34)cmd->buttons=0x400;
            if(t>=48&&t<52)cmd->buttons=0x100;
            if(t%5==0)fprintf(stderr,"SMB STAR RETURN CONTROL round=%u tick=%u x=%.2f y=%.2f z=%.2f motion=%u grounded=%u hits=%u active=%d delta=%.2f\n",round,t,m->pos[0],m->pos[1],m->pos[2],snapshot.motion,snapshot.grounded,snapshot.hitboxes,active,hypotf(m->pos[0]-origin[0],m->pos[2]-origin[2]));
            if(t>=100&&round<2){round++;phase=0;}
        }
        return;
    }
    if(!strcmp(test,"encounter-launch")) {
        static unsigned phase,start,recall;
        cmd->buttons=0x20;
        if(!phase){extern void initiate_warp(s16,s16,s16,s32);initiate_warp(LEVEL_BOB,1,0x0a,0);active=0;phase=1;}
        if(phase==1&&gCurrLevelNum==LEVEL_BOB&&npc_ready[0]){start=frames;phase=2;}
        if(phase==2) {
            if(frames==start+20){setenv("SMB_TEST_HIT_PERCENT","0",1);setenv("SMB_TEST_ENCOUNTER_HIT","1",1);}
            if(frames==start+25){smb_host_npc_queue(4);recall=frames;}
            if(frames%10==0){SmbNpc state;embedded_npc(1,NULL,&state);fprintf(stderr,"SMB LAUNCH REGRESSION frame=%u elapsed=%u ready=%u ground=%u motion=%u defeated=%u percent=%.1f recall=%u\n",frames,frames-start,npc_ready[0],state.grounded,state.motion,encounter_defeated,state.percent,recall);}
            if(frames>start+230&&npc_ready[0]) {
                fprintf(stderr,"SMB LAUNCH REGRESSION SURVIVED defeated=%u\n",encounter_defeated);
                setenv("SMB_TEST_HIT_PERCENT","500",1);setenv("SMB_TEST_HIT_ANGLE","20",1);setenv("SMB_TEST_ENCOUNTER_HIT","2",1);phase=3;
            }
        }
        if(phase==3&&encounter_defeated){fprintf(stderr,"SMB LAUNCH REGRESSION HORIZONTAL_KO frame=%u\n",frames);phase=4;}
        return;
    }
    if(!strcmp(test,"campaign-waves")) {
        static unsigned phase,start,hit_serial,last_wave=99;
        cmd->buttons=0x20;
        if(!phase&&frames>110){extern void initiate_warp(s16,s16,s16,s32);initiate_warp(LEVEL_TTC,1,0x0a,0);active=0;phase=1;}
        if(phase==1&&gCurrCourseNum==COURSE_TTC) {
            if(encounter_wave!=last_wave){last_wave=encounter_wave;start=0;}
            if(npc_ready[0]&&!start)start=frames;
            if(start&&frames-start==25){char n[20];snprintf(n,sizeof n,"%u",++hit_serial);setenv("SMB_TEST_ENCOUNTER_HIT",n,1);}
            if(encounter_wave==2&&encounter_defeated==1){fprintf(stderr,"SMB CAMPAIGN WAVES COMPLETE waves=3\n");phase=2;}
        }
        return;
    }
    if(!strcmp(test,"ground-hud")) {
        if(frames==75){m->pos[1]+=600;active=0;cmd->flags|=4;}
        return;
    }
    if(!strcmp(test,"course-battle")) {
        static unsigned phase,start,hit,ko_frame;
        cmd->buttons=0x20; /* Allow CPU combat, keep the host human safe. */
        if(phase==0) {extern void initiate_warp(s16,s16,s16,s32);initiate_warp(LEVEL_BOB,1,0x0a,0);active=0;phase=1;}
        if(phase==1&&gCurrLevelNum==LEVEL_BOB) {
            if(!start&&npc_ready[0])start=frames;
            if(start&&frames-start>30&&!hit&&npc_ready[0]){setenv("SMB_TEST_ENCOUNTER_HIT","1",1);hit=1;}
            if(encounter_defeated&&!ko_frame)ko_frame=frames;
            if(ko_frame&&frames-ko_frame==100) {
                fprintf(stderr,"SMB ENCOUNTER TEST STAYS_DEAD course=%d defeated=%u native_frames=100\n",gCurrCourseNum,encounter_defeated);
                extern void initiate_warp(s16,s16,s16,s32);initiate_warp(LEVEL_HMC,1,0x0a,0);active=0;phase=2;
            }
        }
        if(phase==2&&gCurrLevelNum==LEVEL_HMC&&npc_ready[1]) {phase=3;start=frames;}
        if(phase==3&&frames-start>=40){extern void initiate_warp(s16,s16,s16,s32);initiate_warp(LEVEL_WDW,1,0x0a,0);active=0;phase=4;}
        if(phase==4&&gCurrLevelNum==LEVEL_WDW&&npc_ready[2]){phase=5;start=frames;}
        if(phase==5&&frames-start>=40){extern void initiate_warp(s16,s16,s16,s32);initiate_warp(LEVEL_CASTLE_GROUNDS,1,0x0a,0);active=0;phase=6;start=frames;}
        if(phase==6&&gCurrLevelNum==LEVEL_CASTLE_GROUNDS&&frames-start>50){extern void initiate_warp(s16,s16,s16,s32);initiate_warp(LEVEL_BOB,1,0x0a,0);active=0;phase=7;}
        if(phase==7&&gCurrLevelNum==LEVEL_BOB&&npc_ready[0]){fprintf(stderr,"SMB ENCOUNTER TEST REENTER course=%d defeated=%u count=%u fresh_yoshi=1\n",gCurrCourseNum,encounter_defeated,encounter_count);phase=8;}
        return;
    }
    if(!strcmp(test,"model-forms")) {
        unsigned tick=frames%240;
        /* Genuine native input: up-special, neutral special, then locomotion. */
        if(tick>=20&&tick<24)cmd->buttons=0x208;
        if(tick>=100&&tick<124)cmd->buttons=0x200;
        if(tick>=170&&tick<210){cmd->stick_y=.6f;cmd->camera_yaw=frames<240?0:3.14159265f;}
        if(frames>=280&&frames<315){cmd->stick_y=.6f;cmd->camera_yaw=3.14159265f;}
        if(frames==240||frames==270){m->pos[0]=-1328;m->pos[1]=260;m->pos[2]=4664;active=0;cmd->flags|=4;}
        return;
    }
    if(!strcmp(test,"npc-catchup")) {
        if(frames<60&&frames%24<4)cmd->buttons=0x100;
        if(frames==60) {
            int found=0,room=0;float point[3];
            for(unsigned i=0;i<32;i++) {
                float angle=i*.19634954f;
                if(smb_npc_safe_floor(m->pos[0]+sinf(angle)*6200,m->pos[2]+cosf(angle)*6200,m->pos[1],0,1500,point,&room)) {
                    for(unsigned axis=0;axis<3;axis++)m->pos[axis]=point[axis]*10;
                    active=0;cmd->flags|=4;found=1;
                    fprintf(stderr,"SMB CATCHUP ESCAPE frame=%u x=%.1f y=%.1f z=%.1f\n",frames,m->pos[0],m->pos[1],m->pos[2]);break;
                }
            }
            if(!found){fprintf(stderr,"SMB HOST FAIL catchup escape fixture\n");exit(2);}
        }
        if(frames>=60)cmd->buttons=0x20;
        return;
    }
    if(!strcmp(test,"npc-follow")){smb_npc_follow_test(m,cmd);return;}
    if(!strcmp(test,"npcs")) {
        if(frames%24<4)cmd->buttons=0x100;
        if(frames%90>=60&&frames%90<64)cmd->buttons=0x208;
        static int bob,wdw;
        if(frames>=120&&!bob){extern void initiate_warp(s16,s16,s16,s32);initiate_warp(LEVEL_BOB,1,0x0a,0);active=0;bob=1;}
        if(frames>=300&&!wdw){extern void initiate_warp(s16,s16,s16,s32);initiate_warp(LEVEL_WDW,1,0x0a,0);active=0;wdw=1;}
        return;
    }
    if(!strcmp(test,"victory-forms")) {
        if((frames>=70&&frames<74)||(frames>=180&&frames<184))cmd->buttons=0x240;
        if((frames==40||frames==140||frames==260)&&embedded_celebrate) {
            float p[3]={m->pos[0]*.1f,m->pos[1]*.1f,m->pos[2]*.1f};
            for(unsigned n=0;n<18;n++)embedded_celebrate(1,p);
            embedded_celebrate(0,p);
            fprintf(stderr,"SMB VICTORY PROBE frame=%u kind=%u\n",frames,snapshot.kind);
        }
        return;
    }
    if(!strcmp(test,"effects")||!strcmp(test,"lighting"))return;
    if(!strcmp(test,"slope")) {
        static int requested;static unsigned start;
        if(gCurrLevelNum!=LEVEL_VCUTM) {
            if(!requested){extern void initiate_warp(s16,s16,s16,s32);initiate_warp(LEVEL_VCUTM,1,0x0a,0);requested=1;active=0;}
            return;
        }
        if(!start)start=frames+1;
        unsigned tick=frames+1-start;
        cmd->camera_yaw=3.14159265f;cmd->stick_y=tick>=30&&tick<70?.75f:0;
        if(getenv("SMB_SLOPE_STEER"))cmd->stick_x=tick>=82&&tick<100?.75f:tick>=100&&tick<125?-.75f:0;
        if(getenv("SMB_SLOPE_JUMP")&&tick>=100&&tick<104)cmd->buttons=0x400;
        fprintf(stderr,"SMB SLOPE seq=%u tick=%u x=%.3f y=%.3f z=%.3f floor=%.3f normal_y=%.5f grounded=%u motion=%u lives=%d\n",sequence,tick,m->pos[0],m->pos[1],m->pos[2],m->floorHeight,m->floor?m->floor->normal.y:0,snapshot.grounded,snapshot.motion,m->numLives);
        return;
    }
    if(!strcmp(test,"respawn")) {
        static int died;static unsigned wait;
        if(!died&&frames>=30){died=1;m->health=0xff;set_mario_action(m,ACT_STANDING_DEATH,0);active=0;}
        if(snapshot.motion==13&&++wait>20)cmd->buttons=0x400;
        fprintf(stderr,"SMB RESPAWN TEST frame=%u motion=%u lives=%d percent=%.2f y=%.2f grounded=%u\n",frames,snapshot.motion,m->numLives,snapshot.percent,m->pos[1],snapshot.grounded);
        return;
    }
    if(!strcmp(test,"reload-save")&&frames%90==0)fprintf(stderr,"SMB SCENARIO SAVE stars=%d flags=%x\n",m->numStars,save_file_get_star_flags(gCurrSaveFileNum-1,COURSE_BOB-1));
    if(!strcmp(test,"doors")||!strcmp(test,"stairs")||!strcmp(test,"progression")||!strcmp(test,"impact")||!strcmp(test,"bomb-impact")||!strcmp(test,"health")||!strcmp(test,"star")||!strcmp(test,"flight-low")||!strcmp(test,"flight-high")) {
        if(!strcmp(test,"star")&&gCurrLevelNum==LEVEL_CASTLE&&m->numStars>0&&frames%30==0)fprintf(stderr,"SMB SCENARIO SAVE stars=%d flags=%x\n",m->numStars,save_file_get_star_flags(gCurrSaveFileNum-1,COURSE_BOB-1));
        if(gCurrLevelNum==LEVEL_CASTLE_GROUNDS) {
            static s16 yaw;
            if(!door_started)for(unsigned i=0;i<OBJECT_POOL_CAPACITY;i++) {
                struct Object* o=&gObjectPool[i];
                if(!(o->activeFlags&ACTIVE_FLAG_ACTIVE)||!(o->oInteractType&INTERACT_WARP_DOOR)||o->oPosY<0)continue;
                yaw=o->oMoveAngleYaw;
                m->pos[0]=o->oPosX+sins(yaw)*250;m->pos[1]=o->oPosY;m->pos[2]=o->oPosZ+coss(yaw)*250;
                active=0;door_started=1;
                fprintf(stderr,"SMB SCENARIO DOOR object=%u x=%.1f y=%.1f z=%.1f yaw=%d\n",i,o->oPosX,o->oPosY,o->oPosZ,yaw);break;
            }
            cmd->camera_yaw=yaw*(6.28318530718f/65536.f);cmd->stick_y=.75f;
        } else if(!strcmp(test,"stairs")&&gCurrLevelNum==LEVEL_CASTLE) {
            static unsigned start;static float point[3],normal[3],rise,run;static int descending,finished;
            if(!start) {
                for(unsigned i=0;i<16&&embedded_stair;i++) {
                    float p[3],n[3],r,u;if(embedded_stair(i,p,n,&r,&u))break;
                    fprintf(stderr,"SMB STAIR SAMPLE ordinal=%u x=%.2f y=%.2f z=%.2f rise=%.2f run=%.2f\n",i,p[0],p[1],p[2],r,u);
                }
                unsigned ordinal=getenv("SMB_STAIR_ORDINAL")?(unsigned)atoi(getenv("SMB_STAIR_ORDINAL")):0;
                if(!embedded_stair||embedded_stair(ordinal,point,normal,&rise,&run)){fprintf(stderr,"SMB HOST FAIL no live castle stair ordinal=%u\n",ordinal);exit(2);}
                m->pos[0]=(point[0]+normal[0]*(run+7))*10;m->pos[2]=(point[2]+normal[2]*(run+7))*10;
                m->pos[1]=point[1]*10+2;active=0;cmd->flags=4;start=frames;
                fprintf(stderr,"SMB SCENARIO STAIR point=%.2f,%.2f,%.2f normal=%.3f,%.3f rise=%.2f run=%.2f\n",point[0],point[1],point[2],normal[0],normal[2],rise,run);
            }
            unsigned tick=frames-start;
            float progress=-(m->pos[0]*.1f-point[0])*normal[0]-(m->pos[2]*.1f-point[2])*normal[2];
            if(progress>=8)descending=1;
            if(descending&&progress<=-(run+7))finished=1;
            cmd->camera_yaw=(float)atan2((double)normal[0],(double)normal[2]);
            cmd->stick_y=tick<5||finished?0:descending?-.45f:.45f;
            fprintf(stderr,"SMB STAIR WALK tick=%u phase=%d height=%.3f progress=%.3f grounded=%u motion=%u\n",tick,finished?2:descending?1:0,m->pos[1]*.1f-point[1],progress,snapshot.grounded,snapshot.motion);
        } else if(strcmp(test,"doors")&&gCurrLevelNum==LEVEL_CASTLE&&!painting_started&&gCurrentArea->paintingWarpNodes) {
            for(int i=0;i<gSurfacesAllocated;i++) {
                struct Surface* surface=&sSurfacePool[i];int n=surface->type-SURFACE_PAINTING_WARP_D3;
                if(n<0||n>=45||gCurrentArea->paintingWarpNodes[n].destLevel!=LEVEL_BOB)continue;
                m->pos[0]=(surface->vertex1[0]+surface->vertex2[0]+surface->vertex3[0])/3.f;
                m->pos[1]=(surface->vertex1[1]+surface->vertex2[1]+surface->vertex3[1])/3.f+1;
                m->pos[2]=(surface->vertex1[2]+surface->vertex2[2]+surface->vertex3[2])/3.f;
                active=0;painting_started=1;fprintf(stderr,"SMB SCENARIO PAINTING surface=%d type=%04x\n",i,surface->type);break;
            }
        } else if((!strcmp(test,"impact")||!strcmp(test,"bomb-impact"))&&gCurrLevelNum==LEVEL_BOB) {
            static unsigned start;static struct Object* victim;static int checked;
            if(!start){start=frames; m->pos[0]=-6500;m->pos[1]=0;m->pos[2]=6200;active=0;
            }
            unsigned tick=frames-start;cmd->camera_yaw=0;
            int bomb=!strcmp(test,"bomb-impact");
            if(tick==(bomb?90:50)) {
                struct Surface* target_floor;float target_z=bomb?m->pos[2]-220:5530;float target_y=find_floor(-6500,2000,target_z,&target_floor);
                victim=spawn_object_abs_with_rot(gMarioObject,0,MODEL_GOOMBA,bhvGoomba,-6500,(s16)(target_y+10),(s16)target_z,0,0,0);
                victim->parentObj=victim;
                fprintf(stderr,"SMB SCENARIO TARGET FLOOR y=%.2f\n",target_y);
                fprintf(stderr,"SMB SCENARIO TARGET object=%ld x=-6500 z=5530\n",victim-gObjectPool);
            }
            if(tick<10)cmd->stick_y=.75f;
            if(tick>=20&&tick<50&&!bomb)cmd->buttons=0x200;
            if(tick>=20&&tick<24&&bomb)cmd->buttons=0x240;
            if(tick>=110&&tick<114&&bomb)cmd->buttons=0x100;
            if(victim&&tick%15==0&&tick<230)fprintf(stderr,"SMB SCENARIO TARGET TRACE tick=%u active=%u action=%d x=%.1f y=%.1f z=%.1f type=%x\n",tick,victim->activeFlags,victim->oAction,victim->oPosX,victim->oPosY,victim->oPosZ,victim->oInteractType);
            if(tick==230&&!checked){checked=1;fprintf(stderr,"SMB SCENARIO TARGET RESULT active=%u action=%d health=%d\n",victim->activeFlags,victim->oAction,victim->oHealth);}
        } else if(!strcmp(test,"star")&&gCurrLevelNum==LEVEL_BOB) {
            static int placed;
            if(!placed)for(unsigned i=0;i<OBJECT_POOL_CAPACITY;i++) {
                struct Object* star=&gObjectPool[i];
                if(!(star->activeFlags&ACTIVE_FLAG_ACTIVE)||!(star->oInteractType&INTERACT_STAR_OR_KEY)||(star->oBehParams>>24)!=5)continue;
                m->pos[0]=star->oPosX;m->pos[1]=star->oPosY+20;m->pos[2]=star->oPosZ;active=0;cmd->flags=4;placed=1;
                fprintf(stderr,"SMB SCENARIO EXISTING STAR object=%u\n",i);break;
            }
        } else if((!strcmp(test,"health")||!strcmp(test,"flight-low")||!strcmp(test,"flight-high"))&&gCurrLevelNum==LEVEL_BOB) {
            static unsigned start;static struct Object* victim;static int healed;
            if(!start){start=frames;m->pos[0]=-6500;m->pos[1]=0;m->pos[2]=6200;active=0;}
            unsigned tick=frames-start;
            int flight=strcmp(test,"health")!=0,high=!strcmp(test,"flight-high");
            if(high&&tick==2)embedded_health(100,0,-1);
            if(flight&&tick==3){m->pos[0]=-6500;m->pos[1]=0;m->pos[2]=6200;active=0;cmd->flags=4;}
            if(tick==20) {
                victim=spawn_object_abs_with_rot(gMarioObject,0,MODEL_GOOMBA,bhvGoomba,-6500,10,6100,0,0,0);victim->parentObj=victim;
            }
            if(snapshot.percent>(high?100:0)&&!healed) {
                fprintf(stderr,"SMB SCENARIO DAMAGE percent=%.2f\n",snapshot.percent);
                if(victim)obj_mark_for_deletion(victim);
                if(!flight) {
                    struct Object* coin=spawn_object_abs_with_rot(gMarioObject,0,MODEL_YELLOW_COIN,bhvYellowCoin,(s16)m->pos[0],(s16)m->pos[1]+40,(s16)m->pos[2],0,0,0);
                    coin->parentObj=coin;
                }
                healed=1;
            }
            if(flight&&healed)fprintf(stderr,"SMB SCENARIO FLIGHT tick=%u percent=%.2f x=%.2f y=%.2f z=%.2f health=%d hurt=%d motion=%u\n",tick,snapshot.percent,m->pos[0],m->pos[1],m->pos[2],m->health,m->hurtCounter,snapshot.motion);
        }
    } else if(!strcmp(test,"platforms")) {
        static struct Object* platform;
        if(frames==0) {
            platform=spawn_object_abs_with_rot(gMarioObject,0,MODEL_NONE,smb_platform_behavior,(s16)m->pos[0],(s16)m->pos[1]+450,(s16)m->pos[2],0,0,0);platform->parentObj=platform;
        }
        if(frames==2) {m->pos[0]=platform->oPosX;m->pos[1]=platform->oPosY+21;m->pos[2]=platform->oPosZ;active=0;cmd->flags=4;}
        if(frames==45) {
            s16 yaw=platform->oFaceAngleYaw;
            m->pos[0]=platform->oPosX+coss(yaw)*300;m->pos[2]=platform->oPosZ-sins(yaw)*300;m->pos[1]=platform->oPosY+20-70;
            active=0;cmd->flags=4;cmd->camera_yaw=atan2f(-sins(yaw),coss(yaw));
        }
        if(frames>=105&&frames<109)cmd->buttons=0x400;
        if(frames>=10&&frames<120)fprintf(stderr,"SMB PLATFORM frame=%u x=%.2f z=%.2f radius=%.2f y=%.2f motion=%u\n",frames,m->pos[0],m->pos[2],hypotf(m->pos[0]-platform->oPosX,m->pos[2]-platform->oPosZ),m->pos[1]-platform->oPosY,snapshot.motion);
    } else if(!strcmp(test,"barriers")) {
        static float start[3],normal[3];
        if(frames==0) {
            if(!embedded_wall||embedded_wall(0,start,normal)){fprintf(stderr,"SMB HOST FAIL no live barrier\n");exit(2);}
            for(int k=0;k<3;k++)m->pos[k]=start[k]*10;
            active=0;cmd->flags=4;
        }
        cmd->camera_yaw=(float)atan2((double)normal[0],(double)normal[2])+(frames>70?((frames/5)%2?.2f:-.2f):0);
        cmd->stick_y=.95f;
        if(frames>5)fprintf(stderr,"SMB BARRIER frame=%u clearance=%.4f y=%.2f\n",frames,14+(m->pos[0]*.1f-start[0])*normal[0]+(m->pos[2]*.1f-start[2])*normal[2],m->pos[1]);
    } else if(!strcmp(test,"walls")||!strcmp(test,"walls-all")) {
        static float start[3],normal[3];
        int all=!strcmp(test,"walls-all");unsigned phase=frames/70,t=all?frames%70:frames;
        if(t==0) {
            static const unsigned roster[]={8,2,0,1,4,5,6,19,11,12,14,13,16,17,15,10,7,9,18,21,22,20,24,3,25,23,34,35,36,37,38,39,40};
            if(all&&phase<33&&embedded_switch(roster[phase],0)){fprintf(stderr,"SMB HOST FAIL wall roster switch\n");exit(2);}
            if(!embedded_wall||embedded_wall(0,start,normal)){fprintf(stderr,"SMB HOST FAIL no live wall\n");exit(2);}
            for(int k=0;k<3;k++)m->pos[k]=start[k]*10;m->pos[1]+=100;
            active=0;cmd->flags=12;
            fprintf(stderr,"SMB SCENARIO WALL start=%.2f,%.2f,%.2f normal=%.2f,%.2f\n",start[0],start[1],start[2],normal[0],normal[2]);
        }
        cmd->camera_yaw=atan2f(normal[2],normal[0]);
        cmd->stick_y=t<3?.9f:t<8?-.9f:0;
        if(t==3)cmd->buttons=0x400;
    } else if(!strcmp(test,"ledges")) {
        static float edge[3],inward[3];
        if(frames==0) {
            if(!embedded_ledge||embedded_ledge(0,edge,inward)){fprintf(stderr,"SMB HOST FAIL no live ledge\n");exit(2);}
            for(int k=0;k<3;k++)m->pos[k]=(edge[k]-inward[k]*5)*10;
            m->pos[1]=(edge[1]-7)*10;active=0;cmd->flags=4;
            fprintf(stderr,"SMB SCENARIO LEDGE edge=%.2f,%.2f,%.2f inward=%.2f,%.2f\n",edge[0],edge[1],edge[2],inward[0],inward[2]);
        }
        cmd->camera_yaw=atan2f(-inward[2],-inward[0]);
        if(frames>=45&&frames<50)cmd->buttons=0x400;
    } else if(!strcmp(test,"transform")) {
        if(frames>=20&&frames<24)cmd->buttons=0x240;
    } else if(!strcmp(test,"projectiles")) {
        if(frames<30)cmd->buttons=0x200; /* Charge and release native bow. */
        if(frames>=90&&frames<94)cmd->buttons=0x240; /* Native down-B bomb. */
        if(frames>=130&&frames<134)cmd->buttons=0x100; /* Native throw. */
    }
}
