// ares WASM - WebGPU-powered N64 emulator frontend
#include <n64/n64.hpp>
#include <emscripten.h>
#include <emscripten/html5.h>
#include <webgpu/webgpu.h>

// ── Platform implementation ──────────────────────────────────

struct WebPlatform : ares::Platform {
  ares::Node::System root;
  shared_pointer<vfs::directory> systemPak;
  shared_pointer<vfs::directory> cartridgePak;

  u32 frameWidth = 0;
  u32 frameHeight = 0;
  bool frameReady = false;

  struct InputState {
    bool a, b, z, start;
    bool dpadUp, dpadDown, dpadLeft, dpadRight;
    bool l, r;
    bool cUp, cDown, cLeft, cRight;
    s16 axisX, axisY;
  } inputState = {};

  auto attach(ares::Node::Object) -> void override {}
  auto detach(ares::Node::Object) -> void override {}

  auto pak(ares::Node::Object node) -> shared_pointer<vfs::directory> override {
    if(node->name() == "Nintendo 64") return systemPak;
    if(node->name() == "Nintendo 64 Cartridge") return cartridgePak;
    return new vfs::directory;
  }

  auto event(ares::Event) -> void override {}
  auto log(string_view message) -> void override {
    EM_ASM({ console.log("ares: " + UTF8ToString($0)); }, (const char*)message);
  }
  auto status(string_view) -> void override {}

  auto video(ares::Node::Video::Screen screen, const u32* data, u32 pitch, u32 width, u32 height) -> void override {
    // We render directly from RDRAM, ignore Screen output
    frameReady = true;
  }

  auto audio(ares::Node::Audio::Stream stream) -> void override {
    while(stream->pending()) {
      f64 samples[2] = {};
      stream->read(samples);
    }
  }

  auto input(ares::Node::Input::Input node) -> void override {
    auto name = node->name();
    if(auto button = node->cast<ares::Node::Input::Button>()) {
      if(name == "A")      button->setValue(inputState.a);
      if(name == "B")      button->setValue(inputState.b);
      if(name == "Z")      button->setValue(inputState.z);
      if(name == "Start")  button->setValue(inputState.start);
      if(name == "Up")     button->setValue(inputState.dpadUp);
      if(name == "Down")   button->setValue(inputState.dpadDown);
      if(name == "Left")   button->setValue(inputState.dpadLeft);
      if(name == "Right")  button->setValue(inputState.dpadRight);
      if(name == "L")      button->setValue(inputState.l);
      if(name == "R")      button->setValue(inputState.r);
      if(name == "C-Up")   button->setValue(inputState.cUp);
      if(name == "C-Down") button->setValue(inputState.cDown);
      if(name == "C-Left") button->setValue(inputState.cLeft);
      if(name == "C-Right")button->setValue(inputState.cRight);
    }
    if(auto axis = node->cast<ares::Node::Input::Axis>()) {
      if(name == "X-Axis") axis->setValue(inputState.axisX);
      if(name == "Y-Axis") axis->setValue(inputState.axisY);
    }
  }
};

// ── Globals ──────────────────────────────────────────────────

static WebPlatform platform;
static bool emulatorRunning = false;
static u32 frameCount = 0;

// WebGPU state
static WGPUInstance gpuInstance = nullptr;
static WGPUAdapter gpuAdapter = nullptr;
static WGPUDevice gpuDevice = nullptr;
static WGPUQueue gpuQueue = nullptr;
static WGPUSurface gpuSurface = nullptr;
static WGPURenderPipeline gpuPipeline = nullptr;
static WGPUTexture gpuFrameTexture = nullptr;
static WGPUTextureView gpuFrameTextureView = nullptr;
static WGPUSampler gpuSampler = nullptr;
static WGPUBindGroup gpuBindGroup = nullptr;
static WGPUBindGroupLayout gpuBindGroupLayout = nullptr;

// RGBA pixel buffer for uploading to GPU
static u32* rgbaBuffer = nullptr;
static u32 rgbaBufferSize = 0;

// ── WebGPU Shaders ───────────────────────────────────────────

static const char* wgslShader = R"(
@group(0) @binding(0) var frameSampler: sampler;
@group(0) @binding(1) var frameTexture: texture_2d<f32>;

