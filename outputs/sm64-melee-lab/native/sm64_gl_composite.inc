#include "sm64_record_capture.inc"
/* Native C + world*T carries alpha, additive, subtractive and darkening FX. */
static unsigned smb_capture_frames,smb_capture_w,smb_capture_h;
static GLuint smb_comp_program,smb_comp_textures[2][3],smb_comp_dummy;
static GLint smb_comp_layer;
static void smb_comp_init(void) {
    const char* vertex="#version 120\nvarying vec2 uv;void main(){gl_Position=gl_Vertex;uv=gl_MultiTexCoord0.xy;}";
    const char* fragment="#version 120\nuniform sampler2D rgbaTex;uniform sampler2D depthTex;uniform sampler2D transmissionTex;uniform int layer;varying vec2 uv;void main(){vec4 c=texture2D(rgbaTex,uv);vec3 t=texture2D(transmissionTex,uv).rgb;if(max(max(abs(c.r),abs(c.g)),abs(c.b))<0.00001&&min(min(t.r,t.g),t.b)>0.99999)discard;if(layer==3&&max(max(t.r,t.g),t.b)>0.001)discard;gl_FragDepth=1.0-texture2D(depthTex,uv).r;gl_FragColor=layer==0?vec4(t,1.0):layer==1?vec4(max(-c.rgb,vec3(0.0)),0.0):vec4(max(c.rgb,vec3(0.0)),0.0);}";
    GLuint vs=glCreateShader(GL_VERTEX_SHADER),fs=glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(vs,1,&vertex,NULL);glCompileShader(vs);glShaderSource(fs,1,&fragment,NULL);glCompileShader(fs);
    smb_comp_program=glCreateProgram();glAttachShader(smb_comp_program,vs);glAttachShader(smb_comp_program,fs);glLinkProgram(smb_comp_program);
    GLint good=0;glGetProgramiv(smb_comp_program,GL_LINK_STATUS,&good);
    if(!good){char error[4096];glGetProgramInfoLog(smb_comp_program,sizeof error,NULL,error);fprintf(stderr,"SMB GL FAIL %s\n",error);exit(2);}
    glDeleteShader(vs);glDeleteShader(fs);glGenTextures(3,smb_comp_textures[0]);glGenTextures(3,smb_comp_textures[1]);
    glUseProgram(smb_comp_program);
    glUniform1i(glGetUniformLocation(smb_comp_program,"rgbaTex"),0);
    glUniform1i(glGetUniformLocation(smb_comp_program,"depthTex"),1);
    glUniform1i(glGetUniformLocation(smb_comp_program,"transmissionTex"),2);
    smb_comp_layer=glGetUniformLocation(smb_comp_program,"layer");
    /* The overlay pass runs with depth testing off and never writes depth, so
     * nothing reads the depth plane: bind a 1x1 stand-in instead of uploading a
     * full LUMINANCE32F image per frame for a value the draw cannot observe. */
    glGenTextures(1,&smb_comp_dummy);glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,smb_comp_dummy);
    float zero=0.0f;
    glTexImage2D(GL_TEXTURE_2D,0,GL_LUMINANCE32F_ARB,1,1,0,GL_LUMINANCE,GL_FLOAT,&zero);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glActiveTexture(GL_TEXTURE0);glUseProgram(0);
    if(GLEW_ARB_color_buffer_float)glClampColorARB(GL_CLAMP_FRAGMENT_COLOR_ARB,GL_FALSE);
}
static void smb_comp_upload(GLuint texture,int unit,unsigned w,unsigned h,int vertical_nearest,GLint internal,GLenum format,GLenum type,const void* data) {
    glActiveTexture(GL_TEXTURE0+unit);glBindTexture(GL_TEXTURE_2D,texture);
    glTexImage2D(GL_TEXTURE_2D,0,internal,w,h,0,format,type,data);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,vertical_nearest?GL_NEAREST:GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,vertical_nearest?GL_NEAREST:GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
}
void smb_gl_composite(const float* rgba,const float* depth,const unsigned char* transmission,unsigned w,unsigned h,int overlay) {
    if(!smb_comp_program)smb_comp_init();
    GLint previous;glGetIntegerv(GL_CURRENT_PROGRAM,&previous);
    /* glPushAttrib(GL_ALL_ATTRIB_BITS) resolved every pending attribute group,
     * a hard pipeline flush twice per frame. The fast3d renderer re-establishes
     * program, depth, blend and color mask per draw, so only the switches it
     * leaves alone are saved and restored here. */
    GLboolean cull=glIsEnabled(GL_CULL_FACE),offset=glIsEnabled(GL_POLYGON_OFFSET_FILL),depth_test=glIsEnabled(GL_DEPTH_TEST);
    glUseProgram(smb_comp_program);glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    /* Ping-pong the upload textures: a single set forces the driver to wait
     * for the previous frame's draw before every glTexSubImage2D (a hard
     * stall visible as nvoglv64 waits). Alternating lets the transfer overlap. */
    static unsigned allocated_w,allocated_h;static unsigned char valid[2][3];static unsigned buffer;
    if(allocated_w!=w||allocated_h!=h){allocated_w=w;allocated_h=h;memset(valid,0,sizeof valid);}
    buffer^=1;GLuint* set=smb_comp_textures[buffer];
    int skip_depth=overlay;
    for(int i=0;i<3;i++) {
        if(i==1&&skip_depth){glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,smb_comp_dummy);continue;}
        if(!valid[buffer][i]) {
            smb_comp_upload(set[i],i,w,h,i==1,i==0?GL_RGBA16F_ARB:i==1?GL_LUMINANCE32F_ARB:GL_RGBA8,i==1?GL_LUMINANCE:GL_RGBA,i==2?GL_UNSIGNED_BYTE:GL_FLOAT,i==0?(const void*)rgba:i==1?(const void*)depth:(const void*)transmission);
            valid[buffer][i]=1;continue;
        }
        glActiveTexture(GL_TEXTURE0+i);glBindTexture(GL_TEXTURE_2D,set[i]);
        glTexSubImage2D(GL_TEXTURE_2D,0,0,0,w,h,i==1?GL_LUMINANCE:GL_RGBA,i==2?GL_UNSIGNED_BYTE:GL_FLOAT,i==0?(const void*)rgba:i==1?(const void*)depth:(const void*)transmission);
    }
    float backdrop[5][3];static unsigned diagnostic_frame;
    int probe=getenv("SMB_RENDER_DIAGNOSTIC")&&++diagnostic_frame==91;
    if(probe)for(unsigned i=0;i<5;i++)glReadPixels(w*(48+80*i)/640,h-1-h*152/480,1,1,GL_RGB,GL_FLOAT,backdrop[i]);
    if(overlay)glDisable(GL_DEPTH_TEST);else glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);glDepthMask(GL_FALSE);glDisable(GL_CULL_FACE);glDisable(GL_POLYGON_OFFSET_FILL);
    glEnable(GL_BLEND);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_FALSE);
    for(int layer=0;layer<(overlay?3:4);layer++) {
        if(layer==3){glDepthMask(GL_TRUE);glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);}
        glUniform1i(smb_comp_layer,layer);
        glBlendEquation(layer==1?GL_FUNC_REVERSE_SUBTRACT:GL_FUNC_ADD);
        glBlendFunc(layer?GL_ONE:GL_ZERO,layer?GL_ONE:GL_SRC_COLOR);
        glBegin(GL_TRIANGLE_STRIP);
        glTexCoord2f(0,1);glVertex2f(-1,-1);glTexCoord2f(1,1);glVertex2f(1,-1);
        glTexCoord2f(0,0);glVertex2f(-1,1);glTexCoord2f(1,0);glVertex2f(1,1);
        glEnd();
    }
    if(probe)for(unsigned i=0;i<5;i++) {
        unsigned x=w*(48+80*i)/640,y=h*152/480,j=(y*w+x)*4;
        float actual[3],error=0;glReadPixels(x,h-1-y,1,1,GL_RGB,GL_FLOAT,actual);
        for(unsigned k=0;k<3;k++) {
            float expected=fmaxf(0,fminf(1,rgba[j+k]+backdrop[i][k]*transmission[j+k]/255.f));
            error=fmaxf(error,fabsf(expected-actual[k]));
        }
        fprintf(stderr,"SMB COMPOSITE WORLD mode=%u error=%.6f pass=%d\n",i,error,error<.015f);
    }
    if(!overlay){smb_capture_frames++;smb_capture_w=w;smb_capture_h=h;}
    glActiveTexture(GL_TEXTURE0);
    glDepthMask(GL_TRUE);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glDepthFunc(GL_LEQUAL);
    glBlendEquation(GL_FUNC_ADD);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glDisable(GL_BLEND);
    if(depth_test)glEnable(GL_DEPTH_TEST);else glDisable(GL_DEPTH_TEST);
    if(cull)glEnable(GL_CULL_FACE);else glDisable(GL_CULL_FACE);
    if(offset)glEnable(GL_POLYGON_OFFSET_FILL);
    glUseProgram(previous);
}
/* Capture after SM64 has also drawn held objects, scenery and its lives count. */
void smb_host_render_hud(void);
unsigned smb_host_title_frames(void);
void smb_gl_capture_frame(void) {
    if(getenv("SMB_TITLE_PREVIEW")&&smb_host_title_frames()>=60) {
        GLint viewport[4];glGetIntegerv(GL_VIEWPORT,viewport);unsigned w=viewport[2],h=viewport[3];
        unsigned char* pixels=malloc((size_t)w*h*4);glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
        FILE* f=fopen(getenv("SMB_TITLE_CAPTURE"),"wb");if(f){fwrite(&w,4,1,f);fwrite(&h,4,1,f);fwrite(pixels,4,(size_t)w*h,f);fclose(f);}free(pixels);
        fprintf(stderr,"SMB TITLE COMPLETE original_sm64_scene=1 frames=%u\n",smb_host_title_frames());exit(0);
    }
    smb_host_render_hud();
    smb_record_capture();
    if(getenv("SMB_SMOKE")&&getenv("SMB_SCENARIO")&&!strcmp(getenv("SMB_SCENARIO"),"powerups")&&getenv("SMB_SCENE_CAPTURE")) {
        extern unsigned smb_host_game_frames(void);unsigned frame=smb_host_game_frames();
        static unsigned captured;unsigned bit=frame==30?1:frame==60?2:frame==100?4:frame==140?8:frame==180?16:0;
        if(bit&&!(captured&bit)) {
            captured|=bit;GLint viewport[4];glGetIntegerv(GL_VIEWPORT,viewport);unsigned w=viewport[2],h=viewport[3];
            unsigned char* pixels=malloc((size_t)w*h*4);glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
            char path[4096];snprintf(path,sizeof path,"%s.%u.rgba",getenv("SMB_SCENE_CAPTURE"),frame);FILE* f=fopen(path,"wb");
            if(f){fwrite(&w,4,1,f);fwrite(&h,4,1,f);fwrite(pixels,4,(size_t)w*h,f);fclose(f);}free(pixels);
        }
    }
    static int saved;if(saved||smb_capture_frames<90||!getenv("SMB_SCENE_CAPTURE"))return;
    saved=1;unsigned w=smb_capture_w,h=smb_capture_h;
    unsigned char* pixels=malloc((size_t)w*h*4);glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    FILE* f=fopen(getenv("SMB_SCENE_CAPTURE"),"wb");
    if(f){fwrite(&w,4,1,f);fwrite(&h,4,1,f);fwrite(pixels,4,(size_t)w*h,f);fclose(f);}free(pixels);
}

