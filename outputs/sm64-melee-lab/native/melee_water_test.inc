/* Smoke-only callbacks alongside the live JRB controller run. Native state
 * entry installs each motion's real physics before the water hook is tested;
 * an assigned motion number alone would leave the previous callback active. */
#include <melee/ft/kinds/ftCommon/ftCo_AttackAir.h>
#include <melee/ft/kinds/ftCommon/ftCo_FallSpecial.h>
static void smb_water_test_controls(Fighter* fp,float horizontal,int depth,unsigned buttons) {
    fp->input.lstick[0]=(Vec2){horizontal,depth*.8f};
    fp->input.held_buttons[0]=buttons&0xffff;fp->input.pressed_buttons=0;
    host_environment.stick_x=horizontal;host_environment.stick_y=0;
    host_environment.buttons=buttons;host_environment.camera_yaw=0;
    if(depth>0)host_environment.buttons|=SMB_SWIM_UP;
    if(depth<0)host_environment.buttons|=SMB_SWIM_DOWN;
}
static void smb_water_test_tick(Fighter* fp) {
    smb_native_water_before_input(fp);
    if(fp->phys_cb)fp->phys_cb(fp->gobj);
    smb_native_water_physics(fp);
}
static void smb_water_test_fall(Fighter* fp) {
    ftCommon_8007D5D4(fp);ftCo_Fall_Enter(fp->gobj);
    fp->self_vel=fp->x8c_kb_vel=fp->xD4_unk_vel=(Vec3){0};fp->fall_fast=false;
}
static int smb_water_test_probe(Fighter* fp) {
    unsigned seat=fp->player_idx,sub=fp->is_sub_fighter?1:0;
    __typeof__(fp->input) input_saved=fp->input;
    __typeof__(actor_environment[0][0]) environment_saved=actor_environment[seat][sub];
    __typeof__(swim_state[0][0]) swim_saved=swim_state[seat][sub];
    SmbInput host_saved=host_environment;Vec3 velocity_saved=fp->self_vel,position_saved=fp->cur_pos;
    Vec3 kb_saved=fp->x8c_kb_vel,extra_saved=fp->xD4_unk_vel;
    unsigned jumps_saved=fp->x1968_jumpsUsed;
    int metal_saved=fp->is_metal,fast_saved=fp->fall_fast;
    float lx,lz;m3d_GetLane(fp,&lx,&lz);m3d_SetLane(fp,1,0);
    int okay=1,entry=0,recovery=0,surface=0,cancel=1;
    float idle=0,kick=0,ascent=0,descent_speed=0,special=0,descent=0;
    smb_water_test_fall(fp);fp->is_metal=false;fp->cur_pos.y=0;
    host_environment.flags=SMB_ENV_WATER;host_environment.water_level=500;
    memset(&swim_state[seat][sub],0,sizeof swim_state[seat][sub]);
    if(smb_embed_actor_environment(seat,sub,SMB_ENV_WATER,500))okay=0;
    smb_water_test_controls(fp,0,0,0);
    if(!smb_native_swim_neutral(fp))fprintf(stderr,"SMB WATER PHYSICS BLOCKED seat=%u sub=%u motion=%u sleeping=%u disabled=%u stamina=%u hitstun=%u hitlag=%.3f victim=%u active_captor=%u previous_thrower=%u\n",
        seat,sub,fp->motion_id,fp->is_sleeping,fp->input_disabled,fp->stamina_dead,fp->x221C_b6,fp->dmg.x195c_hitlag_frames,fp->victim_gobj!=NULL,fp->x1A5C!=NULL,fp->throw_thrower!=NULL);
    /* Entering after spending every jump must restore recovery immediately. */
    fp->x1968_jumpsUsed=fp->co_attrs.max_jumps;
    smb_native_water_before_input(fp);entry=fp->x1968_jumpsUsed<fp->co_attrs.max_jumps;
    for(unsigned n=0;n<120;n++){smb_water_test_tick(fp);descent+=fp->self_vel.y;}
    idle=fp->self_vel.y;if(idle>-.47f||idle<-.53f||descent> -48||descent< -66)okay=0;
    smb_water_test_fall(fp);smb_water_test_controls(fp,1,0,0);
    for(unsigned n=0;n<120;n++)smb_water_test_tick(fp);
    kick=fp->self_vel.y;
    if(fabsf(kick)>.04f||fp->self_vel.x<.6f||fp->self_vel.x>1.2f)okay=0;
    smb_water_test_fall(fp);smb_water_test_controls(fp,0,1,0);
    for(unsigned n=0;n<120;n++)smb_water_test_tick(fp);
    ascent=fp->self_vel.y;if(ascent<.76f||ascent>.84f)okay=0;
    smb_water_test_fall(fp);smb_water_test_controls(fp,0,-1,0);
    for(unsigned n=0;n<120;n++)smb_water_test_tick(fp);
    descent_speed=fp->self_vel.y;if(descent_speed<-.84f||descent_speed>-.76f)okay=0;
    /* Both physical modifier buttons only exist on human input. NPCs have a
     * single native vertical axis, tested independently above. */
    if(!seat) {
        smb_water_test_fall(fp);smb_water_test_controls(fp,0,0,SMB_SWIM_UP|SMB_SWIM_DOWN);
        for(unsigned n=0;n<120;n++)smb_water_test_tick(fp);
        cancel=fabsf(fp->self_vel.y)<.04f;if(!cancel)okay=0;
    }
    /* Real helpless entry consumes the jump budget. The water IASA hook must
     * install Fall before input processing so another special is possible. */
    smb_water_test_fall(fp);smb_water_test_controls(fp,0,0,0);
    ftCo_80096900(fp->gobj,1,0,true,1,20);
    smb_native_water_before_input(fp);
    recovery=fp->motion_id>=ftCo_MS_Fall&&fp->motion_id<=ftCo_MS_FallB&&fp->x1968_jumpsUsed<fp->co_attrs.max_jumps&&fp->input_cb!=NULL;
    /* An active move's velocity and callback must survive the same hook. */
    ftCo_AttackAir_EnterFromMsid(fp->gobj,ftCo_MS_AttackAirN);
    fp->self_vel=(Vec3){2,3.5f,0};HSD_GObjEvent move_physics=fp->phys_cb;
    smb_water_test_tick(fp);special=fp->self_vel.y;
    if(special<3||fp->self_vel.x<1.8f||fp->phys_cb!=move_physics||fp->motion_id!=ftCo_MS_AttackAirN)okay=0;
    smb_water_test_fall(fp);smb_water_test_controls(fp,0,0,0);
    fp->cur_pos.y=490;fp->x1968_jumpsUsed=fp->co_attrs.max_jumps;
    fp->input.pressed_buttons=HSD_PAD_X;fp->input.held_buttons[0]=HSD_PAD_X;host_environment.buttons=0x400;
    surface=smb_native_water_before_input(fp)&&fp->self_vel.y>=3&&
        (fp->motion_id==ftCo_MS_JumpF||fp->motion_id==ftCo_MS_JumpB);
    /* Surface escape must keep retail launch, rather than immediately
     * damping the jump back to the ordinary .8 swimming speed. */
    fp->input.pressed_buttons=0;smb_water_test_tick(fp);
    if(fp->self_vel.y<2.5f)surface=0;
    if(!entry||!recovery||!surface)okay=0;
    smb_water_test_fall(fp);fp->self_vel=(Vec3){1,-1.3f,0};fp->is_metal=true;
    smb_native_water_physics(fp);if(fp->self_vel.y!= -1.3f||fp->self_vel.x!=1)okay=0;
    fp->is_metal=false;smb_embed_actor_environment(seat,sub,0,500);host_environment.flags=0;
    smb_native_water_physics(fp);if(fp->self_vel.y!= -1.3f||fp->self_vel.x!=1)okay=0;
    fprintf(stderr,"SMB WATER PHYSICS PROBE seat=%u sub=%u idle=%.3f descent=%.3f kick=%.3f ascend=%.3f dive=%.3f special=%.3f entry=%d recovery=%d surface=%d cancel=%d passed=%d\n",seat,sub,idle,descent,kick,ascent,descent_speed,special,entry,recovery,surface,cancel,okay);
    /* Return a valid retail Fall callback set; host smoke placement will
     * reconcile the real position on its next frame. */
    smb_water_test_fall(fp);fp->input=input_saved;fp->self_vel=velocity_saved;fp->cur_pos=position_saved;
    fp->x8c_kb_vel=kb_saved;fp->xD4_unk_vel=extra_saved;fp->x1968_jumpsUsed=jumps_saved;
    fp->is_metal=metal_saved;fp->fall_fast=fast_saved;
    host_environment=host_saved;actor_environment[seat][sub]=environment_saved;swim_state[seat][sub]=swim_saved;m3d_SetLane(fp,lx,lz);
    return okay;
}
void smb_native_water_test(void) {
    static unsigned complete;
    if(complete||!host_world||!getenv("SMB_SMOKE")||!getenv("SMB_TEST_WATER_PHYSICS"))return;
    complete=1;
    /* A dedicated empty seat keeps campaign and player ownership unchanged. */
    if(Player_GetEntity(3)||Player_GetEntity(18))goto failed;
    unsigned humans=0;
    for(unsigned sub=0;sub<2;sub++) {
        HSD_GObj* object=Player_GetEntityAtIndex(0,sub);if(!object)continue;
        Fighter* fp=object->user_data;if(fp->is_sleeping)continue;
        if(!smb_water_test_probe(fp))goto failed;humans++;
    }
    smb_native_create_npc(3,14,0);
    {
        Fighter* player=Player_GetEntity(0)->user_data;
        float position[3]={player->cur_pos.x+30,player->cur_pos.y+1,player->cur_pos.z};SmbNpc state;
        /* Follow the normal encounter wake path. Merely flipping the sleep
         * flag leaves native entry/inactive state behind on a new fighter. */
        if(smb_embed_npc(3,position,&state))goto failed;
    }
    for(unsigned sub=0;sub<2;sub++) {
        HSD_GObj* object=Player_GetEntityAtIndex(3,sub);if(!object)goto failed;
        Fighter* fp=object->user_data;
        if(!smb_water_test_probe(fp))goto failed;
        if(!smb_native_damage_test_actor(fp))goto failed;
        SmbFighterState state;if(smb_embed_actor_state(3,sub,&state)||state.kind!=(sub?Ft_Kind_Nana:Ft_Kind_Popo))goto failed;
        if(smb_embed_actor_environment(3,sub,SMB_ENV_WATER|SMB_ENV_METAL|SMB_ENV_CAP_MANAGED,500))goto failed;
    }
    smb_native_actor_environments();
    for(unsigned sub=0;sub<2;sub++) {
        Fighter* fp=Player_GetEntityAtIndex(3,sub)->user_data;
        if(!fp->is_metal)goto failed;
        smb_embed_actor_environment(3,sub,SMB_ENV_CAP_MANAGED,500);
    }
    smb_native_actor_environments();
    for(unsigned sub=0;sub<2;sub++)if(((Fighter*)Player_GetEntityAtIndex(3,sub)->user_data)->is_metal)goto failed;
    ifStatus_802F6788(3);Player_80031EBC(3);Player_InitOrResetPlayer(3);
    encounter_visible&=~(1u<<3);memset(npc_sleeping[3],0,sizeof npc_sleeping[3]);memset(actor_environment[3],0,sizeof actor_environment[3]);
    SmbRemote remote={0};remote.version=SMB_MAGIC;remote.character=2;remote.kind=Ft_Kind_Fox;remote.motion=ftCo_MS_Fall;remote.facing[0]=1;
    if(smb_embed_remote(15,&remote)||smb_embed_actor_environment(18,0,SMB_ENV_WATER,500))goto failed;
    Fighter* proxy=Player_GetEntity(18)->user_data;proxy->self_vel=(Vec3){2,-3,0};
    smb_native_water_physics(proxy);if(proxy->self_vel.x!=2||proxy->self_vel.y!= -3)goto failed;
    SmbFighterState state;if(smb_embed_actor_state(18,0,&state)!= -3)goto failed;
    smb_embed_remote(15,NULL);memset(actor_environment[18],0,sizeof actor_environment[18]);
    if(!humans||smb_embed_actor_environment(19,0,0,0)!= -1||smb_embed_actor_environment(0,2,0,0)!= -1)goto failed;
    fprintf(stderr,"SMB WATER PHYSICS PASS player=%u cpu_primary=1 cpu_nana=1 sink=.50 kicks=1 specials=1 metal=1 exit=1 proxy_untouched=1\n",humans);
    return;
failed:
    fprintf(stderr,"SMB WATER PHYSICS FAIL\n");exit(2);
}
