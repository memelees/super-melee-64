/* Isolated geometry regressions for the same guard used by the native game.
 * clang -O2 test_terrain.c -o /tmp/test_terrain && /tmp/test_terrain */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
typedef struct {float x,y,z;} Vec3;
typedef struct {float x,y;} Vec2;
typedef struct {Vec3 p[3],n;unsigned surface,flags;} DlTriangle;
typedef struct {Vec3 lo,hi;int first,count,left,right;} DlNode;
typedef struct {
    Vec3 cur_pos;int motion_id;struct {Vec3 cur_pos;struct {Vec2 top,bottom,right,left;} ecb;} coll_data;
    struct {void* hsd_obj;}* gobj;
} Fighter;
static DlTriangle tris[4];static DlNode nodes[1];static Vec3 tri_lo[4],tri_hi[4];static unsigned order[4];
static int host_world=1,node_count=1;
#define DL_WALKABLE .64f
static int m3d_MapIsActive(Fighter* fp){return 0;}
static void HSD_JObjSetTranslate(void* obj,Vec3* p){}
#include "melee_terrain.inc"
static void wall(float angle,float slope) {
    Vec3 along={-sinf(angle),0,cosf(angle)},normal={cosf(angle),0,sinf(angle)};
    Vec3 a={-along.x*100,-20,-along.z*100},b={along.x*100,-20,along.z*100};
    Vec3 c={b.x-normal.x*slope*100,100,b.z-normal.z*slope*100},d={a.x-normal.x*slope*100,100,a.z-normal.z*slope*100};
    Vec3 n=smb_cross(smb_sub(b,a),smb_sub(d,a));n=smb_mul(n,1/sqrtf(smb_dot(n,n)));
    if(smb_dot(n,normal)<0)n=smb_mul(n,-1);
    tris[0]=(DlTriangle){{a,b,c},n,0,0};tris[1]=(DlTriangle){{a,c,d},n,0,0};
    nodes[0]=(DlNode){{-300,-30,-300},{300,110,300},0,2,-1,-1};
    for(int i=0;i<2;i++){order[i]=i;tri_lo[i]=tri_hi[i]=tris[i].p[0];for(int j=1;j<3;j++)for(int k=0;k<3;k++) {
        float* lo=&tri_lo[i].x;float* hi=&tri_hi[i].x;float* p=&tris[i].p[j].x;lo[k]=fminf(lo[k],p[k]);hi[k]=fmaxf(hi[k],p[k]);
    }}
}
static Fighter fighter(Vec3 p) {
    static struct {void* hsd_obj;} obj;
    Fighter fp={0};fp.cur_pos=p;fp.gobj=(void*)&obj;
    fp.coll_data.ecb.top.y=16;fp.coll_data.ecb.left.x=-4;fp.coll_data.ecb.right.x=4;return fp;
}
int main(void) {
    unsigned cases=0;
    for(int heading=0;heading<16;heading++)for(int speed=0;speed<3;speed++)for(int slope=0;slope<2;slope++) {
        float angle=heading*6.28318530718f/16;Vec3 n={cosf(angle),0,sinf(angle)};
        wall(angle,slope?.2f:0);
        Vec3 start=smb_mul(n,20);start.y=5;Fighter fp=fighter(start);
        float length=speed==0?1:speed==1?20:160;
        for(int frame=0;frame<(speed==0?80:4);frame++) {
            Vec3 from=fp.cur_pos;fp.cur_pos=smb_sub(fp.cur_pos,smb_mul(n,length));
            smb_native_constrain(&fp,&from);
            Vec3 body=fp.cur_pos;body.y+=2.6f;
            float clearance=smb_dot(smb_sub(body,tris[0].p[0]),tris[0].n);
            if(clearance<2.5f)fprintf(stderr,"FAIL heading=%d speed=%d slope=%d frame=%d pos=%.2f,%.2f,%.2f clearance=%.2f\n",heading,speed,slope,frame,fp.cur_pos.x,fp.cur_pos.y,fp.cur_pos.z,clearance);
            assert(clearance>2.5f);cases++;
        }
        /* Ignore unloaded surfaces and death planes. */
        tris[0].flags=tris[1].flags=2;Fighter pass=fighter(start);pass.cur_pos=smb_mul(n,-20);pass.cur_pos.y=5;
        assert(!smb_native_constrain(&pass,&start));assert(smb_dot(pass.cur_pos,n)<-19);cases++;
    }
    wall(0,0);
    Vec3 start={20,5,-50};Fighter fp=fighter(start);fp.cur_pos=(Vec3){-20,5,50};smb_native_constrain(&fp,&start);
    assert(fp.cur_pos.x>1&&fp.cur_pos.z>45);cases++;
    /* Jumping above the barrier is allowed. */
    start=(Vec3){20,120,0};fp=fighter(start);fp.cur_pos=(Vec3){-20,120,0};
    assert(!smb_native_constrain(&fp,&start));assert(fp.cur_pos.x<-19);cases++;
    printf("PASS: %u terrain checks (16 headings, slopes, high speed, sliding, unloaded geometry, overhead clearance)\n",cases);
}
