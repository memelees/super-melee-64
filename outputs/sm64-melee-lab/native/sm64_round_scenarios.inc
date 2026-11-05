/* Real input, authored BOB rosters and native CPU actors. Holding a shortcut
 * must create one round, and defeating that round must never auto-start one. */
static struct {unsigned phase,began,checked,killed,requested;} round_test;
static int smb_round_test_enabled(void) {
    const char* s=getenv("SMB_SCENARIO");
    return getenv("SMB_SMOKE")&&s&&!strcmp(s,"thematic-rounds");
}
static void smb_round_test_prepare(struct MarioState* m) {
    if(!smb_round_test_enabled())return;
    if(round_test.phase==3) {
        if(gCurrLevelNum==LEVEL_CASTLE_GROUNDS&&active&&frames>round_test.began+400) {
            int pass=!encounter_round_serial&&!smb_npc_count();
            fprintf(stderr,"SMB ROUNDS SCENE RESET pass=%d serial=%u count=%u\n",pass,encounter_round_serial,smb_npc_count());
            if(!pass)exit(2);round_test.phase=4;fprintf(stderr,"SMB ROUNDS PASS requests=4 wrap=1 held_once=1 idle=1 scene_reset=1\n");
        }
        return;
    }
    if(round_test.phase>=4)return;
    if(gCurrLevelNum!=LEVEL_BOB) {
        if(!round_test.requested){extern void initiate_warp(s16,s16,s16,s32);initiate_warp(LEVEL_BOB,1,0x0a,0);active=0;round_test.requested=1;}
        return;
    }
    if(!round_test.phase&&smb_round_controls_ready()&&snapshot.grounded&&frames>80) {
        round_test.phase=1;round_test.began=frames;
        extern s16 sCurrPlayMode;
        s16 mode=sCurrPlayMode;sCurrPlayMode=2; /* SM64 PLAY_MODE_PAUSED. */
        smb_host_key(SDL_SCANCODE_F8,1);
        smb_host_controller_state(1u<<SDL_CONTROLLER_BUTTON_LEFTSTICK,0,0,1);
        int pass=!round_input_pending&&controller_round_latched;
        sCurrPlayMode=mode;smb_host_key(SDL_SCANCODE_F8,0);
        smb_host_controller_state(0,0,0,1);
        fprintf(stderr,"SMB ROUNDS PAUSED INPUT pass=%d buffered=%d\n",pass,round_input_pending);
        if(!pass)exit(2);
        fprintf(stderr,"SMB ROUNDS FIXTURE course=%d level=%d initial_count=%u\n",gCurrCourseNum,gCurrLevelNum,smb_npc_count());
    }
    if(!round_test.phase)return;
    unsigned t=frames-round_test.began;
    int keyboard=(t>=10&&t<20)||(t>=110&&t<120);
    int controller=(t>=60&&t<70)||(t>=160&&t<170);
    smb_host_key(SDL_SCANCODE_F8,keyboard);
    unsigned physical=controller?1u<<SDL_CONTROLLER_BUTTON_LEFTSTICK:0;
    smb_host_controller_state(physical,0,0,1);
    if(controller&&(controller_buttons||smb_host_camera_pad(physical))) {
        fprintf(stderr,"SMB ROUNDS FAIL input_conflict=1\n");exit(2);
    }
    if(t==35||t==85||t==135||t==185) {
        unsigned expected=(t-35)/50+1,wave=(expected-1)%2;
        const SmbStagePlan* plan=&authored_campaign[3];
        int pass=encounter_round_serial==expected&&encounter_wave==wave&&encounter_count==plan->size[wave];
        for(unsigned i=0;i<encounter_count;i++) {
            SmbNpc state={0};
            pass=pass&&!embedded_npc(i+1,NULL,&state)&&npc_ready[i]&&state.level==plan->level[wave][i];
            pass=pass&&encounter_roster[wave][i]==plan->roster[wave][i];
        }
        fprintf(stderr,"SMB ROUNDS CHECK request=%u pass=%d serial=%u wave=%u count=%u\n",expected,pass,encounter_round_serial,encounter_wave,encounter_count);
        if(!pass)exit(2);round_test.checked++;
    }
    if(t>=195&&!round_test.killed&&round_test.checked==4) {
        for(unsigned i=0;i<encounter_count;i++)smb_npc_environment_kill(i+1,0,"round_input_fixture");
        round_test.killed=1;
    }
    if(t==320) {
        int pass=encounter_round_serial==4&&!encounter_next_wave&&encounter_defeated==((1u<<encounter_count)-1);
        fprintf(stderr,"SMB ROUNDS IDLE pass=%d serial=%u defeated=%u\n",pass,encounter_round_serial,encounter_defeated);
        if(!pass)exit(2);
    }
    if(t==350) {
        extern void initiate_warp(s16,s16,s16,s32);
        initiate_warp(LEVEL_CASTLE_GROUNDS,1,0x0a,0);active=0;round_test.phase=3;
    }
}
static void smb_round_test_input(SmbInput* command) {
    if(!smb_round_test_enabled())return;
    command->stick_x=command->stick_y=command->look_x=command->look_y=0;
    command->buttons=0x20;
}
