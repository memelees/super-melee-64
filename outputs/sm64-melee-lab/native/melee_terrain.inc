/* A world-space capsule guard complements Melee's rotating 2D ECB slice.
 * It runs every native tick, including knockback, and uses the same live
 * triangles as floors/ledges. Small steps prevent thin-wall tunnelling. */
static Vec3 smb_sub(Vec3 a,Vec3 b){return (Vec3){a.x-b.x,a.y-b.y,a.z-b.z};}
static Vec3 smb_add(Vec3 a,Vec3 b){return (Vec3){a.x+b.x,a.y+b.y,a.z+b.z};}
static Vec3 smb_mul(Vec3 a,float s){return (Vec3){a.x*s,a.y*s,a.z*s};}
static float smb_dot(Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
static Vec3 smb_cross(Vec3 a,Vec3 b){return (Vec3){a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
static float smb_clamp(float a){return fmaxf(0,fminf(1,a));}
static Vec3 smb_edge_point(Vec3 p,Vec3 a,Vec3 b){Vec3 d=smb_sub(b,a);float n=smb_dot(d,d);return smb_add(a,smb_mul(d,n>1e-10f?smb_clamp(smb_dot(smb_sub(p,a),d)/n):0));}
static int smb_inside(Vec3 p,DlTriangle* t) {
    Vec3 a=smb_sub(t->p[1],t->p[0]),b=smb_sub(t->p[2],t->p[0]),c=smb_sub(p,t->p[0]);
    float aa=smb_dot(a,a),bb=smb_dot(b,b),ab=smb_dot(a,b),ac=smb_dot(a,c),bc=smb_dot(b,c),d=aa*bb-ab*ab;
    if(d<1e-10f)return 0;float u=(bb*ac-ab*bc)/d,v=(aa*bc-ab*ac)/d;
    return u>=-1e-5f&&v>=-1e-5f&&u+v<=1.00001f;
}
static Vec3 smb_triangle_point(Vec3 p,DlTriangle* t) {
    float distance=smb_dot(smb_sub(p,t->p[0]),t->n);
    Vec3 q=smb_sub(p,smb_mul(t->n,distance));if(smb_inside(q,t))return q;
    float best=1e30f;Vec3 result=t->p[0];
    for(int i=0;i<3;i++){q=smb_edge_point(p,t->p[i],t->p[(i+1)%3]);Vec3 d=smb_sub(p,q);float n=smb_dot(d,d);if(n<best){best=n;result=q;}}
    return result;
}
static void smb_segments(Vec3 a,Vec3 b,Vec3 c,Vec3 d,Vec3* p,Vec3* q) {
    Vec3 u=smb_sub(b,a),v=smb_sub(d,c),w=smb_sub(a,c);
    float aa=smb_dot(u,u),bb=smb_dot(u,v),cc=smb_dot(v,v),dd=smb_dot(u,w),ee=smb_dot(v,w);
    float den=aa*cc-bb*bb,s=den>1e-10f?smb_clamp((bb*ee-cc*dd)/den):0;
    float t=cc>1e-10f?(bb*s+ee)/cc:0;
    if(t<0){t=0;s=aa>1e-10f?smb_clamp(-dd/aa):0;}
    else if(t>1){t=1;s=aa>1e-10f?smb_clamp((bb-dd)/aa):0;}
    *p=smb_add(a,smb_mul(u,s));*q=smb_add(c,smb_mul(v,t));
}
static float smb_capsule_triangle(Vec3 a,Vec3 b,DlTriangle* tri,Vec3* axis,Vec3* face) {
    float da=smb_dot(smb_sub(a,tri->p[0]),tri->n),db=smb_dot(smb_sub(b,tri->p[0]),tri->n);
    if(da*db<=0&&fabsf(da-db)>1e-8f) {
        Vec3 p=smb_add(a,smb_mul(smb_sub(b,a),da/(da-db)));
        if(smb_inside(p,tri)){*axis=*face=p;return 0;}
    }
    *axis=a;*face=smb_triangle_point(a,tri);Vec3 delta=smb_sub(*axis,*face);float best=smb_dot(delta,delta);
    Vec3 p=b,q=smb_triangle_point(b,tri);delta=smb_sub(p,q);float n=smb_dot(delta,delta);
    if(n<best){best=n;*axis=p;*face=q;}
    for(int i=0;i<3;i++){smb_segments(a,b,tri->p[i],tri->p[(i+1)%3],&p,&q);delta=smb_sub(p,q);n=smb_dot(delta,delta);if(n<best){best=n;*axis=p;*face=q;}}
    return best;
}
static int smb_guard_node(int id,Vec3* pos,Vec3 previous,float radius,float height,Vec3* blocked) {
    DlNode* node=&nodes[id];
    if(pos->x+radius<node->lo.x||pos->x-radius>node->hi.x||pos->z+radius<node->lo.z||pos->z-radius>node->hi.z||pos->y>node->hi.y||pos->y+height<node->lo.y)return 0;
    if(!node->count)return smb_guard_node(node->left,pos,previous,radius,height,blocked)+smb_guard_node(node->right,pos,previous,radius,height,blocked);
    int contacts=0;
    for(int j=node->first;j<node->first+node->count;j++) {
        unsigned i=order[j];DlTriangle* t=&tris[i];
        /* Walkable floors and ceilings remain native ECB responsibilities. */
        if(t->flags&3||t->n.y>=DL_WALKABLE||t->n.y<-.01f)continue;
        /* Upward-facing steep terrain supports downhill travel. Treating
         * its full face as a wall pushes a descending capsule through ramps.
         * Uphill motion still meets the steep-terrain barrier. */
        if(t->n.y>.01f&&(pos->x-previous.x)*t->n.x+(pos->z-previous.z)*t->n.z>=-.0001f)continue;
        if(pos->x+radius<tri_lo[i].x||pos->x-radius>tri_hi[i].x||pos->z+radius<tri_lo[i].z||pos->z-radius>tri_hi[i].z||pos->y>tri_hi[i].y||pos->y+height<tri_lo[i].y)continue;
        Vec3 a=*pos,b=*pos;a.y+=radius;b.y+=fmaxf(radius,height-radius);Vec3 axis,face;
        float squared=smb_capsule_triangle(a,b,t,&axis,&face);
        if(squared>=(radius-.001f)*(radius-.001f))continue;
        Vec3 normal=smb_sub(axis,face);float distance=sqrtf(squared);
        /* Steep terrain blocks sideways travel; don't lift the fighter up a
         * barrier or pull a hanging fighter onto its upper face. */
        normal.y=0;float length=hypotf(normal.x,normal.z);
        if(length<1e-5f){normal=t->n;normal.y=0;length=hypotf(normal.x,normal.z);if(smb_dot(smb_sub(previous,t->p[0]),normal)<0)normal=smb_mul(normal,-1);}
        if(length<1e-5f)continue;normal=smb_mul(normal,1/length);
        float horizontal_fraction=fmaxf(.2f,length/fmaxf(distance,1e-5f));
        float push=(radius-distance+.002f)/horizontal_fraction;
        pos->x+=normal.x*push;pos->z+=normal.z*push;*blocked=smb_add(*blocked,normal);contacts++;
    }
    return contacts;
}
int smb_native_constrain(Fighter* fp,const Vec3* from) {
    if(!host_world||!node_count||m3d_MapIsActive(fp))return 0;
    /* Native ledge attachment deliberately puts the ECB against the wall. */
    if((fp->motion_id>=252&&fp->motion_id<=260)||fp->motion_id==262)return 0;
    float height=fmaxf(8,fminf(30,fp->coll_data.ecb.top.y));
    float radius=fmaxf(1.5f,fminf(4,fmaxf(fabsf(fp->coll_data.ecb.left.x),fabsf(fp->coll_data.ecb.right.x))*.65f));
    radius=fminf(radius,height*.4f);
    Vec3 target=fp->cur_pos,delta=smb_sub(target,*from),position=*from,blocked={0};
    float distance=sqrtf(smb_dot(delta,delta));
    int steps=(int)ceilf(distance/(radius*.4f));if(steps<1)steps=1;if(steps>512)steps=512;
    Vec3 step=smb_mul(delta,1.f/steps);int contacts=0;
    for(int s=0;s<steps;s++) {
        Vec3 previous=position;position=smb_add(position,step);
        for(int iteration=0;iteration<4;iteration++){int n=smb_guard_node(0,&position,previous,radius,height,&blocked);contacts+=n;if(!n)break;}
    }
    if(contacts) {
        Vec3 correction=smb_sub(position,target);fp->cur_pos=position;
        fp->coll_data.cur_pos=smb_add(fp->coll_data.cur_pos,correction);
        HSD_JObjSetTranslate(fp->gobj->hsd_obj,&position);
        static unsigned logs;if(logs++<30)fprintf(stderr,"SMB TERRAIN GUARD contacts=%d correction=%.4f,%.4f,%.4f radius=%.2f height=%.2f\n",contacts,correction.x,correction.y,correction.z,radius,height);
    }
    return contacts;
}
