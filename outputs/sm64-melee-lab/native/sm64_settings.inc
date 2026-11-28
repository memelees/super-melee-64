#ifndef SMB_SETTINGS_INC
#define SMB_SETTINGS_INC
/* Settings menu: window scale, display mode, Melee render API and SSAA.
 * Drawn in the roster screen's 960x720 design space and palette, so it reads
 * as another screen of the same frontend rather than an SDL dialog. */

/* Public-domain 8x8 bitmap font (dhepper/font8x8) for menu labels. The
 * roster's own labels are pre-baked Melee artwork, so new text needs glyphs. */
static const unsigned char smb_settings_font_bits[95][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x18,0x3c,0x3c,0x18,0x18,0x00,0x18,0x00},
    {0x36,0x36,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x36,0x36,0x7f,0x36,0x7f,0x36,0x36,0x00},
    {0x0c,0x3e,0x03,0x1e,0x30,0x1f,0x0c,0x00},
    {0x00,0x63,0x33,0x18,0x0c,0x66,0x63,0x00},
    {0x1c,0x36,0x1c,0x6e,0x3b,0x33,0x6e,0x00},
    {0x06,0x06,0x03,0x00,0x00,0x00,0x00,0x00},
    {0x18,0x0c,0x06,0x06,0x06,0x0c,0x18,0x00},
    {0x06,0x0c,0x18,0x18,0x18,0x0c,0x06,0x00},
    {0x00,0x66,0x3c,0xff,0x3c,0x66,0x00,0x00},
    {0x00,0x0c,0x0c,0x3f,0x0c,0x0c,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x0c,0x0c,0x06},
    {0x00,0x00,0x00,0x3f,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x0c,0x0c,0x00},
    {0x60,0x30,0x18,0x0c,0x06,0x03,0x01,0x00},
    {0x3e,0x63,0x73,0x7b,0x6f,0x67,0x3e,0x00},
    {0x0c,0x0e,0x0c,0x0c,0x0c,0x0c,0x3f,0x00},
    {0x1e,0x33,0x30,0x1c,0x06,0x33,0x3f,0x00},
    {0x1e,0x33,0x30,0x1c,0x30,0x33,0x1e,0x00},
    {0x38,0x3c,0x36,0x33,0x7f,0x30,0x78,0x00},
    {0x3f,0x03,0x1f,0x30,0x30,0x33,0x1e,0x00},
    {0x1c,0x06,0x03,0x1f,0x33,0x33,0x1e,0x00},
    {0x3f,0x33,0x30,0x18,0x0c,0x0c,0x0c,0x00},
    {0x1e,0x33,0x33,0x1e,0x33,0x33,0x1e,0x00},
    {0x1e,0x33,0x33,0x3e,0x30,0x18,0x0e,0x00},
    {0x00,0x0c,0x0c,0x00,0x00,0x0c,0x0c,0x00},
    {0x00,0x0c,0x0c,0x00,0x00,0x0c,0x0c,0x06},
    {0x18,0x0c,0x06,0x03,0x06,0x0c,0x18,0x00},
    {0x00,0x00,0x3f,0x00,0x00,0x3f,0x00,0x00},
    {0x06,0x0c,0x18,0x30,0x18,0x0c,0x06,0x00},
    {0x1e,0x33,0x30,0x18,0x0c,0x00,0x0c,0x00},
    {0x3e,0x63,0x7b,0x7b,0x7b,0x03,0x1e,0x00},
    {0x0c,0x1e,0x33,0x33,0x3f,0x33,0x33,0x00},
    {0x3f,0x66,0x66,0x3e,0x66,0x66,0x3f,0x00},
    {0x3c,0x66,0x03,0x03,0x03,0x66,0x3c,0x00},
    {0x1f,0x36,0x66,0x66,0x66,0x36,0x1f,0x00},
    {0x7f,0x46,0x16,0x1e,0x16,0x46,0x7f,0x00},
    {0x7f,0x46,0x16,0x1e,0x16,0x06,0x0f,0x00},
    {0x3c,0x66,0x03,0x03,0x73,0x66,0x7c,0x00},
    {0x33,0x33,0x33,0x3f,0x33,0x33,0x33,0x00},
    {0x1e,0x0c,0x0c,0x0c,0x0c,0x0c,0x1e,0x00},
    {0x78,0x30,0x30,0x30,0x33,0x33,0x1e,0x00},
    {0x67,0x66,0x36,0x1e,0x36,0x66,0x67,0x00},
    {0x0f,0x06,0x06,0x06,0x46,0x66,0x7f,0x00},
    {0x63,0x77,0x7f,0x7f,0x6b,0x63,0x63,0x00},
    {0x63,0x67,0x6f,0x7b,0x73,0x63,0x63,0x00},
    {0x1c,0x36,0x63,0x63,0x63,0x36,0x1c,0x00},
    {0x3f,0x66,0x66,0x3e,0x06,0x06,0x0f,0x00},
    {0x1e,0x33,0x33,0x33,0x3b,0x1e,0x38,0x00},
    {0x3f,0x66,0x66,0x3e,0x36,0x66,0x67,0x00},
    {0x1e,0x33,0x07,0x0e,0x38,0x33,0x1e,0x00},
    {0x3f,0x2d,0x0c,0x0c,0x0c,0x0c,0x1e,0x00},
    {0x33,0x33,0x33,0x33,0x33,0x33,0x3f,0x00},
    {0x33,0x33,0x33,0x33,0x33,0x1e,0x0c,0x00},
    {0x63,0x63,0x63,0x6b,0x7f,0x77,0x63,0x00},
    {0x63,0x63,0x36,0x1c,0x1c,0x36,0x63,0x00},
    {0x33,0x33,0x33,0x1e,0x0c,0x0c,0x1e,0x00},
    {0x7f,0x63,0x31,0x18,0x4c,0x66,0x7f,0x00},
    {0x1e,0x06,0x06,0x06,0x06,0x06,0x1e,0x00},
    {0x03,0x06,0x0c,0x18,0x30,0x60,0x40,0x00},
    {0x1e,0x18,0x18,0x18,0x18,0x18,0x1e,0x00},
    {0x08,0x1c,0x36,0x63,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff},
    {0x0c,0x0c,0x18,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x1e,0x30,0x3e,0x33,0x6e,0x00},
    {0x07,0x06,0x06,0x3e,0x66,0x66,0x3b,0x00},
    {0x00,0x00,0x1e,0x33,0x03,0x33,0x1e,0x00},
    {0x38,0x30,0x30,0x3e,0x33,0x33,0x6e,0x00},
    {0x00,0x00,0x1e,0x33,0x3f,0x03,0x1e,0x00},
    {0x1c,0x36,0x06,0x0f,0x06,0x06,0x0f,0x00},
    {0x00,0x00,0x6e,0x33,0x33,0x3e,0x30,0x1f},
    {0x07,0x06,0x36,0x6e,0x66,0x66,0x67,0x00},
    {0x0c,0x00,0x0e,0x0c,0x0c,0x0c,0x1e,0x00},
    {0x30,0x00,0x30,0x30,0x30,0x33,0x33,0x1e},
    {0x07,0x06,0x66,0x36,0x1e,0x36,0x67,0x00},
    {0x0e,0x0c,0x0c,0x0c,0x0c,0x0c,0x1e,0x00},
    {0x00,0x00,0x33,0x7f,0x7f,0x6b,0x63,0x00},
    {0x00,0x00,0x1f,0x33,0x33,0x33,0x33,0x00},
    {0x00,0x00,0x1e,0x33,0x33,0x33,0x1e,0x00},
    {0x00,0x00,0x3b,0x66,0x66,0x3e,0x06,0x0f},
    {0x00,0x00,0x6e,0x33,0x33,0x3e,0x30,0x78},
    {0x00,0x00,0x3b,0x6e,0x66,0x06,0x0f,0x00},
    {0x00,0x00,0x3e,0x03,0x1e,0x30,0x1f,0x00},
    {0x08,0x0c,0x3e,0x0c,0x0c,0x2c,0x18,0x00},
    {0x00,0x00,0x33,0x33,0x33,0x33,0x6e,0x00},
    {0x00,0x00,0x33,0x33,0x33,0x1e,0x0c,0x00},
    {0x00,0x00,0x63,0x6b,0x7f,0x7f,0x36,0x00},
    {0x00,0x00,0x63,0x36,0x1c,0x36,0x63,0x00},
    {0x00,0x00,0x33,0x33,0x33,0x3e,0x30,0x1f},
    {0x00,0x00,0x3f,0x19,0x0c,0x26,0x3f,0x00},
    {0x38,0x0c,0x0c,0x07,0x0c,0x0c,0x38,0x00},
    {0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00},
    {0x07,0x0c,0x0c,0x38,0x0c,0x0c,0x07,0x00},
    {0x6e,0x3b,0x00,0x00,0x00,0x00,0x00,0x00},
};

