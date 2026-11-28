/* Private native GX/HSD renderer into color + depth, with no donor world. */
#include "bridge_protocol.h"
#include "gfx/frame.hpp"
#include "gfx/recording.hpp"
#include "gx/fifo.hpp"
#include "gx/gx.hpp"
#include "gx/texture.hpp"
#include "webgpu/gpu.hpp"
#include <aurora/gfx.h>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <thread>
using namespace aurora;
namespace {
wgpu::Texture color,depth,transmittance;
unsigned width,height,pitch,colorPitch;
std::mutex mutex;
std::condition_variable condition;
const bool timing=getenv("SMB_GPU_TIMING")!=nullptr;
/* One readback lane per layer. The world lane can run one frame behind the
 * host (SMB_GPU_LAG, the stylised "held frame" look); the HUD lane always
 * does, so its slow overlay pass never sits on the frame critical path. */
struct Slot {
 const char* name;
 wgpu::Buffer buffer;
 bool pending;
 bool mapped;
 bool failed;
 bool hasDepth;
 float* outColor;
 float* outDepth;
 unsigned char* outTrans;
};
Slot worldSlot{"world",{},false,true,false,false,nullptr,nullptr,nullptr};
Slot hudSlot{"hud",{},false,true,false,false,nullptr,nullptr,nullptr};
bool lagMode=false;
uint64_t worldBytes=0,hudBytes=0;
float halfLut[65536];
const bool halfLutReady=[]{
 for(unsigned i=0;i<65536;i++) {
  unsigned s=uint32_t(i&0x8000u)<<16;
  unsigned e=(i>>10)&0x1fu,m=i&0x3ffu,out;
  if(e==0) {
   if(m==0)out=s;
   else {e=113u;while(!(m&0x400u)){m<<=1;--e;}m&=0x3ffu;out=s|(e<<23)|(m<<13);}
  } else if(e==31)out=s|0x7f800000u|(m<<13);
  else out=s|((e+112u)<<23)|(m<<13);
  std::memcpy(&halfLut[i],&out,4);
 }
 return true;
}();
void allocate(unsigned w,unsigned h) {
 if(width==w&&height==h)return;
 gfx::gpu_synchronize();
 width=w;height=h;pitch=(w*4+255)&~255u;colorPitch=(w*8+255)&~255u;
 worldSlot.pending=false;hudSlot.pending=false;
 wgpu::TextureDescriptor desc{};desc.label="SM64 native Melee color";desc.size={w,h,1};
 desc.format=wgpu::TextureFormat::RGBA16Float;
 desc.usage=wgpu::TextureUsage::RenderAttachment|wgpu::TextureUsage::CopySrc;
 color=webgpu::g_device.CreateTexture(&desc);
 desc.label="SM64 Melee transmittance";desc.format=wgpu::TextureFormat::RGBA8Unorm;
 transmittance=webgpu::g_device.CreateTexture(&desc);
 desc.label="SM64 native Melee depth";desc.format=wgpu::TextureFormat::Depth32Float;
 depth=webgpu::g_device.CreateTexture(&desc);
 const uint64_t wantWorld=uint64_t(colorPitch+pitch*2)*h;
 const uint64_t wantHud=uint64_t(colorPitch+pitch)*h;
 const auto make=[&](Slot& slot,uint64_t bytes,uint64_t& current,const char* label){
  if(current==bytes&&slot.buffer)return;
  wgpu::BufferDescriptor buffer{};buffer.label=label;
  buffer.size=bytes;buffer.usage=wgpu::BufferUsage::CopyDst|wgpu::BufferUsage::MapRead;
  slot.buffer=webgpu::g_device.CreateBuffer(&buffer);current=bytes;
 };
 make(worldSlot,wantWorld,worldBytes,"SM64 Melee world transfer");
 make(hudSlot,wantHud,hudBytes,"SM64 Melee hud transfer");
}
/* Waits for the pending map of one lane. */
int wait_mapped(Slot& slot) {
 unsigned iterations=0;
 std::chrono::steady_clock::duration peAcc{};
 {std::unique_lock lock(mutex);
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
  while(!slot.mapped) {
   lock.unlock();const auto pe0=std::chrono::steady_clock::now();webgpu::g_instance.ProcessEvents();peAcc+=std::chrono::steady_clock::now()-pe0;++iterations;std::this_thread::yield();lock.lock();
   if(slot.mapped)break;
   if(std::chrono::steady_clock::now()>=deadline)return -2;
  }
  if(slot.failed)return -3;
 }
 if(timing) {
  static unsigned k;static auto last=std::chrono::steady_clock::now();
  if(++k%60==0) {
   const auto now=std::chrono::steady_clock::now();
   fprintf(stderr,"SMB GPU WAIT lane=%s iters=%u pe_ms=%.2f wall_ms=%.2f\n",slot.name,iterations,
    std::chrono::duration<double,std::milli>(peAcc).count(),std::chrono::duration<double,std::milli>(now-last).count());
   last=now;
  }
 }
 return 0;
}
/* Copies the mapped lane into the host buffers. */
int collect(Slot& slot) {
 const auto began=std::chrono::steady_clock::now();
 auto* data=static_cast<const unsigned char*>(slot.buffer.GetConstMappedRange());
 if(!data)return -4;
 const auto* depth_data=data+size_t(colorPitch)*height;
 const auto* trans=data+size_t(colorPitch+(slot.hasDepth?pitch:0))*height;
 for(unsigned y=0;y<height;y++) {
  const auto* halfs=reinterpret_cast<const uint16_t*>(data+size_t(y)*colorPitch);
  float* row=slot.outColor+size_t(y)*width*4;
  for(unsigned i=0;i<width*4;i++)row[i]=halfLut[halfs[i]];
  const unsigned char* src=trans+size_t(y)*pitch;
  unsigned char* dstt=slot.outTrans+size_t(y)*width*4;
  std::memcpy(dstt,src,size_t(width)*4);
  for(unsigned x=0;x<width;x++) {
   unsigned j=x*4;
   row[j+3]=1.f-float(unsigned(src[j])+unsigned(src[j+1])+unsigned(src[j+2]))*(1.f/765.f);
  }
  if(slot.hasDepth)std::memcpy(slot.outDepth+size_t(y)*width,depth_data+size_t(y)*pitch,size_t(width)*4);
 }
 if(timing) {
  static unsigned n;
  if(++n%60==0)fprintf(stderr,"SMB GPU COPY lane=%s copy_ms=%.2f depth=%d\n",slot.name,
   std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-began).count(),slot.hasDepth?1:0);
 }
 slot.buffer.Unmap();
 return 0;
}
/* Submits this pass and starts its readback map. */
void submit(Slot& slot,bool withDepth) {
 {std::lock_guard lock(mutex);slot.mapped=false;slot.failed=false;}
 gfx::end_frame([&slot,withDepth](wgpu::CommandEncoder& encoder,std::vector<gfx::AfterSubmitCallback> callbacks) {
  wgpu::TexelCopyTextureInfo src{};src.texture=color;
  wgpu::TexelCopyBufferInfo dst{};dst.buffer=slot.buffer;dst.layout.bytesPerRow=colorPitch;dst.layout.rowsPerImage=height;
  wgpu::Extent3D size{width,height,1};encoder.CopyTextureToBuffer(&src,&dst,&size);
  if(withDepth) {
   src.texture=depth;src.aspect=wgpu::TextureAspect::DepthOnly;dst.layout.bytesPerRow=pitch;dst.layout.offset=uint64_t(colorPitch)*height;
   encoder.CopyTextureToBuffer(&src,&dst,&size);
  }
  src.texture=transmittance;src.aspect=wgpu::TextureAspect::All;dst.layout.bytesPerRow=pitch;dst.layout.offset=uint64_t(colorPitch+(withDepth?pitch:0))*height;
  encoder.CopyTextureToBuffer(&src,&dst,&size);
  auto command=encoder.Finish();webgpu::g_queue.Submit(1,&command);
  for(auto& callback:callbacks)callback();
  gfx::after_submit();
  const uint64_t bytes=withDepth?worldBytes:hudBytes;
  slot.buffer.MapAsync(wgpu::MapMode::Read,0,bytes,wgpu::CallbackMode::AllowSpontaneous,
   [&slot](wgpu::MapAsyncStatus status,wgpu::StringView) {
    {std::lock_guard lock(mutex);slot.failed=status!=wgpu::MapAsyncStatus::Success;slot.mapped=true;}
    condition.notify_all();
   });
 });
}
}
namespace aurora::gfx {void smb_transmittance_target(wgpu::TextureView);}
extern "C" int smb_gpu_begin(unsigned w,unsigned h) {
 allocate(w,h);
 if(!gfx::begin_frame())return -1;
 gx::fifo::begin_frame();
 gfx::ColorPassDescriptor pass{};pass.label="Native fighters and projectiles";
 pass.colorView=color.CreateView();pass.colorFormat=wgpu::TextureFormat::RGBA16Float;
 pass.depthStencilView=depth.CreateView();pass.depthStencilFormat=wgpu::TextureFormat::Depth32Float;
 pass.targetSize={w,h,1};pass.hasDepth=true;pass.depthLoadOp=wgpu::LoadOp::Clear;
 pass.depthStoreOp=wgpu::StoreOp::Store;pass.depthClearValue=0;
 gfx::begin_color_pass(pass);gfx::smb_transmittance_target(transmittance.CreateView());
 return 0;
}
/* depths==NULL marks the HUD overlay pass. Returns 0 when the host buffers
 * hold a fresh layer, 1 when this lane has nothing to show yet. */