struct VertexOutput {
  @builtin(position) pos: vec4<f32>,
  @location(0) uv: vec2<f32>,
};

@vertex
fn vs_main(@builtin(vertex_index) idx: u32) -> VertexOutput {
  var positions = array<vec2<f32>, 4>(
    vec2<f32>(-1.0, -1.0),
    vec2<f32>( 1.0, -1.0),
    vec2<f32>(-1.0,  1.0),
    vec2<f32>( 1.0,  1.0),
  );
  var uvs = array<vec2<f32>, 4>(
    vec2<f32>(0.0, 1.0),
    vec2<f32>(1.0, 1.0),
    vec2<f32>(0.0, 0.0),
    vec2<f32>(1.0, 0.0),
  );
  var out: VertexOutput;
  out.pos = vec4<f32>(positions[idx], 0.0, 1.0);
  out.uv = uvs[idx];
  return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
  return textureSample(frameTexture, frameSampler, in.uv);
}
)";

// ── WebGPU Setup ─────────────────────────────────────────────

static void createFrameTexture(u32 width, u32 height) {
  if(gpuFrameTexture) wgpuTextureDestroy(gpuFrameTexture);
  if(gpuFrameTextureView) wgpuTextureViewRelease(gpuFrameTextureView);

  WGPUTextureDescriptor texDesc = {};
  texDesc.size = {width, height, 1};
  texDesc.format = WGPUTextureFormat_RGBA8Unorm;
  texDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
  texDesc.mipLevelCount = 1;
  texDesc.sampleCount = 1;
  texDesc.dimension = WGPUTextureDimension_2D;
  gpuFrameTexture = wgpuDeviceCreateTexture(gpuDevice, &texDesc);

  WGPUTextureViewDescriptor viewDesc = {};
  viewDesc.format = WGPUTextureFormat_RGBA8Unorm;
  viewDesc.dimension = WGPUTextureViewDimension_2D;
  viewDesc.mipLevelCount = 1;
  viewDesc.arrayLayerCount = 1;
  gpuFrameTextureView = wgpuTextureCreateView(gpuFrameTexture, &viewDesc);

  // Recreate bind group with new texture view
  if(gpuBindGroup) wgpuBindGroupRelease(gpuBindGroup);
  WGPUBindGroupEntry entries[2] = {};
  entries[0].binding = 0;
  entries[0].sampler = gpuSampler;
  entries[1].binding = 1;
  entries[1].textureView = gpuFrameTextureView;

  WGPUBindGroupDescriptor bgDesc = {};
  bgDesc.layout = gpuBindGroupLayout;
  bgDesc.entryCount = 2;
  bgDesc.entries = entries;
  gpuBindGroup = wgpuDeviceCreateBindGroup(gpuDevice, &bgDesc);
}