#define SMB_SETTINGS_SCALES 4
#define SMB_SETTINGS_APIS 4
#define SMB_SETTINGS_SSAA 3
static const float smb_settings_scale_value[SMB_SETTINGS_SCALES] = {1.f,1.5f,2.f,3.f};
static const char* smb_settings_scale_label[SMB_SETTINGS_SCALES] = {"1.0X","1.5X","2.0X","3.0X"};
static const char* smb_settings_api_value[SMB_SETTINGS_APIS] = {"d3d12","d3d11","vulkan","opengl"};
static const char* smb_settings_api_label[SMB_SETTINGS_APIS] = {"D3D12","D3D11","VULKAN","OPENGL"};
static const int smb_settings_ssaa_value[SMB_SETTINGS_SSAA] = {1,2,4};
static const char* smb_settings_ssaa_label[SMB_SETTINGS_SSAA] = {"OFF","2X","4X"};

void smb_gl_menu_begin(void);void smb_gl_menu_end(void);
static int smb_settings_scale_index=1,smb_settings_fullscreen,smb_settings_api_index,smb_settings_ssaa_index;
static int smb_settings_loaded;
static GLuint smb_settings_font_texture;
static int smb_settings_font_ready;
static void smb_settings_save(void);

static void smb_settings_font_build(void) {
    unsigned char pixels[128*48*4];memset(pixels,0,sizeof pixels);
    for(int i=0;i<95;i++)for(int row=0;row<8;row++){
        unsigned char bits=smb_settings_font_bits[i][row];
        for(int col=0;col<8;col++)if(bits&(1u<<col)){
            int x=(i%16)*8+col,y=(i/16)*8+row,o=(y*128+x)*4;
            pixels[o]=255;pixels[o+1]=255;pixels[o+2]=255;pixels[o+3]=255;
        }
    }
    glGenTextures(1,&smb_settings_font_texture);glBindTexture(GL_TEXTURE_2D,smb_settings_font_texture);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,128,48,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    smb_settings_font_ready=1;
}
static void smb_settings_text_pass(const char* s,float x,float y,float px) {
    glBegin(GL_QUADS);
    for(;*s;s++,x+=8*px) {
        int c=(unsigned char)*s;if(c<32||c>126)continue;
        int i=c-32,cx=(i%16)*8,cy=(i/16)*8;float w=8*px,h=8*px;
        glTexCoord2f(cx/128.f,cy/48.f);glVertex2f(x,y);
        glTexCoord2f((cx+8)/128.f,cy/48.f);glVertex2f(x+w,y);
        glTexCoord2f((cx+8)/128.f,(cy+8)/48.f);glVertex2f(x+w,y+h);
        glTexCoord2f(cx/128.f,(cy+8)/48.f);glVertex2f(x,y+h);
    }
    glEnd();
}
static void smb_settings_text(float x,float y,const char* s,float px,float r,float g,float b,float a) {
    if(!smb_settings_font_ready)smb_settings_font_build();
    glEnable(GL_TEXTURE_2D);glBindTexture(GL_TEXTURE_2D,smb_settings_font_texture);
    glColor4f(0,0,0,a*.7f);smb_settings_text_pass(s,x+px,y+px*1.1f,px);
    glColor4f(r,g,b,a);smb_settings_text_pass(s,x,y,px);
}
static float smb_settings_text_width(const char* s,float px){return (float)strlen(s)*8*px;}
static void smb_settings_text_center(float cx,float y,const char* s,float px,float r,float g,float b,float a) {
    smb_settings_text(cx-smb_settings_text_width(s,px)*.5f,y,s,px,r,g,b,a);
}