extern "C" int smb_gpu_end(float* rgba,float* depths,unsigned char* transmission) {
 gx::fifo::drain();gx::fifo::end_frame();gx::texture::end_frame();
 static bool once=true;if(once){once=false;
  const float* p=reinterpret_cast<const float*>(&gx::g_gxState.pnMtx[0].pos);
  fprintf(stderr,"SMB GPU MATRIX %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f\n",p[0],p[1],p[2],p[3],p[4],p[5],p[6],p[7],p[8],p[9],p[10],p[11]);
 }
 static bool warmed=false;if(!warmed){warmed=true;aurora_wait_pipelines(5000);
  const char* v=getenv("SMB_GPU_LAG");lagMode=v&&v[0]=='1';
 }
 const bool withDepth=depths!=nullptr;
 Slot& slot=withDepth?worldSlot:hudSlot;
 const bool lag=withDepth?lagMode:true;
 slot.hasDepth=withDepth;slot.outColor=rgba;slot.outDepth=depths;slot.outTrans=transmission;
 bool collected=false;
 if(slot.pending) {
  if(const int rc=wait_mapped(slot))return rc;
  if(const int rc=collect(slot))return rc;
  slot.pending=false;collected=true;
 }
 gfx::end_color_pass();gfx::finish();
 submit(slot,withDepth);
 if(lag){slot.pending=true;return collected?0:1;}
 if(const int rc=wait_mapped(slot))return rc;
 return collect(slot);
}