static void initPipeline() {
  // Shader module
  WGPUShaderSourceWGSL wgslDesc = {};
  wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
  wgslDesc.code = {wgslShader, WGPU_STRLEN};

  WGPUShaderModuleDescriptor smDesc = {};
  smDesc.nextInChain = &wgslDesc.chain;
  WGPUShaderModule sm = wgpuDeviceCreateShaderModule(gpuDevice, &smDesc);

  // Sampler
  WGPUSamplerDescriptor sampDesc = {};
  sampDesc.minFilter = WGPUFilterMode_Nearest;
  sampDesc.magFilter = WGPUFilterMode_Nearest;
  sampDesc.mipmapFilter = WGPUMipmapFilterMode_Nearest;
  sampDesc.addressModeU = WGPUAddressMode_ClampToEdge;
  sampDesc.addressModeV = WGPUAddressMode_ClampToEdge;
  sampDesc.addressModeW = WGPUAddressMode_ClampToEdge;
  sampDesc.maxAnisotropy = 1;
  gpuSampler = wgpuDeviceCreateSampler(gpuDevice, &sampDesc);

  // Bind group layout
  WGPUBindGroupLayoutEntry bglEntries[2] = {};
  bglEntries[0].binding = 0;
  bglEntries[0].visibility = WGPUShaderStage_Fragment;
  bglEntries[0].sampler.type = WGPUSamplerBindingType_Filtering;
  bglEntries[1].binding = 1;
  bglEntries[1].visibility = WGPUShaderStage_Fragment;
  bglEntries[1].texture.sampleType = WGPUTextureSampleType_Float;
  bglEntries[1].texture.viewDimension = WGPUTextureViewDimension_2D;

  WGPUBindGroupLayoutDescriptor bglDesc = {};
  bglDesc.entryCount = 2;
  bglDesc.entries = bglEntries;
  gpuBindGroupLayout = wgpuDeviceCreateBindGroupLayout(gpuDevice, &bglDesc);

  // Pipeline layout
  WGPUPipelineLayoutDescriptor plDesc = {};
  plDesc.bindGroupLayoutCount = 1;
  plDesc.bindGroupLayouts = &gpuBindGroupLayout;
  WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(gpuDevice, &plDesc);

  // Render pipeline
  WGPURenderPipelineDescriptor rpDesc = {};
  rpDesc.layout = pipelineLayout;
  rpDesc.vertex.module = sm;
  rpDesc.vertex.entryPoint = {"vs_main", WGPU_STRLEN};

  WGPUColorTargetState colorTarget = {};
  colorTarget.format = WGPUTextureFormat_BGRA8Unorm;
  colorTarget.writeMask = WGPUColorWriteMask_All;

  WGPUFragmentState fragState = {};
  fragState.module = sm;
  fragState.entryPoint = {"fs_main", WGPU_STRLEN};
  fragState.targetCount = 1;
  fragState.targets = &colorTarget;
  rpDesc.fragment = &fragState;

  rpDesc.primitive.topology = WGPUPrimitiveTopology_TriangleStrip;
  rpDesc.multisample.count = 1;
  rpDesc.multisample.mask = 0xFFFFFFFF;

  gpuPipeline = wgpuDeviceCreateRenderPipeline(gpuDevice, &rpDesc);

  // Create initial frame texture (320x240)
  createFrameTexture(320, 240);

  wgpuShaderModuleRelease(sm);
  wgpuPipelineLayoutRelease(pipelineLayout);
}

// ── Frame rendering ──────���───────────────────────────────────

