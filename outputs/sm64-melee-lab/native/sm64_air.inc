/* SM64's health meter is the underwater air meter. Keep its 30 Hz healing
 * and damage queues separate from Melee percentage, which can heal in the
 * same frame as a hit. One SM64 damage wedge maps to eight Melee percent. */
static int smb_air_step(int air,float* heal,float* hurt,int wet,int metal,
                        int cold,int surface,int intangible,int debug) {
    if(!wet) {
        /* Dry combat belongs to Melee. A leftover underwater hit must not
         * cause an invisible drowning death after leaving the water. */
        *heal=*hurt=0;
        return air+26>0x880?0x880:air+26;
    }
    int healing=(int)fminf(64,floorf(*heal));
    int hurting=(int)fminf(64,floorf(*hurt));
    if(!healing&&!hurting&&!metal&&!intangible) {
        if(surface&&!cold)air+=26;
        else if(!debug)air-=cold?3:1;
    }
    air+=healing-hurting;*heal-=healing;*hurt-=hurting;
    if(air>0x880)air=0x880;
    return air<0x100?0xff:air;
}
