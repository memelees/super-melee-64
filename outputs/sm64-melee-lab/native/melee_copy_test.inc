/* Opt-in live regression for a copy absent from the match-start roster. */
#include <melee/ft/kinds/ftKirby/ftkirby.h>
extern struct ft_80459B88_t ft_80459B88;
static void smb_native_copy_test(Fighter* fp) {
    const char* scenario=getenv("SMB_SCENARIO");
    if(!scenario||strcmp(scenario,"kirby-copy"))return;
    static unsigned step,cold,special;
    int requested=getenv("SMB_COPY_TEST_KIND")?atoi(getenv("SMB_COPY_TEST_KIND")):Ft_Kind_Peach;
    int expected=requested>=0&&requested<=Ft_Kind_Emblem?requested:Ft_Kind_Kirby;
    if(fp->kind!=Ft_Kind_Kirby){fprintf(stderr,"SMB KIRBY COPY FAIL wrong_player\n");exit(2);}
    if(++step==20) {
        cold=expected!=Ft_Kind_Kirby&&ft_80459B88.copies[expected]==NULL;
        HSD_GObj* previous=HSD_GObj_804D7814;HSD_GObj_804D7814=fp->gobj;
        ftKb_SpecialN_800F1BAC(fp->gobj,requested,true);
        HSD_GObj_804D7814=previous;
    }
    if(step>20&&step<200&&fp->motion_id>=ftCo_MS_Count)special=1;
    if(step==200) {
        if(fp->u.kb.hat.kind!=expected||!special||(expected!=Ft_Kind_Kirby&&!ft_80459B88.copies[expected])) {
            fprintf(stderr,"SMB KIRBY COPY FAIL expected=%d actual=%d special=%u\n",expected,fp->u.kb.hat.kind,special);exit(2);
        }
        fprintf(stderr,"SMB KIRBY COPY PASS requested=%d kind=%d cold=%u costume=%u special=1 native_render=1\n",requested,expected,cold,fp->costume_id);
    }
}