static void renderFrame() {
  auto& vi = ares::Nintendo64::vi;
  auto& rdram = ares::Nintendo64::rdram;

  if(!vi.io.dramAddress || !vi.io.width || !vi.io.colorDepth) return;

  u32 w = 320, h = 240;
  u32 needed = w * h;

  if(needed > rgbaBufferSize) {
    delete[] rgbaBuffer;
    rgbaBuffer = new u32[needed];
    rgbaBufferSize = needed;
  }

  // Read N64 framebuffer from RDRAM and convert to RGBA8
  for(u32 y = 0; y < h; y++) {
    for(u32 x = 0; x < w; x++) {
      u32 pixel;
      if(vi.io.colorDepth == 3) {
        u32 addr = vi.io.dramAddress + (y * vi.io.width + x) * 4;
        u32 data = rdram.ram.read<ares::Nintendo64::Word>(addr);
        u8 r = data >> 24;
        u8 g = data >> 16;
        u8 b = data >> 8;
        pixel = r | (g << 8) | (b << 16) | (0xFFu << 24);
      } else {
        u32 addr = vi.io.dramAddress + (y * vi.io.width + x) * 2;
        u16 data = rdram.ram.read<ares::Nintendo64::Half>(addr);
        u8 r = (data >> 11 & 0x1f) << 3;
        u8 g = (data >>  6 & 0x1f) << 3;
        u8 b = (data >>  1 & 0x1f) << 3;
        pixel = r | (g << 8) | (b << 16) | (0xFFu << 24);
      }
      rgbaBuffer[y * w + x] = pixel;
    }
  }

  // Upload to GPU texture
  WGPUTexelCopyTextureInfo dst = {};
  dst.texture = gpuFrameTexture;
  WGPUTexelCopyBufferLayout layout = {};
  layout.bytesPerRow = w * 4;
  layout.rowsPerImage = h;
  WGPUExtent3D size = {w, h, 1};
  wgpuQueueWriteTexture(gpuQueue, &dst, rgbaBuffer, w * h * 4, &layout, &size);

  // Get current surface texture
  WGPUSurfaceTexture surfaceTex;
  wgpuSurfaceGetCurrentTexture(gpuSurface, &surfaceTex);
  if(surfaceTex.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
     surfaceTex.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) return;

  WGPUTextureViewDescriptor svDesc = {};
  svDesc.format = WGPUTextureFormat_BGRA8Unorm;
  svDesc.dimension = WGPUTextureViewDimension_2D;
  svDesc.mipLevelCount = 1;
  svDesc.arrayLayerCount = 1;
  WGPUTextureView surfaceView = wgpuTextureCreateView(surfaceTex.texture, &svDesc);

  // Render pass
  WGPURenderPassColorAttachment colorAtt = {};
  colorAtt.view = surfaceView;
  colorAtt.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
  colorAtt.loadOp = WGPULoadOp_Clear;
  colorAtt.storeOp = WGPUStoreOp_Store;
  colorAtt.clearValue = {0.0, 0.0, 0.0, 1.0};

  WGPURenderPassDescriptor rpDesc = {};
  rpDesc.colorAttachmentCount = 1;
  rpDesc.colorAttachments = &colorAtt;

  WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpuDevice, nullptr);
  WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &rpDesc);
  wgpuRenderPassEncoderSetPipeline(pass, gpuPipeline);
  wgpuRenderPassEncoderSetBindGroup(pass, 0, gpuBindGroup, 0, nullptr);
  wgpuRenderPassEncoderDraw(pass, 4, 1, 0, 0);
  wgpuRenderPassEncoderEnd(pass);

  WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, nullptr);
  wgpuQueueSubmit(gpuQueue, 1, &cmd);
  // wgpuSurfacePresent not needed in Emscripten — rAF handles presentation

  wgpuCommandBufferRelease(cmd);
  wgpuRenderPassEncoderRelease(pass);
  wgpuCommandEncoderRelease(encoder);
  wgpuTextureViewRelease(surfaceView);
  wgpuTextureRelease(surfaceTex.texture);
}

// ── ROM loading ───────────────���──────────────────────────────

static auto detectCIC(const u8* data, u32 size) -> string {
  if(size < 0x1000) return "CIC-NUS-6102";
  u32 crc = Hash::CRC32({&data[0x40], 0x9c0}).value();
  if(crc == 0x1deb51a9) return "CIC-NUS-6101";
  if(crc == 0xc08e5bd6) return "CIC-NUS-6102";
  if(crc == 0x03b8376a) return "CIC-NUS-6103";
  if(crc == 0xcf7f41dc) return "CIC-NUS-6105";
  if(crc == 0xd1059c6a) return "CIC-NUS-6106";
  return "CIC-NUS-6102";
}

static auto loadROMData(const u8* data, u32 size) -> bool {
  auto cic = detectCIC(data, size);

  string manifest;
  manifest.append("game\n");
  manifest.append("  name:   N64Game\n");
  manifest.append("  title:  N64Game\n");
  manifest.append("  region: NTSC\n");
  manifest.append("  board\n");
  manifest.append("    cic: ", cic, "\n");
  manifest.append("    memory\n");
  manifest.append("      type: ROM\n");
  manifest.append("      size: 0x", hex(size), "\n");
  manifest.append("      content: Program\n");

  platform.cartridgePak = new vfs::directory;
  platform.cartridgePak->setAttribute("title",  "N64Game");
  platform.cartridgePak->setAttribute("region", "NTSC");
  platform.cartridgePak->setAttribute("cic",    (string)cic);
  platform.cartridgePak->append("manifest.bml", manifest);
  platform.cartridgePak->append("program.rom",  array_view<u8>{data, size});

  platform.systemPak = new vfs::directory;
  {
    FILE* f = fopen("/pif.ntsc.rom", "rb");
    if(!f) f = fopen("pif.ntsc.rom", "rb");
    if(f) {
      fseek(f, 0, SEEK_END);
      long sz = ftell(f);
      fseek(f, 0, SEEK_SET);
      vector<u8> pifBuf;
      pifBuf.resize(sz);
      fread(pifBuf.data(), 1, sz, f);
      fclose(f);
      platform.systemPak->append("pif.ntsc.rom", pifBuf);
    }
  }

  ares::Nintendo64::option("Enable GPU acceleration", "false");
  ares::Nintendo64::option("Quality", "SD");

  if(!ares::Nintendo64::load(platform.root, "[Nintendo] Nintendo 64 (NTSC)")) return false;

  if(auto port = platform.root->find<ares::Node::Port>("Cartridge Slot")) {
    port->allocate();
    port->connect();
  }
  if(auto port = platform.root->find<ares::Node::Port>("Controller Port 1")) {
    port->allocate("Gamepad");
    port->connect();
  }

  platform.root->power();
  frameCount = 0;
  return true;
}

