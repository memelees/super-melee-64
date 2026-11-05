/* Opt-in live checks: actual SM64 coin interactions and native damage events
 * run in Jolly Roger Bay, with no replacement water or health simulation. */
static struct {unsigned phase,ticks,placed;float x,z,water;} air_test;
static int smb_air_test_enabled(void) {
    const char* s=getenv("SMB_SCENARIO");
    return getenv("SMB_SMOKE")&&s&&!strcmp(s,"air-meter");
}
static void smb_air_test_prepare(struct MarioState* m) {
    if(!smb_air_test_enabled())return;
    if(gCurrLevelNum!=LEVEL_JRB) {
        static int requested;
        if(!requested){extern void initiate_warp(s16,s16,s16,s32);initiate_warp(LEVEL_JRB,1,0x0a,0);active=0;requested=1;}
        return;
    }
    if(!air_test.placed&&frames>25) {
        for(int x=-4500;x<=4500&&!air_test.placed;x+=250)
            for(int z=-4500;z<=4500&&!air_test.placed;z+=250) {
                float water;
                if(!smb_swim_control_column(x,z,&water))continue;
                air_test.x=x;air_test.z=z;air_test.water=water;air_test.placed=1;
                m->pos[0]=x;m->pos[1]=water-2000;m->pos[2]=z;m->waterLevel=water;
                active=0;set_mario_action(m,ACT_FREEFALL,0);
                fprintf(stderr,"SMB AIR FIXTURE level=%d water=%.1f\n",gCurrLevelNum,water);
            }
    }
    if(air_test.placed&&air_test.phase&&air_test.phase<=10&&!air_test.ticks) {
        m->pos[0]=air_test.x;m->pos[1]=air_test.water-2000;m->pos[2]=air_test.z;
        m->waterLevel=air_test.water;active=0;set_mario_action(m,ACT_FREEFALL,0);
    }
}
static void smb_air_test_input(SmbInput* cmd) {
    if(!smb_air_test_enabled())return;
    cmd->buttons=0;cmd->stick_x=cmd->stick_y=cmd->look_x=cmd->look_y=0;
}
static void smb_air_test_before_health(struct MarioState* m) {
    if(!smb_air_test_enabled()||!air_test.placed||frames<40||air_test.phase>10)return;
    if(!air_test.phase)air_test.phase=1;
    if(air_test.ticks)return;
    static const int starts[]={0,1024,1024,512,1500,1500,1500,1000,1000,2100,1000};
    air_remaining=starts[air_test.phase];air_heal_pending=air_hurt_pending=0;
    m->healCounter=m->hurtCounter=0;m->flags&=~MARIO_METAL_CAP;
    embedded_health(-999,0,0);embedded_health(50,0,-1);embedded_actor_damage(0,0);
    if(air_test.phase==7)m->flags|=MARIO_METAL_CAP;
    if(air_test.phase==8)m->waterLevel=m->pos[1]+100;
    if(air_test.phase==10)m->area->terrainType=(m->area->terrainType&~TERRAIN_MASK)|TERRAIN_SNOW;
    if(air_test.phase<=3||air_test.phase==9) {
        int value=air_test.phase==2?2:air_test.phase==3?5:1;
        struct Object* coin=spawn_object_abs_with_rot(gMarioObject,0,MODEL_NONE,bhvYellowCoin,
            m->pos[0],m->pos[1],m->pos[2],0,0,0);
        coin->oDamageOrCoinValue=value;
        extern u32 interact_coin(struct MarioState*,u32,struct Object*);
        interact_coin(m,INTERACT_COIN,coin);
    } else if(air_test.phase==4)m->hurtCounter=4;
    else if(air_test.phase==5||air_test.phase==6) {
        embedded_health(10,0,-1);
        if(air_test.phase==6)embedded_health(-10,0,0);
    }
}
static void smb_air_test_after_health(struct MarioState* m) {
    if(!smb_air_test_enabled()||!air_test.phase||air_test.phase>10)return;
    static const unsigned durations[]={0,4,8,20,4,5,5,12,1,4,6};
    static const int expected[]={0,1280,1536,1792,1244,1180,1180,1000,1026,2176,982};
    static const float percent[]={0,40,30,0,58,60,50,50,50,40,50};
    static const char* names[]={"","yellow","red","blue","world_damage","melee_damage","heal_during_hit","metal_freeze","surface","coin_cap","cold"};
    if(++air_test.ticks<durations[air_test.phase])return;
    SmbFighterState state={0};int result=embedded_actor_state(0,0,&state);
    int pass=!result&&air_remaining==expected[air_test.phase]&&fabsf(state.percent-percent[air_test.phase])<.01f;
    fprintf(stderr,"SMB AIR CHECK case=%s pass=%d air=%d expected=%d percent=%.2f ticks=%u\n",
        names[air_test.phase],pass,air_remaining,expected[air_test.phase],state.percent,air_test.ticks);
    if(!pass)exit(2);
    m->flags&=~MARIO_METAL_CAP;
    air_test.phase++;air_test.ticks=0;
    if(air_test.phase==11)fprintf(stderr,"SMB AIR PASS cases=10\n");
}
