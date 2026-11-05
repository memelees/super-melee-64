/* Included inside the isolated Delfino adapter: live SM64 geometry. */
#include "bridge_protocol.h"
#include <time.h>
static unsigned host_revision;
static unsigned long long host_geometry_hash;
#include "melee_terrain.inc"
#include "melee_damage_events.inc"
/* Fighter attributes retain the disc's scalar byte order. Read these in the
 * GCC-compiled adapter rather than the Clang coroutine boundary. */
float smb_native_weight(Fighter* fp) {return fp->co_attrs.weight;}
void smb_native_attributes(Fighter* fp) {
    fprintf(stderr,"SMB ATTR kind=%d walk=%.6f dash_initial=%.6f run=%.6f jump_squat=%.6f jump_h=%.6f jump_v=%.6f hop_v=%.6f air_h=%.6f gravity=%.6f terminal=%.6f jumps=%d weight=%.3f walljump=%u\n",fp->kind,fp->co_attrs.walk_max_vel,fp->co_attrs.dash_initial_velocity,fp->co_attrs.dash_max_velocity,fp->co_attrs.jump_startup_time,fp->co_attrs.jump_h_max_velocity,fp->co_attrs.jump_v_initial_velocity,fp->co_attrs.hop_v_initial_velocity,fp->co_attrs.air_drift_max,fp->co_attrs.gravity,fp->co_attrs.terminal_velocity,fp->co_attrs.max_jumps,fp->co_attrs.weight,fp->can_walljump);
}
/* Some SM64 tread triangles overlap at their endpoints. Native 2D line
 * walkers can classify that seam as a ledge even after the supported-riser
 * bevel. Reattach a walking fighter to a nearby lower tread; never apply
 * this to a jump, attack, launch, wall contact or a genuine unsupported drop. */