// ── Input ────────────────────────────────────────────────────

static EM_BOOL onKeyDown(int, const EmscriptenKeyboardEvent* e, void*) {
  auto& inp = platform.inputState;
  string key = e->key;
  if(key == "x" || key == "X") inp.a = true;
  if(key == "z" || key == "Z") inp.b = true;
  if(key == "c" || key == "C") inp.z = true;
  if(key == "Enter") inp.start = true;
  if(key == "ArrowUp") inp.dpadUp = true;
  if(key == "ArrowDown") inp.dpadDown = true;
  if(key == "ArrowLeft") inp.dpadLeft = true;
  if(key == "ArrowRight") inp.dpadRight = true;
  if(key == "a" || key == "A") inp.l = true;
  if(key == "s" || key == "S") inp.r = true;
  if(key == "i" || key == "I") inp.cUp = true;
  if(key == "k" || key == "K") inp.cDown = true;
  if(key == "j" || key == "J") inp.cLeft = true;
  if(key == "l" || key == "L") inp.cRight = true;
  if(key == "w" || key == "W") inp.axisY = 85;
  if(key == "d" || key == "D") inp.axisX = 85;
  if(key == "q" || key == "Q") inp.axisX = -85;
  if(key == "e" || key == "E") inp.axisY = -85;
  return EM_TRUE;
}
static EM_BOOL onKeyUp(int, const EmscriptenKeyboardEvent* e, void*) {
  auto& inp = platform.inputState;
  string key = e->key;
  if(key == "x" || key == "X") inp.a = false;
  if(key == "z" || key == "Z") inp.b = false;
  if(key == "c" || key == "C") inp.z = false;
  if(key == "Enter") inp.start = false;
  if(key == "ArrowUp") inp.dpadUp = false;
  if(key == "ArrowDown") inp.dpadDown = false;
  if(key == "ArrowLeft") inp.dpadLeft = false;
  if(key == "ArrowRight") inp.dpadRight = false;
  if(key == "a" || key == "A") inp.l = false;
  if(key == "s" || key == "S") inp.r = false;
  if(key == "i" || key == "I") inp.cUp = false;
  if(key == "k" || key == "K") inp.cDown = false;
  if(key == "j" || key == "J") inp.cLeft = false;
  if(key == "l" || key == "L") inp.cRight = false;
  if(key == "w" || key == "W") inp.axisY = 0;
  if(key == "d" || key == "D") inp.axisX = 0;
  if(key == "q" || key == "Q") inp.axisX = 0;
  if(key == "e" || key == "E") inp.axisY = 0;
  return EM_TRUE;
}

// ── Main loop ──────────────────���─────────────────────────────

static void mainLoop() {
  if(!emulatorRunning || !gpuDevice) return;

  auto& cpu = ares::Nintendo64::cpu;
  auto& vi  = ares::Nintendo64::vi;
  double deadline = emscripten_get_now() + 50.0;

  while(emscripten_get_now() < deadline) {
    for(u32 i = 0; i < 4096; i++) {
      cpu.instruction();
      cpu.synchronize();
      if(vi.refreshed) {
        vi.refreshed = false;
        frameCount++;
      }
    }
  }

  // Render once per browser frame with the latest N64 framebuffer
  renderFrame();

  static double lastLog = 0;
  double now = emscripten_get_now();
  if(now - lastLog > 5000.0) {
    // Sample center pixel directly from RDRAM
    u32 px = 0;
    if(vi.io.dramAddress && vi.io.width) {
      auto& rdram = ares::Nintendo64::rdram;
      u32 addr = vi.io.dramAddress + (120 * vi.io.width + 160) * 2;
      px = rdram.ram.read<ares::Nintendo64::Half>(addr);
    }
    EM_ASM({ if(Module.print) Module.print(
      "frames=" + $0 + " viBuf=0x" + ($1>>>0).toString(16) + " px=0x" + ($2>>>0).toString(16)
    ); }, (int)frameCount, (int)vi.io.dramAddress, (int)px);
    lastLog = now;
  }
}

