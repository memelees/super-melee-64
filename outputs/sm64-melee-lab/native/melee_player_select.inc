/* Replace only the human seat, at the cooperative tick boundary. */
unsigned smb_embed_costumes(unsigned character) {
    static const unsigned playable[]={8,2,0,1,4,5,6,19,11,12,14,13,16,17,15,10,7,9,18,21,22,20,24,3,25,23,34,35,36,37,38,39,40};
    unsigned i;for(i=0;i<sizeof playable/sizeof *playable;i++)if(playable[i]==character)break;
    if(i==sizeof playable/sizeof *playable)return 0;
    int kind=Player_800325C8(character,false);
    return kind>=33&&kind<40?m3d_akaneia_costume_count(kind):CostumeListsForeachCharacter[kind].numCostumes;
}
int smb_native_replace_player(unsigned character,unsigned costume) {
    unsigned choices=smb_embed_costumes(character);
    if(!choices||costume>=choices)return -1;
    smb_embed_celebrate(0,NULL);
    smb_native_clear_npc_items(0);
    ifStatus_802F6788(0);
    if(Player_GetEntity(0))Player_80031EBC(0);
    Player_InitOrResetPlayer(0);
    Player_SetPlayerCharacter(0,character);Player_SetCostumeId(0,costume);
    smb_native_expect_costume(0,costume);
    Player_SetSlottype(0,Gm_PKind_Human);Player_SetTeam(0,0);
    Player_SetPadPort(0,0);Player_SetHandicap(0,9);Player_SetStocks(0,1);
    Player_SetFacingDirection(0,1);Player_SetNametagSlotID(0,0x78);
    Player_80031AD0(0);Player_80031848(0);ifStatus_802F6508(0);
    for(unsigned sub=0;sub<2;sub++) {
        HSD_GObj* entity=Player_GetEntityAtIndex(0,sub);
        if(entity)smb_native_attributes(entity->user_data);
    }
    gm_LoadAnnouncer();lbAudioAx_8002785C();
    fprintf(stderr,"SMB PLAYER REPLACED external=%u costume=%u choices=%u kind=%d partner=%d\n",character,costume,choices,((Fighter*)Player_GetEntity(0)->user_data)->kind,Player_GetEntityAtIndex(0,1)!=NULL);
    return 0;
}
