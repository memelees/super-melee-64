/* Shared with the native menu and keyboard shortcuts on both platforms. */
int smb_host_option(unsigned option,int toggle) {
    if(option)return 0; /* Retired height-line setting is ignored, including old saves. */
    const char* key="SMB_UNIVERSAL_WALL_JUMP";
    int enabled=getenv(key)&&atoi(getenv(key));
    if(toggle) {
        enabled=!enabled;SDL_setenv(key,enabled?"1":"0",1);
        const char* path=getenv("SMB_OPTIONS_PATH");FILE* file=path?fopen(path,"w"):NULL;
        if(file){fprintf(file,"%d %d\n",getenv("SMB_UNIVERSAL_WALL_JUMP")&&atoi(getenv("SMB_UNIVERSAL_WALL_JUMP")),0);fclose(file);}
        fprintf(stderr,"SMB OPTION %s=%d\n",key,enabled);
    }
    return enabled;
}
static void smb_host_options_load(void) {
    const char* path=getenv("SMB_OPTIONS_PATH");FILE* file=path?fopen(path,"r"):NULL;int wall=0,line=0;
    if(file){if(fscanf(file,"%d %d",&wall,&line)!=2)wall=line=0;fclose(file);}
    if(!getenv("SMB_UNIVERSAL_WALL_JUMP"))SDL_setenv("SMB_UNIVERSAL_WALL_JUMP",wall?"1":"0",1);
    SDL_setenv("SMB_ENEMY_GROUND_LINES","0",1);
}