// ── Exported API ────────────��────────────────────────────────

extern "C" {
  EMSCRIPTEN_KEEPALIVE void resetEmulator() {
    if(platform.root) {
      platform.root->power(true);
      frameCount = 0;
    }
  }

  EMSCRIPTEN_KEEPALIVE void loadROM(const u8* data, u32 size) {
    if(emulatorRunning) emulatorRunning = false;

    const u8* romData = data;
    u32 romSize = size;
    vector<u8> fileData;
    if(!data) {
      fileData = file::read("/rom.z64");
      if(!fileData) return;
      romData = fileData.data();
      romSize = fileData.size();
    }
    if(loadROMData(romData, romSize)) {
      emulatorRunning = true;
      EM_ASM({
        document.getElementById('status-bar').textContent = 'Running';
        document.getElementById('status-bar').className = 'ready';
      });
    }
  }
}

// ── WebGPU initialization chain ─────────��────────────────────

static void startMainLoop() {
  EM_ASM({ if(Module.print) Module.print("WebGPU ready, waiting for ROM..."); });
  EM_ASM({
    document.getElementById('status-bar').textContent = 'Load a ROM to start';
    document.getElementById('status-bar').className = 'loading';
  });
  emscripten_set_main_loop(mainLoop, 0, 0);
}

static void onDevice(WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView msg, void* ud1, void* ud2) {
  gpuDevice = device;
  gpuQueue = wgpuDeviceGetQueue(gpuDevice);

  WGPUSurfaceConfiguration config = {};
  config.device = gpuDevice;
  config.format = WGPUTextureFormat_BGRA8Unorm;
  config.usage = WGPUTextureUsage_RenderAttachment;
  config.width = 640;
  config.height = 480;
  config.presentMode = WGPUPresentMode_Fifo;
  config.alphaMode = WGPUCompositeAlphaMode_Opaque;
  wgpuSurfaceConfigure(gpuSurface, &config);

  initPipeline();
  startMainLoop();
}

static void onAdapter(WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView msg, void* ud1, void* ud2) {
  gpuAdapter = adapter;
  WGPUDeviceDescriptor devDesc = {};

  WGPURequestDeviceCallbackInfo devCB = {};
  devCB.mode = WGPUCallbackMode_AllowSpontaneous;
  devCB.callback = onDevice;
  wgpuAdapterRequestDevice(gpuAdapter, &devDesc, devCB);
}

int main(int argc, char** argv) {
  ares::platform = &platform;

  emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, nullptr, true, onKeyDown);
  emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, nullptr, true, onKeyUp);

  // Create WebGPU instance and surface
  gpuInstance = wgpuCreateInstance(nullptr);

  WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvasDesc = {};
  canvasDesc.chain.sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector;
  canvasDesc.selector = {"#canvas", WGPU_STRLEN};

  WGPUSurfaceDescriptor surfDesc = {};
  surfDesc.nextInChain = &canvasDesc.chain;
  gpuSurface = wgpuInstanceCreateSurface(gpuInstance, &surfDesc);

  WGPURequestAdapterOptions adapterOpts = {};
  adapterOpts.compatibleSurface = gpuSurface;
  adapterOpts.powerPreference = WGPUPowerPreference_HighPerformance;

  WGPURequestAdapterCallbackInfo adapterCB = {};
  adapterCB.mode = WGPUCallbackMode_AllowSpontaneous;
  adapterCB.callback = onAdapter;
  wgpuInstanceRequestAdapter(gpuInstance, &adapterOpts, adapterCB);

  EM_ASM({ if(Module.print) Module.print("Requesting WebGPU adapter..."); });

  return 0;
}