static void smb_settings_path(char* out,size_t n) {
    const char* override=getenv("SMB_SETTINGS_PATH");
    if(override&&*override){snprintf(out,n,"%s",override);return;}
#ifdef _WIN32
    const char* appdata=getenv("APPDATA");
    if(appdata&&*appdata){char dir[SYS_MAX_PATH];snprintf(dir,sizeof dir,"%s/Super Melee 64",appdata);mkdir(dir,0755);
        snprintf(out,n,"%s/settings.cfg",dir);return;}
#endif
    snprintf(out,n,"%s/settings.cfg",fs_writepath);
}
static void smb_settings_size(SDL_Window* window,int* out_w,int* out_h) {
    float s=smb_settings_scale_value[smb_settings_scale_index];
    int w=(int)(DESIRED_SCREEN_WIDTH*s+.5f),h=(int)(DESIRED_SCREEN_HEIGHT*s+.5f);
    /* Never ask for a window larger than the desktop that has to show it. */
    SDL_Rect usable;int display=0;
    if(window)display=SDL_GetWindowDisplayIndex(window);
    if(display<0)display=0;
    if(SDL_GetDisplayUsableBounds(display,&usable)==0&&usable.w>0&&usable.h>0) {
        float fit=fminf((usable.w/smb_window_dpi-40.f)/DESIRED_SCREEN_WIDTH,(usable.h/smb_window_dpi-60.f)/DESIRED_SCREEN_HEIGHT);
        if(fit>0.4f&&s>fit){s=fit;w=(int)(DESIRED_SCREEN_WIDTH*s+.5f);h=(int)(DESIRED_SCREEN_HEIGHT*s+.5f);}
    }
    if(w<320)w=320;if(h<240)h=240;*out_w=w;*out_h=h;
}
static void smb_settings_apply_window(SDL_Window* window,int recenter) {
    if(!window)return;
    int w,h;smb_settings_size(window,&w,&h);
    if(smb_settings_fullscreen) {
        SDL_SetWindowFullscreen(window,SDL_WINDOW_FULLSCREEN_DESKTOP);
        int fw,fh;SDL_GetWindowSize(window,&fw,&fh);
        configWindow.w=smb_window_logical(fw);configWindow.h=smb_window_logical(fh);
        configWindow.fullscreen=1;
    } else {
        SDL_SetWindowFullscreen(window,0);
        smb_window_set_size(window,w,h);
        if(recenter)smb_window_set_pos(window,WAPI_WIN_CENTERPOS,WAPI_WIN_CENTERPOS);
        configWindow.w=w;configWindow.h=h;configWindow.fullscreen=0;
        /* Remember where the window actually landed so the boot-time reset
         * keeps that spot instead of snapping back to the old config. */
        int wx,wy;SDL_GetWindowPosition(window,&wx,&wy);
        configWindow.x=smb_window_logical(wx);configWindow.y=smb_window_logical(wy);
    }
    configWindow.settings_changed=true;
}
static void smb_settings_save(void) {
    char path[SYS_MAX_PATH];smb_settings_path(path,sizeof path);
    FILE* f=fopen(path,"wb");
    if(!f){fprintf(stderr,"SMB SETTINGS FAIL save path=%s\n",path);return;}
    fprintf(f,"window_scale=%d\nfullscreen=%d\nrender_api=%s\nssaa=%d\n",
        smb_settings_scale_index,smb_settings_fullscreen,smb_settings_api_value[smb_settings_api_index],smb_settings_ssaa_value[smb_settings_ssaa_index]);
    fclose(f);
}
static void smb_settings_load(SDL_Window* window) {
    if(smb_settings_loaded)return;smb_settings_loaded=1;
    const char* api=getenv("MELEE_BACKEND");
    if(api)for(int i=0;i<SMB_SETTINGS_APIS;i++)if(!strcmp(api,smb_settings_api_value[i]))smb_settings_api_index=i;
    int ssaa=getenv("SMB_SSAA")?atoi(getenv("SMB_SSAA")):1;
    smb_settings_ssaa_index=ssaa==2?1:ssaa==4?2:0;
    if(window) {
        int ww,wh;SDL_GetWindowSize(window,&ww,&wh);float best=1e9f;
        for(int i=0;i<SMB_SETTINGS_SCALES;i++) {
            float d=fabsf(ww-DESIRED_SCREEN_WIDTH*smb_settings_scale_value[i])+fabsf(wh-DESIRED_SCREEN_HEIGHT*smb_settings_scale_value[i]);
            if(d<best){best=d;smb_settings_scale_index=i;}
        }
        smb_settings_fullscreen=(SDL_GetWindowFlags(window)&SDL_WINDOW_FULLSCREEN_DESKTOP)?1:0;
    }
    char path[SYS_MAX_PATH];smb_settings_path(path,sizeof path);
    FILE* f=fopen(path,"rb");
    if(f) {
        char line[160];
        while(fgets(line,sizeof line,f)) {
            char* eq=strchr(line,'=');if(!eq)continue;*eq=0;char* value=eq+1;value[strcspn(value,"\r\n")]=0;
            if(!strcmp(line,"window_scale"))smb_settings_scale_index=atoi(value);
            else if(!strcmp(line,"fullscreen"))smb_settings_fullscreen=atoi(value)?1:0;
            else if(!strcmp(line,"ssaa"))smb_settings_ssaa_index=atoi(value)==2?1:atoi(value)==4?2:0;
            else if(!strcmp(line,"render_api"))for(int i=0;i<SMB_SETTINGS_APIS;i++)if(!strcmp(value,smb_settings_api_value[i]))smb_settings_api_index=i;
        }
        fclose(f);
        smb_settings_apply_window(window,0);
    }
    if(smb_settings_scale_index<0||smb_settings_scale_index>=SMB_SETTINGS_SCALES)smb_settings_scale_index=1;
    char value[16];snprintf(value,sizeof value,"%d",smb_settings_ssaa_value[smb_settings_ssaa_index]);
    setenv("SMB_SSAA",value,1);setenv("MELEE_BACKEND",smb_settings_api_value[smb_settings_api_index],1);
    fprintf(stderr,"SMB SETTINGS load path=%s scale=%s fullscreen=%d api=%s ssaa=%s\n",path,
        smb_settings_scale_label[smb_settings_scale_index],smb_settings_fullscreen,smb_settings_api_value[smb_settings_api_index],value);
}
static void smb_settings_change(SDL_Window* window,int row,int direction,int apply_now) {
    switch(row) {
        case 0:smb_settings_scale_index=(smb_settings_scale_index+direction+SMB_SETTINGS_SCALES)%SMB_SETTINGS_SCALES;
            if(apply_now)smb_settings_apply_window(window,1);break;
        case 1:smb_settings_fullscreen=!smb_settings_fullscreen;
            if(apply_now)smb_settings_apply_window(window,1);break;
        case 2:smb_settings_api_index=(smb_settings_api_index+direction+SMB_SETTINGS_APIS)%SMB_SETTINGS_APIS;
            setenv("MELEE_BACKEND",smb_settings_api_value[smb_settings_api_index],1);break;
        case 3:smb_settings_ssaa_index=(smb_settings_ssaa_index+direction+SMB_SETTINGS_SSAA)%SMB_SETTINGS_SSAA;
            /* Re-sizing the shared render target while the guest is presenting
             * costs the frame; only the boot path may change it safely. */
            if(apply_now){char value[8];snprintf(value,sizeof value,"%d",smb_settings_ssaa_value[smb_settings_ssaa_index]);setenv("SMB_SSAA",value,1);}break;
        default:return;
    }
    smb_settings_save();
    fprintf(stderr,"SMB SETTINGS change row=%d apply=%d scale=%s fullscreen=%d api=%s ssaa=%s\n",row,apply_now,
        smb_settings_scale_label[smb_settings_scale_index],smb_settings_fullscreen,smb_settings_api_value[smb_settings_api_index],smb_settings_ssaa_label[smb_settings_ssaa_index]);
}

