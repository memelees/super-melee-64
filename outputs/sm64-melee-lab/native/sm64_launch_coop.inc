/* Replace this process with the network host, retaining the selected fighter
 * and locally prepared resources. No shell or second Melee process is used. */
int smb_host_launch_coop(const char* address) {
    const char* executable=getenv("SMB_COOP_EXECUTABLE"),*profile=getenv("SMB_PROFILE_COOP"),*rom=getenv("SMB_SM64_ROM");
    if(!executable||!profile||!rom||(address&&(!*address||strlen(address)>31)))return -1;
#ifdef _WIN32
    _mkdir(profile);
#else
    mkdir(profile,0755);
#endif
    char path[4096];snprintf(path,sizeof path,"%s/baserom.us.z64",profile);
    FILE* existing=fopen(path,"rb");
    if(existing)fclose(existing);
    else {
        FILE* input=fopen(rom,"rb"),*out=input?fopen(path,"wb"):NULL;
        if(!input||!out){if(input)fclose(input);return -2;}
        char buffer[65536];size_t size;int failed=0;
        while((size=fread(buffer,1,sizeof buffer,input)))if(fwrite(buffer,1,size,out)!=size){failed=1;break;}
        if(ferror(input))failed=1;fclose(input);if(fclose(out))failed=1;if(failed){unlink(path);return -2;}
    }
    char number[16];snprintf(number,sizeof number,"%u",snapshot.character);
    SDL_setenv("MELEE3D_NATIVE_LINEUP",number,1);SDL_setenv("SMB_PLAYER_EXTERNAL",number,1);
    snprintf(number,sizeof number,"%u",snapshot.costume);SDL_setenv("SMB_PLAYER_COSTUME",number,1);
    SDL_setenv("MELEE3D_NATIVE_LAB","1",1);SDL_setenv("SMB_CPU_COUNT","0",1);
    SDL_setenv("SMB_SAVE_MODE","continue",1);
    unsetenv("SMB_START_STAGE");SDL_setenv("SMB_COURSE_ENEMIES","1",1);unsetenv("SMB_ALL_STARS");
    unsetenv("SMB_CHARACTER_SELECT");unsetenv("SMB_SESSION_LOCK");
    smb_record_stop();
    const char* command[]={executable,"--savepath",profile,"--windowed","--skip-intro","--skip-update-check","--disable-mods","--no-discord","--playercount","16",
        address?"--client":"--server",address?address:"7777",address?"7777":NULL,NULL};
    execv(executable,(char* const*)command);return -3;
}
