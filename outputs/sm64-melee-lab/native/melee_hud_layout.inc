/* Keep native damage digits, portraits and emblem animations. Sixteen players
 * use two compact rows; ordinary four-player adventure layout stays full size. */
extern int smb_native_remote_controlled(Fighter*);
static unsigned hud_seats[GM_MAX_PLAYERS],hud_count;
static struct {HSD_JObj* root;Vec3 position,scale;} hud_saved[GM_MAX_PLAYERS*3];
static unsigned hud_saved_count;
static int smb_hud_visible(unsigned seat,const SmbRender* request) {
    HSD_GObj* object=Player_GetEntity(seat);if(!object)return 0;
    if(!seat)return 1;
    Fighter* fp=object->user_data;
    return !(request->flags&4)&&!fp->is_sleeping&&(smb_native_npc_visible(seat)||smb_native_remote_controlled(fp));
}
static void smb_hud_layout_begin(const SmbRender* request) {
    hud_count=hud_saved_count=0;
    for(unsigned seat=0;seat<GM_MAX_PLAYERS;seat++)if(smb_hud_visible(seat,request))hud_seats[hud_count++]=seat;
}
static void smb_hud_place(HSD_GObj* object,unsigned seat) {
    if(!object||!object->hsd_obj)return;
    unsigned index=0;while(index<hud_count&&hud_seats[index]!=seat)index++;
    if(index==hud_count)return;
    HSD_JObj* root=object->hsd_obj;unsigned saved=hud_saved_count++;
    if(saved>=GM_MAX_PLAYERS*3)abort();
    hud_saved[saved].root=root;HSD_JObjGetTranslation(root,&hud_saved[saved].position);HSD_JObjGetScale(root,&hud_saved[saved].scale);
    Vec3 source=*ifAll_GetPlayerHUDPosition(seat),target=*ifAll_GetPlayerHUDPosition(index<4?index:0);
    float scale=1;
    if(hud_count>4){scale=.48f;target=(Vec3){-24+(index%8)*7.f,-17+(index/8)*5.f,0};}
    Vec3 position=hud_saved[saved].position,size=hud_saved[saved].scale;
    position.x=target.x+(position.x-source.x)*scale;position.y=target.y+(position.y-source.y)*scale;
    size.x*=scale;size.y*=scale;size.z*=scale;
    HSD_JObjSetTranslate(root,&position);HSD_JObjSetScale(root,&size);
}
static void smb_hud_layout_end(void) {
    while(hud_saved_count){unsigned i=--hud_saved_count;HSD_JObjSetTranslate(hud_saved[i].root,&hud_saved[i].position);HSD_JObjSetScale(hud_saved[i].root,&hud_saved[i].scale);}
}