static void smb_settings_row_value(int row,char* out,size_t n) {
    switch(row) {
        case 0:snprintf(out,n,"%s",smb_settings_scale_label[smb_settings_scale_index]);break;
        case 1:snprintf(out,n,"%s",smb_settings_fullscreen?"FULLSCREEN":"WINDOWED");break;
        case 2:snprintf(out,n,"%s",smb_settings_api_label[smb_settings_api_index]);break;
        case 3:snprintf(out,n,"%s",smb_settings_ssaa_label[smb_settings_ssaa_index]);break;
        default:snprintf(out,n,"");break;
    }
}
static void smb_settings_open(SDL_Window* window,int runtime) {
    if(runtime)smb_gl_menu_begin();
    glUseProgram(0);glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    static const char* labels[4]={"WINDOW SCALE","DISPLAY MODE","RENDER API","ANTI-ALIASING"};
    int row=0,done=0;
    fprintf(stderr,"SMB SETTINGS open runtime=%d scale=%s fullscreen=%d api=%s ssaa=%s\n",runtime,
        smb_settings_scale_label[smb_settings_scale_index],smb_settings_fullscreen,smb_settings_api_value[smb_settings_api_index],smb_settings_ssaa_label[smb_settings_ssaa_index]);
    while(!done) {
        int ww,wh,w,h;SDL_GetWindowSize(window,&ww,&wh);SDL_GL_GetDrawableSize(window,&w,&h);
        float scale=fminf(ww/960.f,wh/720.f),ox=(ww-960*scale)*.5f,oy=(wh-720*scale)*.5f;
        SDL_Event e;
        while(SDL_PollEvent(&e)) {
            if(e.type==SDL_QUIT)exit(0);
            int move=0,change=0,activate=0;
            if(e.type==SDL_KEYDOWN&&!e.key.repeat) {
                switch(e.key.keysym.scancode) {
                    case SDL_SCANCODE_UP:case SDL_SCANCODE_W:move=-1;break;
                    case SDL_SCANCODE_DOWN:case SDL_SCANCODE_S:case SDL_SCANCODE_TAB:move=1;break;
                    case SDL_SCANCODE_LEFT:case SDL_SCANCODE_A:change=-1;break;
                    case SDL_SCANCODE_RIGHT:case SDL_SCANCODE_D:change=1;break;
                    case SDL_SCANCODE_RETURN:case SDL_SCANCODE_KP_ENTER:case SDL_SCANCODE_SPACE:activate=1;break;
                    case SDL_SCANCODE_ESCAPE:case SDL_SCANCODE_F10:case SDL_SCANCODE_F11:done=1;break;
                    default:break;
                }
            }
            if(e.type==SDL_MOUSEBUTTONDOWN&&e.button.button==SDL_BUTTON_LEFT) {
                float x=(e.button.x-ox)/scale,y=(e.button.y-oy)/scale;
                for(int i=0;i<4;i++) {
                    float ry=150+i*78;
                    if(y>=ry&&y<ry+64&&x>=190&&x<770) {row=i;if(x>=492&&x<532)change=-1;else if(x>=712&&x<752)change=1;else if(x>=540&&x<700)change=1;}
                }
                if(x>=190&&x<770&&y>=472&&y<528){row=4;activate=1;}
            }
            if(e.type==SDL_CONTROLLERBUTTONDOWN) {
                switch(e.cbutton.button) {
                    case SDL_CONTROLLER_BUTTON_DPAD_UP:move=-1;break;
                    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:move=1;break;
                    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:change=-1;break;
                    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:change=1;break;
                    case SDL_CONTROLLER_BUTTON_A:activate=1;break;
                    case SDL_CONTROLLER_BUTTON_B:case SDL_CONTROLLER_BUTTON_START:done=1;break;
                    default:break;
                }
            }
            if(move)row=(row+move+5)%5;
            int live=runtime?0:1;
            if(change&&row<4)smb_settings_change(window,row,change,live);
            if(activate) {if(row==4)done=1;else smb_settings_change(window,row,1,live);}
        }
        glViewport(0,0,w,h);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        glViewport((int)(ox*w/ww),(int)(oy*h/wh),(int)(960*scale*w/ww),(int)(720*scale*h/wh));
        glMatrixMode(GL_PROJECTION);glLoadIdentity();glOrtho(0,960,720,0,-1,1);glMatrixMode(GL_MODELVIEW);glLoadIdentity();
        smb_select_rect(0,0,960,720,.02f,.03f,.06f,.86f);
        float pulse=.55f+.45f*sinf(SDL_GetTicks()*.006f);
        smb_select_rect(168,88,640,552,.02f,.03f,.05f,.9f);
        smb_select_rect(160,80,640,552,1,.75f,.2f,1);
        smb_select_rect(164,84,632,544,.06f,.08f,.12f,1);
        smb_select_rect(164,84,632,46,.75f,.12f,.16f,1);
        smb_select_rect(164,128,632,2,1,.75f,.2f,1);
        smb_settings_text(184,94,"SETTINGS",2.2f,1,1,1,1);
        smb_settings_text(620,104,"SUPER MELEE 64",1.1f,1,.8f,.35f,.9f);
        for(int i=0;i<4;i++) {
            float ry=150+i*78;int selected=i==row;
            if(selected)smb_select_rect(184,ry-4,592,72,1,.75f,.2f,pulse);
            smb_select_rect(190,ry,580,64,selected?.16f:.09f,selected?.19f:.11f,selected?.27f:.16f,1);
            smb_select_rect(190,ry,580,2,selected?1:.25f,selected?.75f:.3f,selected?.2f:.4f,1);
            smb_settings_text(210,ry+24,labels[i],1.7f,selected?1:.85f,selected?1:.89f,selected?1:.96f,1);
            char value[32];smb_settings_row_value(i,value,sizeof value);
            smb_settings_text_center(620,ry+24,value,1.7f,selected?1:.78f,selected?.88f:.82f,selected?.4f:.5f,1);
            if(selected) {
                smb_settings_text(496,ry+24,"<",1.9f,1,.75f,.2f,pulse);
                smb_settings_text(724,ry+24,">",1.9f,1,.75f,.2f,pulse);
            }
        }
        int apply=row==4;
        if(apply)smb_select_rect(184,468,592,64,1,.75f,.2f,pulse);
        smb_select_rect(190,472,580,56,apply?.7f:.22f,apply?.18f:.06f,apply?.1f:.08f,1);
        smb_settings_text_center(480,488,"APPLY & BACK",1.9f,1,.92f,.7f,1);
        {
            char hint[128];int W=0,H=0;smb_settings_size(window,&W,&H);
            if(runtime)snprintf(hint,sizeof hint,"TAKES EFFECT ON NEXT LAUNCH");
            else snprintf(hint,sizeof hint,"OUTPUT %dX%d   SSAA %s",W,H,smb_settings_ssaa_label[smb_settings_ssaa_index]);
            smb_settings_text_center(480,548,hint,1.15f,1,.8f,.4f,.9f);
        }
        smb_settings_text_center(480,582,"UP/DOWN SELECT   LEFT/RIGHT CHANGE   ENTER APPLY   ESC BACK",1.05f,.65f,.7f,.8f,1);
        SDL_GL_SwapWindow(window);
        SDL_Delay(16);
    }
    SDL_Event stale;while(SDL_PollEvent(&stale))if(stale.type==SDL_QUIT)exit(0);
    glDisable(GL_BLEND);glDisable(GL_TEXTURE_2D);
    if(runtime){smb_gl_menu_end();fprintf(stderr,"SMB SETTINGS CLOSE gl_error=%u\n",(unsigned)glGetError());}
    smb_settings_save();
}
static void smb_host_settings_menu(SDL_Window* window) {smb_settings_open(window,1);}
#endif