int smb_native_step_down(Fighter* fp) {
    if(!host_world||!m3d_MapIsActive(fp)||fp->ground_or_air!=GA_Ground||fp->motion_id<14||fp->motion_id>23||
       (fp->coll_data.env_flags&Collide_WallMask)||fabsf(fp->input.lstick[0].x)<.3f)return 0;
    float direction=fp->input.lstick[0].x>0?1:-1;
    float x=fp->cur_pos.x+direction*1.5f;
    float foot=fp->cur_pos.y+fp->coll_data.ecb.bottom.y;
    Vec3 hit,normal;int line;u32 flags;
    if(!mpCheckAll(&hit,&line,&flags,&normal,-1,-1,x,foot+.05f,x,foot-6.f)||normal.y<DL_WALKABLE)return 0;
    float drop=foot-hit.y;if(drop<.02f||drop>6.f)return 0;
    fp->cur_pos.x=x;fp->cur_pos.y=hit.y-fp->coll_data.ecb.bottom.y;
    fp->coll_data.cur_pos=fp->coll_data.prev_pos=fp->coll_data.last_pos=fp->cur_pos;
    fp->coll_data.contact=hit;
    fp->coll_data.floor=(SurfaceData){line,flags,normal};
    fp->coll_data.env_flags=(fp->coll_data.env_flags&~(Collide_Edge|Collide_LeftEdge|Collide_RightEdge))|Collide_FloorMask;
    static unsigned logged;if(logged++<20)fprintf(stderr,"SMB STAIR STEP DOWN drop=%.3f line=%d\n",drop,line);
    return 1;
}
static void host_bounds(void) {
    map_min=(Vec3){1e20f,1e20f,1e20f};map_max=(Vec3){-1e20f,-1e20f,-1e20f};
    for(unsigned i=0;i<triangle_count;i++) {
        tri_lo[i]=tri_hi[i]=tris[i].p[0];order[i]=i;
        for(int j=0;j<3;j++) {
            Vec3 p=tris[i].p[j];
            tri_lo[i].x=fminf(tri_lo[i].x,p.x);tri_lo[i].y=fminf(tri_lo[i].y,p.y);tri_lo[i].z=fminf(tri_lo[i].z,p.z);
            tri_hi[i].x=fmaxf(tri_hi[i].x,p.x);tri_hi[i].y=fmaxf(tri_hi[i].y,p.y);tri_hi[i].z=fmaxf(tri_hi[i].z,p.z);
            map_min.x=fminf(map_min.x,p.x);map_min.y=fminf(map_min.y,p.y);map_min.z=fminf(map_min.z,p.z);
            map_max.x=fmaxf(map_max.x,p.x);map_max.y=fmaxf(map_max.y,p.y);map_max.z=fmaxf(map_max.z,p.z);
        }
    }
    node_count=0;build_node(0,triangle_count);
    step_count=0;
    struct timespec before,after;clock_gettime(CLOCK_MONOTONIC,&before);
    smooth_steps();
    clock_gettime(CLOCK_MONOTONIC,&after);
    static unsigned logged_revision=~0u,logged_count=~0u;
    if(host_revision!=logged_revision||step_count!=logged_count) {
        logged_revision=host_revision;logged_count=step_count;
        fprintf(stderr,"SMB WORLD STAIRS revision=%u risers=%u milliseconds=%.3f\n",host_revision,step_count,(after.tv_sec-before.tv_sec)*1000+(after.tv_nsec-before.tv_nsec)*.000001);
    }
    free(ledges);ledges=alloc(triangle_count*3*sizeof(*ledges));ledge_count=0;
    for(unsigned i=0;i<triangle_count;i++) {
        DlTriangle* t=&tris[i];if(t->n.y<DL_WALKABLE||(t->flags&7))continue;
        Vec3 center={(t->p[0].x+t->p[1].x+t->p[2].x)/3,(t->p[0].y+t->p[1].y+t->p[2].y)/3,(t->p[0].z+t->p[1].z+t->p[2].z)/3};
        for(int e=0;e<3;e++) {
            Vec3 a=t->p[e],b=t->p[(e+1)%3];
            float x=center.x-(a.x+b.x)/2,z=center.z-(a.z+b.z)/2,length=hypotf(x,z);
            if(length<.0001f||fabsf(a.y-b.y)>1)continue;
            ledges[ledge_count++]=(DlLedge){a,b,{x/length,0,z/length}};
        }
    }
    if(map_lines)for(unsigned i=0;i<triangle_count;i++) {
        int k=kind(&tris[i]);lines_for_triangle[i][0]=k*triangle_count+i;
        lines_for_triangle[i][1]=k==2?3*triangle_count+i:-1;
        original_lines[i].lo_flags=map_lines[i].lo_flags=((tris[i].flags&4)||tris[i].n.y<DL_WALKABLE)?0:LINE_FLAG_LEDGE;
    }
    memset(ray_cache,0,sizeof ray_cache);
    for(unsigned i=0;i<DL_SLICE_CACHE;i++)slice_cache[i].valid=0;
    current_slice=NULL;
}
int smb_native_stair_probe(unsigned ordinal,Vec3* point,Vec3* normal,float* rise,float* run) {
    unsigned valid=0;
    for(unsigned i=0;i<step_count;i++) {
        DlStep* step=&steps[i];Vec3 low={step->point.x+step->normal.x*(step->run+7),step->point.y+10,step->point.z+step->normal.z*(step->run+7)};
        Vec3 high={step->point.x-step->normal.x*3,step->point.y+step->rise+10,step->point.z-step->normal.z*3},a,b;
        if(!m3d_DelfinoFloor(&low,&a)||!m3d_DelfinoFloor(&high,&b)||fabsf(a.y-step->point.y)>.2f||fabsf(b.y-step->point.y-step->rise)>.2f)continue;
        if(valid++!=ordinal)continue;
        *point=step->point;*normal=step->normal;*rise=step->rise;*run=step->run;return 0;
    }
    return -1;
}
int smb_native_world(const SmbSurface* surfaces,unsigned count,unsigned revision) {
    if(!surfaces||!count||count>8000||sizeof(SmbSurface)!=sizeof(DlTriangle)||slicing)return -1;
    unsigned long long hash=1469598103934665603ull;
    const unsigned char* data=(const unsigned char*)surfaces;
    for(size_t i=0;i<count*sizeof(*surfaces);i++)hash=(hash^data[i])*1099511628211ull;
    if(host_world&&hash==host_geometry_hash&&revision==host_revision)return 0;

    int rebuild=!host_world||count!=triangle_count||revision!=host_revision;

    if(host_world&&revision!=host_revision){extern void smb_native_reset_world_items(void);smb_native_reset_world_items();}
    host_world=1;host_revision=revision;host_geometry_hash=hash;
    if(rebuild) {
        free(tris);free(nodes);free(order);free(tri_lo);free(tri_hi);
        tris=alloc(count*sizeof(*tris));nodes=alloc(count*2*sizeof(*nodes));order=alloc(count*sizeof(*order));
        tri_lo=alloc(count*sizeof(*tri_lo));tri_hi=alloc(count*sizeof(*tri_hi));triangle_count=count;
        free(map_lines);free(original_lines);free(map_points);free(saved);free(previous);free(saved_flags);free(triangle_for_line);free(lines_for_triangle);
        map_lines=original_lines=NULL;map_points=NULL;saved=previous=NULL;saved_flags=NULL;triangle_for_line=NULL;lines_for_triangle=NULL;
        for(unsigned i=0;i<DL_SLICE_CACHE;i++){free(slice_cache[i].segments);free(slice_cache[i].next_line);memset(&slice_cache[i],0,sizeof slice_cache[i]);}
    }
    memcpy(tris,surfaces,count*sizeof(*tris));host_bounds();active=true;
    if(rebuild) {
        /* Native scoped slices supply the islands; discard the original
         * static island lists and reuse the fixed native collision arrays. */
        mpIsland* groups[2]={mpIsland_80458E88.floors,mpIsland_80458E88.ceilings};
        for(int g=0;g<2;g++)for(mpIsland* p=groups[g];p;){mpIsland* next=p->next;HSD_Free(p);p=next;}
        mpIsland_8005A6F8();mpLibLoad(m3d_DelfinoCollision(NULL));
        fprintf(stderr,"SMB WORLD REBUILD revision=%u surfaces=%u lines=%d ledges=%u\n",revision,count,collision_data.line_count,ledge_count);
    }
    return rebuild?2:1;
}

#include "melee_environment.inc"
#include "melee_npcs.inc"
#include "melee_model_visibility.inc"

#include "melee_victory.inc"
#include "melee_player_select.inc"
#include "melee_powerups.inc"

#include "melee_remote.inc"
#include "melee_network_grab.inc"

#include "melee_campaign_remote.inc"

#include "melee_network_audio.inc"