static GLint smb_menu_program,smb_menu_framebuffer,smb_menu_unpack;
void smb_gl_menu_begin(void) {
    glGetIntegerv(GL_CURRENT_PROGRAM,&smb_menu_program);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING,&smb_menu_framebuffer);
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&smb_menu_unpack);
    glPushAttrib(GL_ALL_ATTRIB_BITS);glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);
    glMatrixMode(GL_PROJECTION);glPushMatrix();glMatrixMode(GL_MODELVIEW);glPushMatrix();
    glBindFramebuffer(GL_FRAMEBUFFER,0);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);glUseProgram(0);
    for(int i=0;i<3;i++){glActiveTexture(GL_TEXTURE0+i);glDisable(GL_TEXTURE_2D);}
    glActiveTexture(GL_TEXTURE0);glDisable(GL_SCISSOR_TEST);glDisable(GL_ALPHA_TEST);glDisable(GL_LIGHTING);glDisable(GL_FOG);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glDepthMask(GL_TRUE);
    glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
}
void smb_gl_menu_end(void) {
    glMatrixMode(GL_MODELVIEW);glPopMatrix();glMatrixMode(GL_PROJECTION);glPopMatrix();
    glBindFramebuffer(GL_FRAMEBUFFER,smb_menu_framebuffer);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,smb_menu_unpack);
    glUseProgram(smb_menu_program);glPopClientAttrib();glPopAttrib();
}