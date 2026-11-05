/* Downsample the completed game framebuffer on the GPU, before readback. */
int smb_record_wanted(void);
int smb_record_scene_ready(void);
int smb_record_active(void);
void smb_record_video(unsigned,unsigned,const unsigned char*);
static void smb_record_capture(void) {
#ifdef __APPLE__
    extern void smb_record_menu_update(void);smb_record_menu_update();
#else
    SDL_Window* titleWindow=SDL_GL_GetCurrentWindow();if(titleWindow)SDL_SetWindowTitle(titleWindow,smb_record_active()?"Super Melee 64 — REC":"Super Melee 64");
#endif
    if(!smb_record_scene_ready()||!smb_record_wanted())return;
    static GLuint framebuffer,texture;static unsigned width,height,allocatedW,allocatedH;
    GLint viewport[4],oldRead,oldDraw,oldTexture,oldPack;
    glGetIntegerv(GL_VIEWPORT,viewport);unsigned w=viewport[2],h=viewport[3];if(!w||!h)return;
    if(!smb_record_active()) {
        float scale=fminf(1,fminf(1920.f/w,1080.f/h));width=((unsigned)(w*scale))&~1u;height=((unsigned)(h*scale))&~1u;
    }
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&oldRead);glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&oldDraw);
    glGetIntegerv(GL_TEXTURE_BINDING_2D,&oldTexture);glGetIntegerv(GL_PACK_ALIGNMENT,&oldPack);
    if(!framebuffer){glGenFramebuffers(1,&framebuffer);glGenTextures(1,&texture);}
    GLboolean scissor=glIsEnabled(GL_SCISSOR_TEST);GLfloat clear[4];glGetFloatv(GL_COLOR_CLEAR_VALUE,clear);glDisable(GL_SCISSOR_TEST);
    glBindTexture(GL_TEXTURE_2D,texture);
    if(allocatedW!=width||allocatedH!=height){glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,height,0,GL_BGRA,GL_UNSIGNED_BYTE,NULL);allocatedW=width;allocatedH=height;}
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,framebuffer);glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
    glViewport(0,0,width,height);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    float scale=fminf(width/(float)w,height/(float)h);int outW=w*scale,outH=h*scale,x=(width-outW)/2,y=(height-outH)/2;
    glBlitFramebuffer(viewport[0],viewport[1],viewport[0]+w,viewport[1]+h,x,y,x+outW,y+outH,GL_COLOR_BUFFER_BIT,GL_LINEAR);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,framebuffer);glPixelStorei(GL_PACK_ALIGNMENT,1);
    unsigned char* data=malloc((size_t)width*height*4);glReadPixels(0,0,width,height,GL_BGRA,GL_UNSIGNED_BYTE,data);
    smb_record_video(width,height,data);free(data);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,oldRead);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,oldDraw);glBindTexture(GL_TEXTURE_2D,oldTexture);glPixelStorei(GL_PACK_ALIGNMENT,oldPack);glViewport(viewport[0],viewport[1],w,h);
    if(scissor)glEnable(GL_SCISSOR_TEST);glClearColor(clear[0],clear[1],clear[2],clear[3]);
}
