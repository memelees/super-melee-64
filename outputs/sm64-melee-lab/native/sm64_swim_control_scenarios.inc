/* Live controller regression in real Jolly Roger Bay water. A fresh process
 * can run the same course with any selected character; no test physics or
 * synthetic water volume replaces either game's normal simulation. */
enum { SMB_SWIM_ENTRY=1,SMB_SWIM_FORWARD,SMB_SWIM_ASCEND,SMB_SWIM_DESCEND,
       SMB_SWIM_BOTH,SMB_SWIM_EXIT,SMB_SWIM_SPECIAL,SMB_SWIM_RETRY,SMB_SWIM_DONE };
static struct {
    unsigned phase,began,place,jumped,special,recovered,sampled,dry,results,failed;
    float x,z,water,sample[3],high,low;
} swim_control;
static int smb_swim_control_enabled(void) {
    const char* scenario=getenv("SMB_SCENARIO");
    return getenv("SMB_SMOKE")&&scenario&&!strcmp(scenario,"swim-controls");
}
static void smb_swim_control_next(unsigned phase) {
    swim_control.phase=phase;swim_control.place=1;swim_control.sampled=0;
    swim_control.high=-1e9f;swim_control.low=1e9f;swim_control.dry=0;
}
static void smb_swim_control_result(const char* name,int pass,float a,float b) {
    swim_control.results++;if(!pass)swim_control.failed++;
    fprintf(stderr,"SMB SWIM CONTROL case=%s pass=%d kind=%u a=%.3f b=%.3f\n",name,pass,snapshot.kind,a,b);
}
static int smb_swim_control_column(float x,float z,float* water) {
    float surface=find_water_level(x,z);struct Surface* floor=NULL;
    if(surface< -10000)return 0;
    float bottom=find_floor(x,surface,z,&floor);
    if(!floor||floor->type==SURFACE_DEATH_PLANE||surface-bottom<3000)return 0;
    struct Surface* ceiling=NULL;float top=find_ceil(x,surface+30,z,&ceiling);
    if(ceiling&&top-surface<1200)return 0;
    *water=surface;return 1;
}
static void smb_swim_control_prepare(struct MarioState* m) {
    if(!smb_swim_control_enabled())return;
    if(gCurrLevelNum!=LEVEL_JRB) {
        static unsigned requested;
        if(!requested){extern void initiate_warp(s16,s16,s16,s32);initiate_warp(LEVEL_JRB,1,0x0a,0);active=0;requested=1;}
        return;
    }
    if(!swim_control.phase&&active&&frames>25) {
        for(int x=-4500;x<=4500&&!swim_control.phase;x+=250)
            for(int z=-4500;z<=4500&&!swim_control.phase;z+=250) {
                float surface,a,b;
                /* Full-stick travel has room for two seconds at SM64's peak
                 * swim speed, in both directions, without a shoreline. */
                if(!smb_swim_control_column(x,z,&surface)||!smb_swim_control_column(x+2000,z,&a)||
                   !smb_swim_control_column(x-1000,z,&b)||fabsf(a-surface)>1||fabsf(b-surface)>1)continue;
                swim_control.x=x;swim_control.z=z;swim_control.water=surface;
                smb_swim_control_next(SMB_SWIM_ENTRY);
                fprintf(stderr,"SMB SWIM CONTROL FIXTURE level=%d x=%d z=%d surface=%.1f baseline_normal=480 baseline_peak=840\n",gCurrLevelNum,x,z,surface);
            }
        if(!swim_control.phase&&frames>180){fprintf(stderr,"SMB SWIM CONTROL FAIL reason=no_open_water\n");exit(2);}
    }
    if(swim_control.place&&swim_control.phase<SMB_SWIM_DONE) {
        static const float depths[]={0,-400,900,1000,700,900,80,2300,2300};
        m->pos[0]=swim_control.x;m->pos[2]=swim_control.z;
        m->pos[1]=swim_control.water-depths[swim_control.phase];m->waterLevel=swim_control.water;
        m->health=0x880;air_remaining=0x880;water_active=0;active=0;
        memset(m->vel,0,sizeof m->vel);m->forwardVel=0;
        set_mario_action(m,ACT_FREEFALL,0);
        swim_control.began=frames;swim_control.place=0;
    }
}
static void smb_swim_control_input(struct MarioState* m,SmbInput* cmd) {
    (void)m;if(!smb_swim_control_enabled())return;
    cmd->stick_x=cmd->stick_y=cmd->look_x=cmd->look_y=0;cmd->buttons=0;cmd->camera_yaw=0;
    if(!swim_control.phase||swim_control.place)return;
    unsigned t=frames-swim_control.began;
    switch(swim_control.phase) {
    case SMB_SWIM_ENTRY:if(t==4||t==5)cmd->buttons=0x400;break;
    case SMB_SWIM_FORWARD:cmd->stick_x=1;break;
    /* Exercise the normal Q/E or L1/L2 modifier path. The host translates
     * these to swim intent after this input hook, exactly as for the player. */
    case SMB_SWIM_ASCEND:cmd->buttons=0x8;break;
    case SMB_SWIM_DESCEND:cmd->buttons=0x40;break;
    case SMB_SWIM_BOTH:cmd->buttons=0x48;break;
    case SMB_SWIM_EXIT:if(t==6||t==7)cmd->buttons=0x400;break;
    case SMB_SWIM_SPECIAL:if(t>=8&&t<11)cmd->buttons=0x208;break;
    case SMB_SWIM_RETRY:if(t<20)cmd->stick_x=1;else if(t<23)cmd->buttons=0x208;break;
    default:break;
    }
}
static void smb_swim_control_after(struct MarioState* m) {
    if(!smb_swim_control_enabled()||!swim_control.phase||swim_control.place||swim_control.phase==SMB_SWIM_DONE)return;
    unsigned t=frames-swim_control.began;
    swim_control.high=fmaxf(swim_control.high,m->pos[1]);swim_control.low=fminf(swim_control.low,m->pos[1]);
    if(t%15==0)fprintf(stderr,"SMB SWIM CONTROL TRACE phase=%u t=%u wet=%d y=%.3f vy=%.3f motion=%u x=%.3f z=%.3f\n",
        swim_control.phase,t,water_active,m->pos[1],snapshot.vy,snapshot.motion,m->pos[0],m->pos[2]);
    if(swim_control.phase==SMB_SWIM_ENTRY) {
        if(t>=4&&t<30&&snapshot.vy>1)swim_control.jumped=1;
        if(t>12&&water_active) {
            smb_swim_control_result("jump_entry",swim_control.jumped,swim_control.high-swim_control.water,t);
            smb_swim_control_next(SMB_SWIM_FORWARD);
        } else if(t>180){smb_swim_control_result("jump_entry",0,swim_control.high-swim_control.water,t);smb_swim_control_next(SMB_SWIM_FORWARD);}
        return;
    }
    if(swim_control.phase>=SMB_SWIM_FORWARD&&swim_control.phase<=SMB_SWIM_BOTH) {
        if(t>=15&&!swim_control.sampled){memcpy(swim_control.sample,m->pos,sizeof swim_control.sample);swim_control.sampled=t;}
        unsigned duration=swim_control.phase==SMB_SWIM_FORWARD?60:40;
        if(t>=duration&&swim_control.sampled) {
            float seconds=(t-swim_control.sampled)/30.f,dy=m->pos[1]-swim_control.sample[1];
            float speed=hypotf(m->pos[0]-swim_control.sample[0],m->pos[2]-swim_control.sample[2])/seconds;
            if(swim_control.phase==SMB_SWIM_FORWARD)
                smb_swim_control_result("forward",water_active&&speed>=350&&speed<=1000&&fabsf(dy)<100,speed,dy);
            else if(swim_control.phase==SMB_SWIM_ASCEND)
                smb_swim_control_result("ascend",water_active&&dy/seconds>=350&&dy/seconds<=650,dy/seconds,speed);
            else if(swim_control.phase==SMB_SWIM_DESCEND)
                smb_swim_control_result("descend",water_active&&dy/seconds<=-350&&dy/seconds>=-650,dy/seconds,speed);
            else smb_swim_control_result("cancel",water_active&&fabsf(dy)<80,dy,speed);
            smb_swim_control_next(swim_control.phase+1);
        }
        return;
    }
    if(swim_control.phase==SMB_SWIM_EXIT) {
        if(t>6&&!water_active&&m->pos[1]>swim_control.water+20)swim_control.dry=1;
        if(t>=35) {
            smb_swim_control_result("surface_exit",swim_control.dry&&swim_control.high>swim_control.water+80,swim_control.high-swim_control.water,swim_control.dry);
            smb_swim_control_next(SMB_SWIM_SPECIAL);
        }
        return;
    }
    if(swim_control.phase==SMB_SWIM_SPECIAL) {
        if(t>8&&snapshot.motion>=341)swim_control.special=1;
        int neutral=snapshot.motion>=25&&snapshot.motion<=34;
        if(swim_control.special&&t>30&&water_active&&neutral) {
            swim_control.recovered=1;memcpy(swim_control.sample,m->pos,sizeof swim_control.sample);
            /* Preserve the actual special's finishing state and position for
             * this retry. A placement/reset would hide the helpless bug. */
            swim_control.phase=SMB_SWIM_RETRY;swim_control.began=frames;swim_control.sampled=0;return;
        }
        if(t>220){smb_swim_control_result("special_recovery",0,snapshot.motion,water_active);smb_swim_control_next(SMB_SWIM_DONE);}
        return;
    }
    if(swim_control.phase==SMB_SWIM_RETRY) {
        if(t==19) {
            float moved=hypotf(m->pos[0]-swim_control.sample[0],m->pos[2]-swim_control.sample[2]);
            swim_control.sampled=water_active&&moved>50;
        }
        if(t>=23&&snapshot.motion>=341) {
            smb_swim_control_result("special_recovery",swim_control.recovered&&swim_control.sampled,swim_control.sampled,snapshot.motion);
            smb_swim_control_next(SMB_SWIM_DONE);
        } else if(t>65){smb_swim_control_result("special_recovery",0,swim_control.sampled,snapshot.motion);smb_swim_control_next(SMB_SWIM_DONE);}
    }
    if(swim_control.phase==SMB_SWIM_DONE) {
        fprintf(stderr,"SMB SWIM CONTROL %s cases=%u kind=%u\n",swim_control.failed?"FAIL":"PASS",swim_control.results,snapshot.kind);
        if(swim_control.failed)exit(2);
    }
}
