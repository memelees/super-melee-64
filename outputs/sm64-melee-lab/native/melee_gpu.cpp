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
using namespace aurora;
namespace {
wgpu::Texture color,depth,transmittance;
wgpu::Buffer readback;
unsigned width,height,pitch,colorPitch;
std::mutex mutex;
std::condition_variable condition;
bool mapped,failed;
void allocate(unsigned w,unsigned h) {
 if(width==w&&height==h)return;
 gfx::gpu_synchronize();width=w;height=h;pitch=(w*4+255)&~255u;colorPitch=(w*8+255)&~255u;
 wgpu::TextureDescriptor desc{};desc.label="SM64 native Melee color";desc.size={w,h,1};
 desc.format=wgpu::TextureFormat::RGBA16Float;
 desc.usage=wgpu::TextureUsage::RenderAttachment|wgpu::TextureUsage::CopySrc;
 color=webgpu::g_device.CreateTexture(&desc);
 desc.label="SM64 Melee transmittance";desc.format=wgpu::TextureFormat::RGBA8Unorm;
 transmittance=webgpu::g_device.CreateTexture(&desc);
 desc.label="SM64 native Melee depth";desc.format=wgpu::TextureFormat::Depth32Float;
 depth=webgpu::g_device.CreateTexture(&desc);
 wgpu::BufferDescriptor buffer{};buffer.label="SM64 Melee frame transfer";
 buffer.size=uint64_t(colorPitch+pitch*2)*h;buffer.usage=wgpu::BufferUsage::CopyDst|wgpu::BufferUsage::MapRead;
 readback=webgpu::g_device.CreateBuffer(&buffer);
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
extern "C" int smb_gpu_end(float* rgba,float* depths,unsigned char* transmission) {
 gx::fifo::drain();gx::fifo::end_frame();gx::texture::end_frame();
 static bool once=true;if(once){once=false;
  const float* p=reinterpret_cast<const float*>(&gx::g_gxState.pnMtx[0].pos);
  fprintf(stderr,"SMB GPU MATRIX %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f\n",p[0],p[1],p[2],p[3],p[4],p[5],p[6],p[7],p[8],p[9],p[10],p[11]);
 }
 static bool warmed=false;if(!warmed){aurora_wait_pipelines(5000);warmed=true;}
 gfx::end_color_pass();gfx::finish();
 {std::lock_guard lock(mutex);mapped=false;failed=false;}
 gfx::end_frame([](wgpu::CommandEncoder& encoder,std::vector<gfx::AfterSubmitCallback> callbacks) {
  wgpu::TexelCopyTextureInfo src{};src.texture=color;
  wgpu::TexelCopyBufferInfo dst{};dst.buffer=readback;dst.layout.bytesPerRow=colorPitch;dst.layout.rowsPerImage=height;
  wgpu::Extent3D size{width,height,1};encoder.CopyTextureToBuffer(&src,&dst,&size);
  src.texture=depth;src.aspect=wgpu::TextureAspect::DepthOnly;dst.layout.bytesPerRow=pitch;dst.layout.offset=uint64_t(colorPitch)*height;
  encoder.CopyTextureToBuffer(&src,&dst,&size);
  src.texture=transmittance;src.aspect=wgpu::TextureAspect::All;dst.layout.offset=uint64_t(colorPitch+pitch)*height;
  encoder.CopyTextureToBuffer(&src,&dst,&size);
  auto command=encoder.Finish();webgpu::g_queue.Submit(1,&command);
  for(auto& callback:callbacks)callback();
  gfx::after_submit();
  readback.MapAsync(wgpu::MapMode::Read,0,uint64_t(colorPitch+pitch*2)*height,wgpu::CallbackMode::AllowSpontaneous,
   [](wgpu::MapAsyncStatus status,wgpu::StringView) {
    {std::lock_guard lock(mutex);failed=status!=wgpu::MapAsyncStatus::Success;mapped=true;}
    condition.notify_one();
   });
 });
 {std::unique_lock lock(mutex);
  if(!condition.wait_for(lock,std::chrono::seconds(20),[]{return mapped;}))return -2;
  if(failed)return -3;
 }
 auto* data=static_cast<const unsigned char*>(readback.GetConstMappedRange());
 if(!data)return -4;
 for(unsigned y=0;y<height;y++) {
  const auto* halfs=reinterpret_cast<const _Float16*>(data+size_t(y)*colorPitch);
  for(unsigned x=0;x<width*4;x++)rgba[size_t(y)*width*4+x]=float(halfs[x]);
  std::memcpy(transmission+size_t(y)*width*4,data+size_t(colorPitch+pitch)*height+size_t(y)*pitch,width*4);
  for(unsigned x=0;x<width;x++) {
   unsigned j=(y*width+x)*4;
   rgba[j+3]=1.f-(transmission[j]+transmission[j+1]+transmission[j+2])/(3.f*255.f);
  }
  std::memcpy(depths+size_t(y)*width,data+size_t(colorPitch)*height+size_t(y)*pitch,width*4);
 }
 readback.Unmap();return 0;
}
