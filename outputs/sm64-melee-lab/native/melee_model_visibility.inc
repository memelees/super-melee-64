/* Read authored model groups only in the GCC disc-data boundary. This probe
 * compares rendered DObj flags with the active native animation's selections;
 * it never changes a fighter or forces a particular character form. */
#include <sysdolphin/baselib/dobj.h>
void smb_native_model_probe(HSD_GObj* object) {
    if(!getenv("SMB_MODEL_TRACE")||!object)return;
    Fighter* fp=object->user_data;
    FtPartsVisLookup* lookup=fp->x5AC.xC[0];
    if(fp->is_sleeping||!lookup||!fp->x5AC.cleared[0])return;
    if(fp->dobj_list.count>256||fp->x5AC.model_num>12)abort();
    signed char expected[256];memset(expected,-1,sizeof expected);
    for(unsigned model=0;model<fp->x5AC.model_num;model++) {
        int count=lookup[model].x0;if(count<0||count>128)abort();
        TempS* groups=DP(TempS,lookup[model].x4);
        for(int variant=0;variant<count;variant++) {
            u8* indices=DP(u8,groups[variant].x4);
            int n=groups[variant].x0;if(n<0||n>256)abort();
            for(int k=0;k<n;k++) {
                unsigned index=indices[k];if(index>=fp->dobj_list.count)abort();
                expected[index]=variant!=fp->x5F4_arr[model].idx;
            }
        }
    }
    unsigned checked=0,mismatch=0,visible=0;unsigned long long signature=1469598103934665603ull;
    for(unsigned i=0;i<fp->dobj_list.count;i++) {
        int hidden=(fp->dobj_list.data[i]->flags&DOBJ_HIDDEN)!=0;
        if(expected[i]>=0){checked++;mismatch+=hidden!=expected[i];}
        visible+=!hidden;signature=(signature^hidden)*1099511628211ull;
    }
    fprintf(stderr,"SMB MODEL CHECK slot=%u sub=%u kind=%d motion=%d model0=%d default0=%d checked=%u mismatch=%u visible=%u signature=%llx\n",fp->player_idx,fp->is_sub_fighter,fp->kind,fp->motion_id,fp->x5F4_arr[0].idx,fp->x5F4_arr[0].prev,checked,mismatch,visible,signature);
}
