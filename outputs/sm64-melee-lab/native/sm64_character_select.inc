#ifndef SMB_CHARACTER_SELECT_INC
#define SMB_CHARACTER_SELECT_INC
/* One SM64 window, local-disc Melee CSS artwork, keyboard/mouse/controller. */
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
void smb_host_boot(void);
void smb_record_set_wanted(int);
int smb_record_wanted(void);
#ifdef __APPLE__
void smb_record_menu_update(void);
#endif
#include "fs/fs.h"
#include "sm64_stage_starts.inc"
#include "configfile.h"
#include "cliopts.h"
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define mkdir(path,mode) _mkdir(path)
#define setenv(name,value,overwrite) SDL_setenv(name,value,overwrite)
#define unsetenv(name) _putenv_s(name,"")
#endif
static void smb_select_save_mode(int mode) {
    int stage=getenv("SMB_START_STAGE")?atoi(getenv("SMB_START_STAGE")):-1;
    if(stage>=0&&stage<27) {
        /* Never mix a recording route's earned progress with the 120-star
         * sandbox or a normal adventure save. Each course resumes its stars. */
        char root[SYS_MAX_PATH],path[SYS_MAX_PATH];
        snprintf(root,sizeof root,"%s",getenv("SMB_PROFILE_RECOMMENDED")?:fs_writepath);
        mkdir(root,0755);snprintf(path,sizeof path,"%s/stage-%02d",root,stage);
        mkdir(path,0755);snprintf(fs_writepath,SYS_MAX_PATH,"%s",path);
        setenv("SMB_SAVE_MODE","recommended",1);unsetenv("SMB_ALL_STARS");
        configSkipIntro=true;gCLIOpts.SkipIntro=1;
        fprintf(stderr,"SMB SAVE PROFILE mode=recommended stage=%d path=%s\n",stage,fs_writepath);
        return;
    }
    const char* names[]={"unlocked","continue","new"};
    const char* keys[]={"SMB_PROFILE_UNLOCKED","SMB_PROFILE_CONTINUE","SMB_PROFILE_NEW"};
    const char* path=getenv(keys[mode]);char fresh_path[SYS_MAX_PATH];
    if(mode==2&&path) {
        char save[SYS_MAX_PATH];snprintf(save,sizeof save,"%s/sm64_save_file.bin",path);FILE* existing=fopen(save,"rb");
        if(existing){fclose(existing);snprintf(fresh_path,sizeof fresh_path,"%s/new-%llu",path,(unsigned long long)SDL_GetPerformanceCounter());path=fresh_path;}
    }
    if(path){mkdir(path,0755);snprintf(fs_writepath,SYS_MAX_PATH,"%s",path);}
    setenv("SMB_SAVE_MODE",names[mode],1);
    if(mode){unsetenv("SMB_ALL_STARS");configSkipIntro=false;gCLIOpts.SkipIntro=0;}
    else {if(!getenv("SMB_SMOKE")||getenv("SMB_ALL_STARS"))setenv("SMB_ALL_STARS","1",1);configSkipIntro=true;gCLIOpts.SkipIntro=1;}
    if(mode==2&&!getenv("SMB_SMOKE")&&getenv("SMB_PLAYTHROUGH_LATEST")) {
        FILE* latest=fopen(getenv("SMB_PLAYTHROUGH_LATEST"),"w");if(latest){fprintf(latest,"%s\n",fs_writepath);fclose(latest);}
    }
    fprintf(stderr,"SMB SAVE PROFILE mode=%s path=%s\n",names[mode],fs_writepath);
}
typedef struct {unsigned character,x,y,w,h,kind,costumes;char name[24];} SmbSelectCell;
static void smb_select_rect(float x,float y,float w,float h,float r,float g,float b,float a) {
    glDisable(GL_TEXTURE_2D);glColor4f(r,g,b,a);glBegin(GL_QUADS);
    glVertex2f(x,y);glVertex2f(x+w,y);glVertex2f(x+w,y+h);glVertex2f(x,y+h);glEnd();
}
static void smb_select_sprite(GLuint tex,float x,float y,float w,float h,float sx,float sy,float sw,float sh,float tw,float th) {
    glEnable(GL_TEXTURE_2D);glBindTexture(GL_TEXTURE_2D,tex);glColor4f(1,1,1,1);glBegin(GL_QUADS);
    glTexCoord2f(sx/tw,sy/th);glVertex2f(x,y);glTexCoord2f((sx+sw)/tw,sy/th);glVertex2f(x+w,y);
    glTexCoord2f((sx+sw)/tw,(sy+sh)/th);glVertex2f(x+w,y+h);glTexCoord2f(sx/tw,(sy+sh)/th);glVertex2f(x,y+h);glEnd();
}
static GLuint smb_select_texture(unsigned w,unsigned h,FILE* file) {
    unsigned char* pixels=malloc((size_t)w*h*4);if(!pixels||fread(pixels,4,(size_t)w*h,file)!=(size_t)w*h)exit(2);
    GLuint tex;glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);free(pixels);return tex;
}
static void smb_select_named(SmbSelectCell* cells,const char* name,int* pick,int* sheik) {
    if(!name)return;
    if(!strcmp(name,"sheik")){*pick=19;*sheik=1;return;}
    for(int i=0;i<32;i++)if(!strcmp(cells[i].name,name)){*pick=i;*sheik=0;return;}
}
int smb_host_change_menu_ready(void);
void smb_host_change_menu_request(void);void smb_host_change_menu_closed(void);
int smb_host_change_character(unsigned,unsigned,const char*);
int smb_host_spawn_enemy(unsigned,unsigned,unsigned);
void smb_gl_menu_begin(void);void smb_gl_menu_end(void);
static void smb_select_costume(int* costume,unsigned count,int direction) {
    if(count)*costume=(*costume+(direction>0?1:(int)count-1))%count;
}
static void smb_host_select(SDL_Window* window,int runtime) {
#ifdef __APPLE__
    smb_record_menu_update();
#endif
    const char* configured_mode=getenv("SMB_SAVE_MODE");
    int save_mode=configured_mode&&!strcmp(configured_mode,"new")?2:configured_mode&&!strcmp(configured_mode,"continue")?1:0;
    int record=getenv("SMB_RECORD_AUTO")&&atoi(getenv("SMB_RECORD_AUTO"));
    if(!runtime)smb_record_set_wanted(record);
    if(!runtime&&!getenv("SMB_CHARACTER_SELECT")){smb_select_save_mode(save_mode);return;}
    if(runtime)smb_gl_menu_begin();
    FILE* f=fopen(getenv("SMB_SELECTION_ART"),"rb");unsigned header[6];SmbSelectCell cells[32];
    if(!f||fread(header,4,6,f)!=6||memcmp(header,"SSC5",4)||header[1]!=960||header[2]!=720||header[5]!=32||fread(cells,sizeof cells,1,f)!=1){fprintf(stderr,"SMB SELECT FAIL artwork\n");exit(2);}
    GLuint bg=smb_select_texture(960,720,f),atlas=smb_select_texture(512,4096,f),portraits=smb_select_texture(952,6204,f);fclose(f);
    int course=runtime||getenv("SMB_COURSE_ENEMIES")!=NULL;
    const char* encounter_modes[]={"default","impossible","none"};
    int encounter_mode=getenv("SMB_DIFFICULTY")&&!strcmp(getenv("SMB_DIFFICULTY"),"none")?2:
        getenv("SMB_DIFFICULTY")&&!strcmp(getenv("SMB_DIFFICULTY"),"impossible")?1:0;
    int pick[4]={20,12,29,13},sheik[4]={0},slot=0,cpus=getenv("SMB_CPU_COUNT")?atoi(getenv("SMB_CPU_COUNT")):3;
    if(cpus<0)cpus=0;if(cpus>3)cpus=3;
    smb_select_named(cells,getenv("MELEE3D_NATIVE_CHARACTER"),&pick[0],&sheik[0]);
    char configured[128];snprintf(configured,sizeof configured,"%s",getenv("SMB_CPU_CHARACTERS")?getenv("SMB_CPU_CHARACTERS"):"fox,marth,wolf");
    char* token=strtok(configured,",");for(int i=1;i<4&&token;i++,token=strtok(NULL,","))smb_select_named(cells,token,&pick[i],&sheik[i]);
    int route=getenv("SMB_START_STAGE")?atoi(getenv("SMB_START_STAGE")):-1;
    if(route< -1||route>=27)route=-1;
    int costume=getenv("SMB_PLAYER_COSTUME")?atoi(getenv("SMB_PLAYER_COSTUME")):0;
    if(costume<0||costume>=cells[pick[0]].costumes)costume=0;
    if(runtime&&getenv("SMB_SWITCH_TEST_NAME")) {
        smb_select_named(cells,getenv("SMB_SWITCH_TEST_NAME"),&pick[0],&sheik[0]);
        costume=atoi(getenv("SMB_SWITCH_TEST_COSTUME"));
    }
    int automated=getenv("SMB_SELECT_AUTOSTART")!=NULL;
    SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);SDL_GameController* pad=NULL;
    for(int i=0;i<SDL_NumJoysticks();i++)if(SDL_IsGameController(i)){pad=SDL_GameControllerOpen(i);break;}
    glUseProgram(0);glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    int trigger_latch[2]={0};unsigned ticks=0,last_stick=0;int done=0,cancel=0;fprintf(stderr,"SMB SELECT OPEN native_portraits=32 forms=33 window=sm64 editable_slots=4\n");
    fprintf(stderr,"SMB SELECT CONTEXT runtime=%d costume=%d\n",runtime,costume);
    while(!done) {
#ifdef __APPLE__
        if(!runtime)record=smb_record_wanted();
        smb_record_menu_update();
#endif
        int prior_record=record;
        int ww,wh,w,h;SDL_GetWindowSize(window,&ww,&wh);SDL_GL_GetDrawableSize(window,&w,&h);
        float scale=fminf(ww/960.f,wh/720.f),ox=(ww-960*scale)*.5f,oy=(wh-720*scale)*.5f;
        SDL_Event e;while(SDL_PollEvent(&e)) {
            if(e.type==SDL_QUIT)exit(0);
            if(automated)continue; /* Deterministic smoke choice, independent of a live controller. */
            int move=0,route_move=0;
            if(e.type==SDL_KEYDOWN&&!e.key.repeat) {
                switch(e.key.keysym.scancode) {
                    case SDL_SCANCODE_LEFTBRACKET:if(!runtime)route_move=-1;break;
                    case SDL_SCANCODE_RIGHTBRACKET:if(!runtime)route_move=1;break;
                    case SDL_SCANCODE_RIGHT:move=1;break;
                    case SDL_SCANCODE_LEFT:move=31;break;
                    case SDL_SCANCODE_DOWN:move=11;break;
                    case SDL_SCANCODE_UP:move=21;break;
                    case SDL_SCANCODE_TAB:if(course)break;slot=(slot+1)%(cpus+1);break;
                    case SDL_SCANCODE_P:if(runtime)break;save_mode=(save_mode+1)%3;route=-1;break;
                    case SDL_SCANCODE_R:if(runtime||(e.key.keysym.mod&KMOD_GUI))break;record=!record;break;
                    case SDL_SCANCODE_M:if(runtime)break;if(course)encounter_mode=(encounter_mode+1)%3;break;
                    case SDL_SCANCODE_C:if(course)break;cpus=(cpus+1)%4;if(slot>cpus)slot=0;break;
                    case SDL_SCANCODE_V:if(slot==0)smb_select_costume(&costume,cells[pick[0]].costumes,(e.key.keysym.mod&KMOD_SHIFT)?-1:1);break;
                    case SDL_SCANCODE_Z:if(cells[pick[slot]].character==18)sheik[slot]=!sheik[slot];break;
                    case SDL_SCANCODE_RETURN:case SDL_SCANCODE_SPACE:done=1;break;
                    case SDL_SCANCODE_ESCAPE:if(runtime){cancel=1;done=1;break;}exit(0);
                    default:break;
                }
            }
            if(e.type==SDL_MOUSEBUTTONDOWN&&e.button.button==SDL_BUTTON_LEFT) {
                float x=(e.button.x-ox)/scale,y=(e.button.y-oy)/scale;
                for(int i=0;i<32;i++)if(x>=cells[i].x&&x<cells[i].x+cells[i].w&&y>=cells[i].y&&y<cells[i].y+cells[i].h){if(slot==0&&pick[slot]!=i)costume=0;pick[slot]=i;sheik[slot]=0;}
                for(int i=0;i<(course?1:4);i++)if(x>=40+i*226&&x<252+i*226&&y>=380&&y<640){slot=i;if(i>cpus)cpus=i;}
                if(!runtime&&course&&y>=492&&y<524){if(x>=290&&x<450)encounter_mode=0;if(x>=466&&x<646)encounter_mode=1;if(x>=662&&x<886)encounter_mode=2;}
                if(!runtime&&y>=102&&y<122){for(int i=0;i<3;i++)if(x>=100+i*180&&x<260+i*180){save_mode=i;route=-1;}if(x>=700&&x<920)record=!record;}
                if(slot==0&&y>=480&&y<540) {
                    if(x>=40&&x<68)smb_select_costume(&costume,cells[pick[0]].costumes,-1);
                    if(x>=224&&x<252)smb_select_costume(&costume,cells[pick[0]].costumes,1);
                }
                if(course&&y>=(runtime?420:380)&&y<(runtime?536:444)) {
                    float step=runtime?84:62;int k=(int)((x-310)/step);
                    if(x>=310&&k>=0&&k<(int)cells[pick[0]].costumes)costume=k;
                }
                if(!runtime&&course&&x>=290&&x<900&&y>=546&&y<590)route_move=x<600?-1:1;
                if(x>=28&&x<931&&y>=646&&y<690)done=1;
            }
            if(e.type==SDL_CONTROLLERBUTTONDOWN) {
                if(e.cbutton.button==SDL_CONTROLLER_BUTTON_BACK){if(runtime){cancel=1;done=1;}else {save_mode=(save_mode+1)%3;route=-1;}}
                if(runtime&&e.cbutton.button==SDL_CONTROLLER_BUTTON_B){cancel=1;done=1;}
                if(!runtime&&course&&e.cbutton.button==SDL_CONTROLLER_BUTTON_LEFTSHOULDER)route_move=-1;
                if(!runtime&&course&&e.cbutton.button==SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)route_move=1;
                if(!runtime&&e.cbutton.button==SDL_CONTROLLER_BUTTON_RIGHTSTICK)record=!record;
                if(e.cbutton.button==SDL_CONTROLLER_BUTTON_START||e.cbutton.button==SDL_CONTROLLER_BUTTON_A)done=1;
                if(!runtime&&course&&e.cbutton.button==SDL_CONTROLLER_BUTTON_X)encounter_mode=(encounter_mode+1)%3;
                if(!course&&e.cbutton.button==SDL_CONTROLLER_BUTTON_X){cpus=(cpus+1)%4;if(slot>cpus)slot=0;}
                if(!course&&e.cbutton.button==SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)slot=(slot+1)%(cpus+1);
                if(!course&&e.cbutton.button==SDL_CONTROLLER_BUTTON_LEFTSHOULDER)slot=(slot+cpus)%(cpus+1);
                if(e.cbutton.button==SDL_CONTROLLER_BUTTON_Y&&cells[pick[slot]].character==18)sheik[slot]=!sheik[slot];
                if(e.cbutton.button==SDL_CONTROLLER_BUTTON_DPAD_RIGHT)move=1;
                if(e.cbutton.button==SDL_CONTROLLER_BUTTON_DPAD_LEFT)move=31;
                if(e.cbutton.button==SDL_CONTROLLER_BUTTON_DPAD_DOWN)move=11;
                if(e.cbutton.button==SDL_CONTROLLER_BUTTON_DPAD_UP)move=21;
            }
            if(e.type==SDL_CONTROLLERAXISMOTION&&slot==0) {
                int side=e.caxis.axis==SDL_CONTROLLER_AXIS_TRIGGERLEFT?0:e.caxis.axis==SDL_CONTROLLER_AXIS_TRIGGERRIGHT?1:-1;
                if(side>=0){if(e.caxis.value>16000&&!trigger_latch[side])smb_select_costume(&costume,cells[pick[0]].costumes,side?1:-1);trigger_latch[side]=e.caxis.value>8000;}
            }
            if(route_move) {
                route=(route+1+route_move+28)%28-1;
                if(route>=0){smb_select_named(cells,stage_starts[route].player,&pick[0],&sheik[0]);costume=0;save_mode=0;}
            }
            if(move){if(slot==0)costume=0;pick[slot]=(pick[slot]+move)%32;sheik[slot]=0;}
        }
        if(!runtime&&record!=prior_record)smb_record_set_wanted(record);
        if(!automated&&pad&&SDL_GetTicks()-last_stick>180) {
            int x=SDL_GameControllerGetAxis(pad,SDL_CONTROLLER_AXIS_LEFTX),y=SDL_GameControllerGetAxis(pad,SDL_CONTROLLER_AXIS_LEFTY);
            if(abs(x)>16000||abs(y)>16000){if(slot==0)costume=0;pick[slot]=(pick[slot]+(abs(x)>abs(y)?(x>0?1:31):(y>0?11:21)))%32;sheik[slot]=0;last_stick=SDL_GetTicks();}
        }
        glViewport(0,0,w,h);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        glViewport((int)(ox*w/ww),(int)(oy*h/wh),(int)(960*scale*w/ww),(int)(720*scale*h/wh));
        glMatrixMode(GL_PROJECTION);glLoadIdentity();glOrtho(0,960,720,0,-1,1);glMatrixMode(GL_MODELVIEW);glLoadIdentity();
        smb_select_sprite(bg,0,0,960,720,0,0,960,720,960,720);
        SmbSelectCell* c=&cells[pick[slot]];
        smb_select_rect(c->x-3,c->y-3,c->w+6,c->h+6,1,.75f,.2f,.95f);
        smb_select_sprite(bg,c->x,c->y,c->w,c->h,c->x,c->y,c->w,c->h,960,720);
        smb_select_rect(30,375,905,265,.06f,.08f,.12f,1);
        for(int i=0;i<(course?1:4);i++) {
            float x=40+i*226;
            smb_select_rect(x-2,378,216,264,i==slot?1:.2f,i==slot?.75f:.25f,i==slot?.2f:.34f,1);
            smb_select_rect(x,380,212,260,.11f,.035f,.05f,1);
            smb_select_rect(x,380,212,25,.75f,.12f,.16f,1);
            /* Keep the native P1 / CPU header artwork, not a new text panel. */
            if(runtime==2)smb_select_sprite(atlas,x,380,212,25,0,3660,212,25,512,4096);
            else smb_select_sprite(bg,x,380,212,25,x,450,212,25,960,720);
            if(i<=cpus) {
                unsigned row=sheik[i]?32:pick[i],outfit=i?0:costume;
                smb_select_sprite(portraits,x+32,406,148,204,outfit*136,row*188,136,188,952,6204);
                smb_select_sprite(atlas,x+8,613,196,26,56,row*48,369,48,512,4096);
            } else {
                smb_select_sprite(atlas,x+70,490,80,40,0,1880,100,48,512,4096);
            }
        }
        /* Costume choices are Melee's own large portraits and palettes. */
        if(course)for(unsigned k=0;k<cells[pick[0]].costumes;k++) {
            float step=runtime?84:62,w=runtime?78:44,h=w*188/136,y=runtime?420:380,x=310+k*step;
            if(k==(unsigned)costume)smb_select_rect(x-3,y-3,w+6,h+6,1,.75f,.2f,1);
            smb_select_rect(x,y,w,h,.09f,.11f,.16f,1);
            smb_select_sprite(portraits,x,y,w,h,k*136,(sheik[0]?32:pick[0])*188,136,188,952,6204);
        }
        glDisable(GL_TEXTURE_2D);glColor4f(1,.85f,.4f,1);glBegin(GL_TRIANGLES);
        glVertex2f(44,510);glVertex2f(61,498);glVertex2f(61,522);
        glVertex2f(248,510);glVertex2f(231,498);glVertex2f(231,522);glEnd();
        if(course&&!runtime) {
            smb_select_sprite(bg,270,448,660,192,270,448,660,192,960,720);
            const float mode_x[]={290,466,662},mode_width[]={160,180,224};
            float x=mode_x[encounter_mode],width=mode_width[encounter_mode];
            smb_select_rect(x-3,489,width+6,38,1,.75f,.2f,1);
            smb_select_sprite(bg,x,492,width,32,x,492,width,32,960,720);
        }
        if(course&&!runtime) {
            smb_select_rect(290,546,610,44,.15f,.18f,.25f,1);
            smb_select_sprite(atlas,327,555,536,28,0,2688+(route+1)*32,512,30,512,4096);
            glDisable(GL_TEXTURE_2D);glColor4f(1,.8f,.35f,1);glBegin(GL_TRIANGLES);
            glVertex2f(300,568);glVertex2f(316,558);glVertex2f(316,578);
            glVertex2f(892,568);glVertex2f(876,558);glVertex2f(876,578);glEnd();
        }
        for(int i=0;i<3;i++) {
            float x=100+i*180;smb_select_rect(x-1,102,162,21,i==save_mode?.85f:.18f,i==save_mode?.65f:.22f,.25f,1);
            smb_select_rect(x,103,160,19,.08f,.1f,.16f,1);
            smb_select_sprite(atlas,x+6,103,148,19,0,!runtime&&route>=0&&i==0?3600:2000+i*32,240,30,512,4096);
        }
        smb_select_rect(699,102,222,21,record?.9f:.18f,record?.2f:.22f,.25f,1);smb_select_rect(700,103,220,19,.08f,.1f,.16f,1);
        smb_select_sprite(atlas,708,103,200,19,0,2096+record*32,300,30,512,4096);
        if(runtime) {
            smb_select_rect(30,100,900,23,.06f,.08f,.13f,1);
            smb_select_rect(30,642,900,78,.06f,.08f,.13f,1);
            smb_select_sprite(atlas,260,652,440,40,0,runtime==2?3690:2160,480,40,512,4096);
            smb_select_sprite(atlas,270,697,420,20,0,2240,512,30,512,4096);
        }
        if((runtime||getenv("SMB_PICKER_CAPTURE"))&&ticks==10&&getenv("SMB_MENU_CAPTURE")) {
            unsigned dims[2]={(unsigned)w,(unsigned)h};unsigned char* pixels=malloc((size_t)w*h*4);
            if(pixels){glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,pixels);FILE* capture=fopen(getenv("SMB_MENU_CAPTURE"),"wb");if(capture){fwrite(dims,4,2,capture);fwrite(pixels,4,(size_t)w*h,capture);fclose(capture);}free(pixels);}
        }
        SDL_GL_SwapWindow(window);ticks++;
        if(automated&&ticks==20)done=1;
        SDL_Delay(16);
    }
    if(runtime) {
        if(automated&&getenv("SMB_SWITCH_TEST_CANCEL")&&atoi(getenv("SMB_SWITCH_TEST_CANCEL")))cancel=1;
        if(!cancel) {
            SmbSelectCell* c=&cells[pick[0]];
            if(runtime==2){if(smb_host_spawn_enemy(sheik[0]?19:c->character,costume,9))fprintf(stderr,"SMB SPAWN FAILED\n");}
            else if(smb_host_change_character(sheik[0]?19:c->character,costume,sheik[0]?"sheik":c->name))fprintf(stderr,"SMB SWITCH FAILED\n");
        } else fprintf(stderr,"SMB SWITCH CANCEL unchanged=1\n");
        goto cleanup;
    }
    if(route>=0){char number[16];snprintf(number,sizeof number,"%d",route);setenv("SMB_START_STAGE",number,1);save_mode=0;}
    else unsetenv("SMB_START_STAGE");
    char selected_costume[16];snprintf(selected_costume,sizeof selected_costume,"%d",costume);setenv("SMB_PLAYER_COSTUME",selected_costume,1);
    smb_select_save_mode(save_mode);setenv("SMB_RECORD_AUTO",record?"1":"0",1);smb_record_set_wanted(record);
    fprintf(stderr,"SMB SELECT RECORD enabled=%d toggle=F9\n",record);
    if(course){setenv("SMB_DIFFICULTY",encounter_modes[encounter_mode],1);cpus=encounter_mode==2?0:3;fprintf(stderr,"SMB SELECT MODE name=%s\n",encounter_modes[encounter_mode]);}
    char text[64],lineup[64]={0},enemies[128]={0};
    for(int i=0;i<=cpus;i++) {
        SmbSelectCell* c=&cells[pick[i]];const char* name=sheik[i]?"sheik":c->name;unsigned character=sheik[i]?19:c->character;
        snprintf(text,sizeof text,"%s%u",i?",":"",character);strcat(lineup,text);
        if(i){if(i>1)strcat(enemies,",");strcat(enemies,name);}else setenv("MELEE3D_NATIVE_CHARACTER",name,1);
        fprintf(stderr,"SMB SELECT SLOT slot=%d character=%s external=%u\n",i,name,character);
    }
    snprintf(text,sizeof text,"%d",cpus);setenv("SMB_CPU_COUNT",text,1);setenv("SMB_CPU_CHARACTERS",enemies,1);
    snprintf(text,sizeof text,"%d",cpus+1);setenv("MELEE3D_NATIVE_LAB",text,1);setenv("MELEE3D_NATIVE_LINEUP",lineup,1);
    fprintf(stderr,"SMB SELECT CONFIRM character=%s external=%u cpus=%d difficulty=9 enemies=%s\n",getenv("MELEE3D_NATIVE_CHARACTER"),sheik[0]?19:cells[pick[0]].character,cpus,enemies);
cleanup:
    if(pad)SDL_GameControllerClose(pad);glDeleteTextures(1,&bg);glDeleteTextures(1,&atlas);glDeleteTextures(1,&portraits);
    glDisable(GL_BLEND);glDisable(GL_TEXTURE_2D);SDL_FlushEvents(SDL_KEYDOWN,SDL_KEYUP);
    if(runtime){smb_gl_menu_end();fprintf(stderr,"SMB SELECT CLOSE gl_error=%u\n",(unsigned)glGetError());}
}

static void smb_host_character_select(SDL_Window* window){smb_host_select(window,0);}
#endif
