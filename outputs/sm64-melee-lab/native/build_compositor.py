"""Private Aurora patches: render C + T so final RGB = C + world * T.

An alpha-only target cannot carry Melee's additive/subtractive/multiply effects.
C uses signed half floats; T uses the native destination blend factor. Both
attachments are produced by the same draw, with the same depth/alpha tests.
Only this lab's copied libraries are rebuilt.
"""
from pathlib import Path
import os,sys,shutil
import json,subprocess
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]; OUT=Path(os.environ.get('SMB_RUNTIME_DIR',str(ROOT/'work/melee-embedded'))); ABI=OUT/'abi/aurora-lib'
def build():
    plan=json.loads((OUT/'cached-build-plan.json').read_text())
    def compile(relative,source,library):
        path=OUT/'compositor'/relative;path.parent.mkdir(parents=True,exist_ok=True);path.write_text(source)
        obj=path.with_name(path.name+('.obj' if plan.get('target')=='windows' else '.o'))
        subprocess.run(plan['gpu_compiler']+['-I',str((ABI/relative).parent),'-c',str(path),'-o',str(obj)],check=True)
        subprocess.run([plan.get('archiver','ar'),'r',str(OUT/'pinned-link-inputs/extern/aurora'/library),str(obj)],check=True)
    s=(ABI/'window.cpp').read_text()
    s=s.replace('void show_window() {','void show_window() {\n  if(SDL_getenv("SM64_MELEE_LIBRARY")){Log.info("SMB donor window hidden from creation");return;}')
    s=s.replace('bool is_paused() noexcept {','bool is_paused() noexcept {\n  if(SDL_getenv("SM64_MELEE_LIBRARY"))return false;')
    s=s.replace('if ((flags & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN)) != 0u) {',
        'if (!SDL_getenv("SM64_MELEE_LIBRARY") && (flags & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN)) != 0u) {')
    compile('window.cpp',s,'libaurora_core.a')
    s=(ABI/'gfx/recording.cpp').read_text()
    s=s.replace('void end_color_pass() {', '''// Called only for the embedded fighter pass. No public struct/ABI changes.
void smb_transmittance_target(wgpu::TextureView view) {
  auto& pass=current_render_passes()[g_recorder.currentRenderPass];
  auto& target=pass.colorAttachments[1];
  target.semantic=ColorAttachmentSemantic::Normal;
  target.format=wgpu::TextureFormat::RGBA8Unorm;
  target.size=pass.colorAttachments[0].size;target.view=std::move(view);
  target.clearValue={1.f,1.f,1.f,1.f};target.loadOp=wgpu::LoadOp::Clear;
  target.storeOp=wgpu::StoreOp::Store;target.clear=true;
  pass.colorAttachmentCount=2;
}
void end_color_pass() {''')
    compile('gfx/recording.cpp',s,'libaurora_gx.a')
    s=(ABI/'gx/shader.cpp').read_text()
    # Native normals are unused in this pass; location 1 carries the unmodified
    # TEV output, including its real texture/vertex alpha, for T's blend factor.
    begin=s.index('    if (wantNormal) {\n      if (useNormalTarget)')
    end=s.index('    if (wantZTex) {',begin)
    s=s[:begin]+'''    if (wantNormal) {
      fragReturn += "\\n    out_frag.normal = prev;";
    }
'''+s[end:]
    compile('gx/shader.cpp',s,'libaurora_gx.a')
    s=(ABI/'gx/pipeline.cpp').read_text()
    s=s.replace('if (config.alphaUpdate && config.dstAlpha != UINT32_MAX && config.dstAlpha != 0) {',
      'if (layout.colorAttachmentCount == 1 && config.alphaUpdate && config.dstAlpha != UINT32_MAX && config.dstAlpha != 0) {')
    compile('gx/pipeline.cpp',s,'libaurora_gx.a')
    s=(ABI/'gx/gx.cpp').read_text()
    needle='  colorTargets[gfx::SceneColorAttachmentIndex].blend = &blendState;'
    s=s.replace(needle,'''  // Preserve native RGB operations. T starts white, and each draw scales it
  // by exactly the factor with which that draw retains its background.
  wgpu::BlendState trans{};
  trans.color.operation=wgpu::BlendOperation::Add;
  trans.color.srcFactor=wgpu::BlendFactor::Zero;
  auto retain=wgpu::BlendFactor::Zero;
  if(config.blendMode==GX_BM_BLEND) {
    retain=to_blend_factor(config.blendFacDst,true);
    if(config.blendFacSrc==GX_BL_DSTCLR && config.blendFacDst==GX_BL_ZERO)
      retain=wgpu::BlendFactor::Src;
  } else if(config.blendMode==GX_BM_SUBTRACT ||
            (config.blendMode==GX_BM_LOGIC && config.blendOp==GX_LO_NOOP))
    retain=wgpu::BlendFactor::One;
  trans.color.dstFactor=retain;trans.alpha=trans.color;
  if(layout.colorAttachmentCount==2) {
    colorTargets[1].blend=&trans;
    colorTargets[1].writeMask=options.colorUpdate?wgpu::ColorWriteMask::All:wgpu::ColorWriteMask::None;
  }
'''+needle)
    compile('gx/gx.cpp',s,'libaurora_gx.a')
if __name__=='__main__':build()
