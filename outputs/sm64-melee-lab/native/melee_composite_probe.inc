/* Five real GX blend modes, exercised only by the compositor diagnostic. */
static void smb_draw_composite_probe(void) {
    Mtx id;PSMTXIdentity(id);GXLoadPosMtxImm(id,0);GXSetCurrentMtx(0);
    Mtx44 projection={{2.f/640,0,0,-1},{0,-2.f/480,0,1},{0,0,0,-.5f},{0,0,0,1}};
    GXSetProjection(projection,GX_ORTHOGRAPHIC);GXSetViewport(0,0,640,480,0,1);GXSetScissor(0,0,640,480);
    GXSetCullMode(GX_CULL_NONE);GXSetZMode(GX_ENABLE,GX_ALWAYS,GX_ENABLE);
    GXSetColorUpdate(GX_ENABLE);GXSetAlphaUpdate(GX_ENABLE);
    GXSetAlphaCompare(GX_ALWAYS,0,GX_AOP_AND,GX_ALWAYS,0);
    GXSetNumTexGens(0);GXSetNumTevStages(1);GXSetNumChans(1);
    GXSetChanCtrl(GX_COLOR0A0,GX_DISABLE,GX_SRC_REG,GX_SRC_VTX,0,GX_DF_NONE,GX_AF_NONE);
    GXSetTevOrder(GX_TEVSTAGE0,GX_TEXCOORD_NULL,GX_TEXMAP_NULL,GX_COLOR0A0);GXSetTevOp(GX_TEVSTAGE0,GX_PASSCLR);
    GXClearVtxDesc();GXSetVtxDesc(GX_VA_POS,GX_DIRECT);GXSetVtxDesc(GX_VA_CLR0,GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0,GX_VA_POS,GX_POS_XYZ,GX_F32,0);GXSetVtxAttrFmt(GX_VTXFMT0,GX_VA_CLR0,GX_CLR_RGBA,GX_RGBA8,0);
    const GXColor colors[]={{0,255,0,255},{0,0,255,128},{128,0,0,128},{128,128,128,255},{64,64,64,255}};
    for(unsigned i=0;i<5;i++) {
        if(i==0)GXSetBlendMode(GX_BM_NONE,GX_BL_ONE,GX_BL_ZERO,GX_LO_NOOP);
        if(i==1)GXSetBlendMode(GX_BM_BLEND,GX_BL_SRCALPHA,GX_BL_INVSRCALPHA,GX_LO_NOOP);
        if(i==2)GXSetBlendMode(GX_BM_BLEND,GX_BL_SRCALPHA,GX_BL_ONE,GX_LO_NOOP);
        if(i==3)GXSetBlendMode(GX_BM_BLEND,GX_BL_ZERO,GX_BL_SRCCLR,GX_LO_NOOP);
        if(i==4)GXSetBlendMode(GX_BM_SUBTRACT,GX_BL_ONE,GX_BL_ONE,GX_LO_NOOP);
        float x=16+80*i;GXColor c=colors[i];GXBegin(GX_QUADS,GX_VTXFMT0,4);
        GXPosition3f32(x,120,0);GXColor4u8(c.r,c.g,c.b,c.a);
        GXPosition3f32(x+64,120,0);GXColor4u8(c.r,c.g,c.b,c.a);
        GXPosition3f32(x+64,184,0);GXColor4u8(c.r,c.g,c.b,c.a);
        GXPosition3f32(x,184,0);GXColor4u8(c.r,c.g,c.b,c.a);GXEnd();
    }
}
static void smb_check_composite_probe(const SmbRender* request,const float* c,const unsigned char* t) {
    const float expected[5][4]={{0,1,0,0},{0,0,128.f/255,127.f/255},{128.f*128/(255*255),0,0,1},{0,0,0,128.f/255},{-64.f/255,-64.f/255,-64.f/255,1}};
    for(unsigned i=0;i<5;i++) {
        unsigned j=((request->height*152/480)*request->width+request->width*(48+80*i)/640)*4;
        float err=0;for(unsigned k=0;k<3;k++){err=fmaxf(err,fabsf(c[j+k]-expected[i][k]));err=fmaxf(err,fabsf(t[j+k]/255.f-expected[i][3]));}
        fprintf(stderr,"SMB COMPOSITE GPU mode=%u error=%.6f c=%.3f,%.3f,%.3f t=%.3f pass=%d\n",i,err,c[j],c[j+1],c[j+2],t[j]/255.f,err<.012f);
    }
}
