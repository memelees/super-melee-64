/* Both engines share the same render resolution and depth buffer. Cap the
 * complete scene at Full HD before presentation, rather than downsampling
 * just the fighters. This bounds the synchronous Metal -> OpenGL transfer. */
static GLuint smb_video_fbo,smb_video_color,smb_video_depth;
static int smb_video_w,smb_video_h;
static Uint64 smb_video_begin,smb_video_previous;
static double smb_video_work[300],smb_video_period[300];
static unsigned smb_video_samples;
static int smb_video_compare(const void* a,const void* b){double x=*(const double*)a,y=*(const double*)b;return (x>y)-(x<y);}
static void smb_video_dimensions(SDL_Window* window,int* w,int* h) {
    SDL_GL_GetDrawableSize(window,w,h);
    int limit=getenv("SMB_RENDER_MAX_HEIGHT")?atoi(getenv("SMB_RENDER_MAX_HEIGHT")):1080;
    if(limit>0&&*h>limit){*w=(int)((double)*w*limit/(*h));*h=limit;}
}
static void smb_video_start(SDL_Window* window) {
    if(!getenv("SM64_MELEE_LIBRARY"))return;
    int w,h;smb_video_dimensions(window,&w,&h);if(w<1||h<1)return;
    if(!smb_video_fbo){glGenFramebuffers(1,&smb_video_fbo);glGenRenderbuffers(1,&smb_video_color);glGenRenderbuffers(1,&smb_video_depth);}
    glBindFramebuffer(GL_FRAMEBUFFER,smb_video_fbo);
    if(w!=smb_video_w||h!=smb_video_h) {
        smb_video_w=w;smb_video_h=h;
        glBindRenderbuffer(GL_RENDERBUFFER,smb_video_color);glRenderbufferStorage(GL_RENDERBUFFER,GL_RGBA8,w,h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,smb_video_color);
        glBindRenderbuffer(GL_RENDERBUFFER,smb_video_depth);glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,w,h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,smb_video_depth);
        glBindRenderbuffer(GL_RENDERBUFFER,0);
        if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE){fprintf(stderr,"SMB VIDEO FAIL framebuffer\n");exit(2);}
        fprintf(stderr,"SMB VIDEO resolution=%dx%d both_engines=1 target_hz=30 native_hz=60\n",w,h);
    }
    if(!smb_video_begin)smb_video_begin=SDL_GetPerformanceCounter();
}
static void smb_video_present(SDL_Window* window) {
    if(!smb_video_fbo)return;
    Uint64 now=SDL_GetPerformanceCounter();double freq=SDL_GetPerformanceFrequency();
    if(smb_video_previous) {
        unsigned i=smb_video_samples++%300;
        smb_video_work[i]=(now-smb_video_begin)*1000/freq;
        smb_video_period[i]=(now-smb_video_previous)*1000/freq;
        if(smb_video_samples%300==0) {
            double work[300],period[300],total=0;unsigned late=0;
            memcpy(work,smb_video_work,sizeof work);memcpy(period,smb_video_period,sizeof period);
            for(int j=0;j<300;j++){total+=period[j];late+=work[j]>33.34;}
            qsort(work,300,sizeof(double),smb_video_compare);qsort(period,300,sizeof(double),smb_video_compare);
            fprintf(stderr,"SMB PERFORMANCE fps=%.2f render_median_ms=%.2f render_p95_ms=%.2f frame_p95_ms=%.2f over_budget=%u/300 resolution=%dx%d\n",300000/total,work[150],work[284],period[284],late,smb_video_w,smb_video_h);
        }
    }
    smb_video_previous=now;
    int w,h;SDL_GL_GetDrawableSize(window,&w,&h);
    glPushAttrib(GL_SCISSOR_BIT);glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,smb_video_fbo);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,0);
    glBlitFramebuffer(0,0,smb_video_w,smb_video_h,0,0,w,h,GL_COLOR_BUFFER_BIT,GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER,0);glPopAttrib();
}
