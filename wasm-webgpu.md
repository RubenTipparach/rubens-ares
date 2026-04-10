# ares N64 WASM + WebGPU Port

## Overview

This is a browser-based N64 emulator built from the [ares emulator](https://github.com/ares-emulator/ares) compiled to WebAssembly with a WebGPU display pipeline. The N64's Reality Display Processor (RDP) is implemented as a software renderer that writes to RDRAM, and the framebuffer is displayed via WebGPU.

## Architecture

```
[N64 Game ROM]
     |
[CPU Interpreter] ──> [RSP] ──> [RDP Software Renderer] ──> [RDRAM Framebuffer]
     |                                                              |
[PIF Boot / CIC]                                          [VI reads RDRAM]
     |                                                              |
[Controller Input] <── [Keyboard Events]                  [WebGPU Texture Upload]
                                                                    |
                                                          [WebGPU Render to Canvas]
```

### Components

| Component | Source | Description |
|-----------|--------|-------------|
| CPU | ares (original) | MIPS VR4300 interpreter, ~93.75 MHz |
| RSP | ares (original) | Reality Signal Processor, microcode execution |
| RDP | **Custom software renderer** | Triangle rasterization, texture mapping, color combiner |
| VI | ares (original) | Video Interface, reads RDRAM framebuffer |
| AI/PI/SI/MI/RI | ares (original) | All peripheral interfaces |
| PIF/CIC | ares (original) | Boot sequence, controller I/O, copy protection |
| Display | **WebGPU via Dawn** | Uploads RDRAM framebuffer to GPU texture, renders to canvas |
| Input | **Custom** | Keyboard-to-N64 gamepad mapping |
| Frontend | **Custom** | HTML/JS with ROM upload, reset, screenshot |

## What was built vs original ares

### Original ares code (unchanged, ~95% of codebase)
- Entire N64 emulation core (CPU, RSP, VI, AI, PI, SI, MI, RI, PIF, CIC, RDRAM, Cartridge)
- nall utility library
- libco cooperative threading
- mia media identification
- thirdparty libraries (sljit, libchdr, zlib)

### Custom code for WASM port

#### Software RDP (`ares/n64/rdp/render.cpp`)
The original ares RDP had all rendering functions as empty stubs — it relied entirely on the Vulkan-based parallel-RDP for GPU rendering. We implemented:

- **fillRectangle()** — solid color fills to RDRAM framebuffer
- **textureRectangle() / textureRectangleFlip()** — 2D textured quads (menus, HUD, text, rotated sprites)
- **renderTriangle()** — scanline triangle rasterizer with:
  - Edge walking (hi/md/lo edges with 16.16 fixed-point slopes)
  - Gouraud shading (per-vertex RGBA color interpolation)
  - Texture mapping (S/T/W coordinate interpolation, TMEM texel fetch)
  - Z-buffer depth testing and updates (via mask image)
  - Blender: `(P*A + M*B)/(A+B)` with selector-based inputs (transparency, fog, memory blend)
  - Scissor clipping
- **colorCombine()** — N64 color combiner implementing `(A - B) * C + D` per channel
  - Per-channel evaluation (R/G/B independently, not swapped from R)
  - Supports texture, shade, primitive, environment, key color inputs
  - Fill mode, copy mode, 1-cycle mode
- **TMEM management** — loadBlock(), loadTile(), loadTLUT()
- **Tile descriptors** — 8 tile slots with format, size, wrapping, mirroring, clamping
- **Texel fetching** — RGBA16, RGBA32, CI4, CI8, IA4, IA8, I4, I8 formats with palette lookup

#### WebGPU Display Pipeline (`web-ui/main.cpp`)
- WebGPU device/adapter/surface initialization via emdawnwebgpu
- WGSL shader for fullscreen textured quad
- Per-frame: reads RDRAM at VI's framebuffer address, converts 15/32-bit N64 pixels to RGBA8, uploads to GPU texture
- Renders via WebGPU render pass to canvas

#### Web Frontend (`web-ui/main.cpp`)
- `ares::Platform` implementation with video/audio/input callbacks
- ROM loading via emscripten virtual filesystem (JS writes file, C++ reads it)
- PIF ROM loading via C stdio (emscripten preloaded data)
- CIC detection from ROM header checksum
- VFS pak construction (cartridge manifest, system pak)
- Time-sliced main loop: 50ms CPU budget per requestAnimationFrame tick
- Cartridge and controller connection after system load, before power

#### Build System Patches (`wasm-patches/`)
Files modified from the wasm branch to compile for emscripten:

| File | Change |
|------|--------|
| `nall/GNUmakefile` | C++20, `-Wno-deprecated-literal-operator`, skip `-ldl`/`-pthread` for wasm |
| `nall/traits.hpp` | Guard `std::is_integral` specialization with `!defined(__wasm__)` |
| `ares/ares/ares.hpp` | `Video::Threaded = false` for wasm (prevents deadlock from background thread + mutex) |
| `ares/GNUmakefile` | Skip `-lX11 -lXext` for wasm |
| `ares/n64/accuracy.hpp` | Force interpreter mode on wasm (no JIT recompiler) |
| `ares/n64/cpu/cpu.hpp` | Stub Recompiler struct without `recompiler::generic` base on wasm |
| `ares/n64/cpu/cpu.cpp` | Guard `#include "recompiler.cpp"` with `!defined(__wasm__)` |
| `ares/n64/rsp/rsp.hpp` | Same recompiler stub as CPU |
| `ares/n64/rsp/rsp.cpp` | Same recompiler guard as CPU |
| `ares/n64/rdp/rdp.hpp` | Added `tiles[8]` array and `tmem[4096]` buffer |
| `ares/n64/rdp/render.cpp` | Full software RDP implementation (was empty stubs) |
| `thirdparty/sljit.h` | Guard with `!defined(__wasm__)` (JIT not supported) |
| `thirdparty/sljitAllocator.cpp` | Guard with `#if defined(SLJIT)` |
| `ruby/video/webgl2.cpp` | Minimal emscripten stub (not used with WebGPU path) |
| `ruby/ruby.cpp` | Added `DISPLAY_WEB` include block |
| `ruby/GNUmakefile` | Skip X11 libs for wasm |
| `libco/libco.c` | Added `__EMSCRIPTEN__` / `__wasm__` → wasm.c backend |
| `libco/wasm.c` | Emscripten fiber-based coroutine backend (not currently used — Asyncify removed) |
| `web-ui/GNUmakefile` | Removed hiro/desktop-ui, added web-ui target, WebGPU link flags, preload PIF ROMs |
| `web-ui/main.cpp` | Complete WASM frontend (see above) |

## Building

### Prerequisites
- Emscripten SDK (emsdk) with latest toolchain installed and activated
- GNU Make (comes with emsdk or via mingw/msys2)
- Node.js / npm (for the dev server)

### Build steps

```bash
# From repo root
./build-run.bat
```

Or manually:
```bash
# Activate emsdk
source emsdk/emsdk_env.sh  # or emsdk_env.bat on Windows

# Create wasm branch worktree
git fetch origin wasm
git worktree add build_wasm origin/wasm

# Apply patches
xcopy /s /y wasm-patches/* build_wasm/

# Copy PIF ROMs
cp ares/System/Nintendo\ 64/pif.ntsc.rom build_wasm/web-ui/
cp ares/System/Nintendo\ 64/pif.pal.rom build_wasm/web-ui/

# Build
cd build_wasm/web-ui
emmake make -j8 wasm32=true platform=linux

# Deploy
cp out/ares ../web/ares.js
cp out/ares.wasm ../web/
cp out/ares.data ../web/

# Serve (with COOP/COEP headers for SharedArrayBuffer/pthreads)
cd ../web
node server.js
```

### Output files
- `ares.js` — Emscripten JS glue (~250KB)
- `ares.wasm` — Compiled WASM binary (~3.5MB)
- `ares.data` — Preloaded PIF ROMs (~4KB)

## Browser Requirements
- Chrome 113+ or Edge 113+ (WebGPU support required)
- Firefox Nightly with `dom.webgpu.enabled` flag

## Keyboard Controls

| Key | N64 Button |
|-----|-----------|
| X | A |
| Z | B |
| C | Z |
| Enter | Start |
| Arrow keys | D-Pad |
| A / S | L / R |
| I / J / K / L | C-Up / C-Left / C-Down / C-Right |
| W / Q / D / E | Analog Up / Left / Right / Down |

Click the canvas to give it keyboard focus.

## Technical Deep Dive

### Problem: ares had no software RDP

The N64's Reality Display Processor handles all 2D/3D rendering. ares was designed exclusively around parallel-RDP, a Vulkan compute shader implementation. The software fallback was literally:

```cpp
// Every single rendering function was this:
auto RDP::shadedTextureTriangle() -> void {
}
auto RDP::fillRectangle() -> void {
}
auto RDP::textureRectangle() -> void {
}
// ... all empty
```

The only non-empty function was `syncFull()` which raises an interrupt. This meant zero pixels were ever written to the framebuffer by the RDP — games would boot (CPU runs, PIF handshake works) but never render anything beyond what the CPU writes directly via DMA (the N64 boot logo).

### Solution: Implement the RDP rendering pipeline

#### Fill Rectangle
The simplest RDP command — fills a rectangle with a solid color. The N64 stores two 16-bit pixels packed in the 32-bit fill color register:

```cpp
auto RDP::fillRectangle() -> void {
  // Coordinates are 10.2 fixed-point
  u32 xlo = fillRectangle_.x.hi >> 2;
  u32 ylo = fillRectangle_.y.hi >> 2;
  u32 xhi = fillRectangle_.x.lo >> 2;
  u32 yhi = fillRectangle_.y.lo >> 2;

  for(u32 y = ylo; y < yhi; y++) {
    for(u32 x = xlo; x < xhi; x++) {
      if(set.color.size == 2) {
        // 16-bit: fill color has two pixels packed
        u16 pixel = (x & 1) ? (set.fill.color & 0xffff) 
                             : (set.fill.color >> 16);
        rdram.ram.write<Half>(addr + (y * width + x) * 2, pixel);
      }
    }
  }
}
```

#### Triangle Rasterizer
The N64 RDP uses edge-walking with three edges (hi, md, lo) and 16.16 fixed-point coordinates. The edge slopes are pre-computed by the RSP microcode:

```cpp
// Edge coordinates in 16.16 fixed-point
s32 xh = ((s32)(s16)edge.x.hi.c.i << 16) | (u16)edge.x.hi.c.f;
s32 dxhdy = ((s32)(s16)edge.x.hi.s.i << 16) | (u16)edge.x.hi.s.f;

// Rasterize scanlines
for(s32 y = yh; y < yl; y++) {
  // Select which edges define left/right based on lmajor flag
  s32 leftX = edge.lmajor ? xh : (y < ym ? xm : xl);
  s32 rightX = edge.lmajor ? (y < ym ? xm : xl) : xh;
  
  // Shade color interpolation per pixel
  for(s32 x = x0; x < x1; x++) {
    shR = clamp(cr >> 16, 0, 255);  // 16.16 → 8-bit
    cr += drdx;  // step per X
  }
  
  // Step edges and attributes along Y
  xh += dxhdy;
  sr += drde;  // shade steps along edge
  ts += dsde;  // texture S steps along edge
}
```

#### Color Combiner
The N64's "pixel shader" — computes `(A - B) * C + D` per channel. The inputs are selected by 3-5 bit fields in the SetCombineMode command:

```cpp
auto RDP::colorCombine(u32 cycle, u8 texR, u8 texG, u8 texB, u8 texA,
                        u8 shR, u8 shG, u8 shB, u8 shA) -> u32 {
  // Input A (sub A) — 4-bit selector
  auto getSubA_RGB = [&](u32 sel) -> s32 {
    switch(sel) {
      case 1: return texR;       // texture color
      case 3: return primitive.red;   // primitive color
      case 4: return shR;        // shade (vertex) color
      case 5: return environment.red; // environment color
      case 6: return 255;        // 1.0
      // ...
    }
  };
  
  // C (multiply) — 5-bit selector, includes alpha sources
  auto getMul_RGB = [&](u32 sel) -> s32 {
    switch(sel) {
      case 8:  return texA;      // texture alpha
      case 10: return primitive.alpha;
      case 11: return shA;       // shade alpha
      // ...
    }
  };
  
  // (A - B) * C / 256 + D
  s32 outR = clamp(((aR - bR) * cR) / 256 + dR, 0, 255);
}
```

This enables effects like:
- `(Texture - 0) * Shade + 0` = texture modulated by vertex color (most 3D objects)
- `(Primitive - 0) * 1 + 0` = flat primitive color
- `(Texture - 0) * TexAlpha + Shade` = alpha-blended texture over shade

#### TMEM and Texel Fetching
The N64 has 4KB of on-chip texture memory. Games load textures from RDRAM into TMEM via LoadBlock/LoadTile commands, then reference them through 8 tile descriptors:

```cpp
auto RDP::loadBlock() -> void {
  auto& td = tiles[load_.block.index];
  u32 tmemAddr = (u32)td.address * 8;  // tile address is in 8-byte units
  u32 dramAddr = set.texture.dramAddress;
  // Copy bytes from RDRAM to TMEM
  for(u32 i = 0; i < bytes; i++) {
    tmem[(tmemAddr + i) & 0xfff] = rdram.ram.read<Byte>(dramAddr + i);
  }
}

auto RDP::fetchTexel(u32 tileIdx, s32 s, s32 t) -> u32 {
  auto& td = tiles[tileIdx];
  // Apply wrap/mirror/clamp
  if(td.s.mask) {
    u32 smask = (1 << td.s.mask) - 1;
    if(td.s.mirror && (s >> td.s.mask & 1)) 
      s = smask - (s & smask);  // mirror
    else s &= smask;            // wrap
  }
  // Read from TMEM based on format
  if(td.size == 2 && td.format == 0) {
    // RGBA16: RRRRR GGGGG BBBBB A
    u16 texel = tmem[offset] << 8 | tmem[offset + 1];
    u8 r = (texel >> 11 & 0x1f) << 3;
    // ...
  }
}
```

### Problem: Video::Threaded deadlocked on WASM

ares's `Screen` class creates a background thread for frame processing:

```cpp
// ares/ares/node/video/screen.cpp
Screen::Screen(...) {
  if constexpr(ares::Video::Threaded) {
    _thread = nall::thread::create({&Screen::main, this});
  }
}
auto Screen::frame() -> void {
  while(_frame) spinloop();  // Wait for thread — DEADLOCKS on WASM
  lock_guard<recursive_mutex> lock(_mutex);
  // ...
}
```

WASM is single-threaded — `thread::create` and mutex locks deadlock immediately. Fix:

```cpp
// ares/ares/ares.hpp
namespace Video {
  #if defined(__wasm__)
  static constexpr bool Threaded = false;  // Synchronous frame processing
  #else
  static constexpr bool Threaded = true;
  #endif
}
```

### Problem: 67MB palette allocation

The VI creates a color lookup palette with `(1 << 24) + (1 << 15)` = 16.8 million entries (67MB). This was used to convert N64 native pixel formats to ARGB8888. On WASM with limited memory, this was problematic.

Solution: bypass the Screen's palette entirely and read RDRAM directly:

```cpp
// In the main loop, after vi.refreshed:
for(u32 y = 0; y < h; y++) {
  for(u32 x = 0; x < w; x++) {
    u32 addr = vi.io.dramAddress + (y * vi.io.width + x) * 2;
    u16 data = rdram.ram.read<Half>(addr);
    // Convert 15-bit N64 color to RGBA8 directly
    u8 r = (data >> 11 & 0x1f) << 3;
    u8 g = (data >>  6 & 0x1f) << 3;
    u8 b = (data >>  1 & 0x1f) << 3;
    rgbaBuffer[y * w + x] = r | (g << 8) | (b << 16) | (0xFFu << 24);
  }
}
```

### Problem: PIF ROM not loading via nall::file

Emscripten's preloaded filesystem uses its own VFS. nall's `file::read()` goes through POSIX `open()/read()` which doesn't find preloaded files. Solution: use C stdio which emscripten properly intercepts:

```cpp
FILE* f = fopen("/pif.ntsc.rom", "rb");
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
```

### Problem: Cartridge not connecting during system load

ares's desktop-ui connects cartridges via the `attach()` callback during `Node::Port` creation. But when we did this during `ares::Nintendo64::load()`, the system wasn't fully initialized yet, causing the CIC chip to have an empty model string:

```
[PIF::main] invalid IPL2 checksum: :000000000000 != cpu:ea42e30a4e27
```

The `:` before `000000000000` shows an empty CIC model. Fix: connect after load, before power:

```cpp
// After load completes, find and connect ports manually:
if(auto port = platform.root->find<ares::Node::Port>("Cartridge Slot")) {
  port->allocate();
  port->connect();
}
if(auto port = platform.root->find<ares::Node::Port>("Controller Port 1")) {
  port->allocate("Gamepad");
  port->connect();
}
// Now the cartridge CIC is available for power():
platform.root->power();
```

### WebGPU Display Pipeline

The display uses Dawn (Google's WebGPU implementation) via emscripten's `emdawnwebgpu` port:

```cpp
// Create surface from HTML canvas
WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvasDesc = {};
canvasDesc.chain.sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector;
canvasDesc.selector = {"#canvas", WGPU_STRLEN};
gpuSurface = wgpuInstanceCreateSurface(gpuInstance, &surfDesc);

// Each frame: upload RDRAM pixels to GPU texture
wgpuQueueWriteTexture(gpuQueue, &dst, rgbaBuffer, w * h * 4, &layout, &size);

// Render fullscreen quad with WGSL shader
// The shader simply samples the N64 framebuffer texture:
@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
  return textureSample(frameTexture, frameSampler, in.uv);
}
```

### Time-Sliced Main Loop

The N64 CPU interpreter is too slow to complete a full frame in one requestAnimationFrame callback. Solution: run as many instructions as possible within a 50ms budget:

```cpp
static void mainLoop() {
  double deadline = emscripten_get_now() + 50.0;
  while(emscripten_get_now() < deadline) {
    for(u32 i = 0; i < 4096; i++) {
      cpu.instruction();
      cpu.synchronize();  // Runs VI, AI, RSP, RDP, PIF
      if(vi.refreshed) {
        vi.refreshed = false;
        frameCount++;
      }
    }
  }
  renderFrame();  // Upload latest RDRAM framebuffer to WebGPU
}
```

This gives ~200-300k instructions per browser frame, producing roughly 1 N64 frame per 2 browser frames (~15 emulated fps).

## angrylion-rdp-plus Integration (2026-04-09)

### Overview

The custom software RDP has been supplemented with [angrylion-rdp-plus](https://github.com/ata4/angrylion-rdp-plus), the gold-standard pixel-perfect N64 RDP software renderer. angrylion is enabled by default via `-DUSE_ANGRYLION` and completely replaces the custom renderer's command processing — ares's VI + WebGPU display pipeline remains unchanged.

### Architecture

```
[RSP microcode] ──> [DP registers: START/END/CURRENT]
                          |
                    [angrylion bridge]
                          |
                    [angrylion_process()]
                          |
            ┌─────────────┼─────────────┐
            │             │             │
       [Worker 0]    [Worker 1]    [Worker N]     ← pthreads via Web Workers
            │             │             │
            └─────────────┼─────────────┘
                          |
                    [RDRAM framebuffer]
                          |
                    [ares VI] ──> [WebGPU]
```

### How It Works

1. **Lazy init**: First call to `RDP::render()` initializes angrylion with pointers to ares's RDRAM (`rdram.ram.data`) and RSP DMEM (`rsp.dmem.data`)
2. **Register mapping**: ares's DP register values (`command.start/end/current/source`) are copied into a static register array that angrylion reads via `uint32_t**` pointers
3. **Command processing**: `n64video_process_list()` reads RDP commands from RDRAM (or DMEM for XBUS DMA), dispatches them through angrylion's pixel-perfect pipeline
4. **Parallel rendering**: angrylion splits scanline rasterization across multiple worker threads (pthreads → Web Workers with SharedArrayBuffer)
5. **Interrupt**: On `sync_full`, angrylion sets a flag; ares raises `MI::IRQ::DP` to signal frame completion
6. **Display**: ares's VI reads the RDRAM framebuffer as usual → WebGPU uploads to canvas (unchanged)

### Files

| File | Purpose |
|------|---------|
| `angrylion-rdp-plus/` | Submodule: ata4/angrylion-rdp-plus |
| `wasm-patches/angrylion/angrylion_bridge.h` | C bridge API header |
| `wasm-patches/angrylion/angrylion_bridge.c` | Maps ares DP registers → angrylion config, calls `n64video_process_list()` |
| `wasm-patches/angrylion/msg_stub.c` | Logging stub (routes to stderr/console) |
| `wasm-patches/web-ui/GNUmakefile` | Build rules: `-DUSE_ANGRYLION`, `-pthread`, angrylion compile targets |
| `web/server.js` | HTTP server with COOP/COEP headers for SharedArrayBuffer |

### Build Changes

- `-DUSE_ANGRYLION` added to global flags (controls `#if` in render.cpp)
- `-pthread` added globally (enables atomics/bulk-memory for all objects)
- `-sPTHREAD_POOL_SIZE=4` link flag (pre-spawns 4 Web Workers)
- `-sALLOW_BLOCKING_ON_MAIN_THREAD=1` (angrylion's fork-join blocks main thread while workers run)
- angrylion core compiled as unity build (`n64video.c` includes `rdp.c` and `vi.c`)
- Real `parallel.cpp` from angrylion used (std::thread/mutex/condition_variable)

### Server Requirements

SharedArrayBuffer requires COOP/COEP headers. The old `http-server` doesn't support these. Use `web/server.js` instead:

```bash
cd web && node server.js
# Serves on http://localhost:3000 with:
#   Cross-Origin-Opener-Policy: same-origin
#   Cross-Origin-Embedder-Policy: require-corp
```

### Switching Between Renderers

To revert to the custom software RDP, remove `-DUSE_ANGRYLION` and `-pthread` from the GNUmakefile flags, and do a clean rebuild. Both renderers coexist in `render.cpp` behind `#if defined(USE_ANGRYLION)`.

### What angrylion Provides (vs custom RDP)

| Feature | Custom RDP | angrylion |
|---------|-----------|-----------|
| Triangle rasterization | Basic scanline | Pixel-perfect, sub-pixel accurate |
| Color combiner | 1-cycle only | Full 1-cycle and 2-cycle |
| Texture filtering | Nearest only | Bilinear, nearest |
| Perspective correction | No | Yes (W division) |
| Anti-aliasing / coverage | No | Full coverage calculation |
| Color/alpha dithering | No | Yes |
| LOD / mipmapping | No | Yes |
| Noise generator | Returns 0 | LFSR |
| VI filtering | None (raw pixels) | Divot, gamma, AA, interpolation |

## RDP Software Renderer Changelog (2026-04-09)

Before screenshot: `screenshots/ares_2026-04-08_20-55-40.png` — garbled blue stripes, broken rendering.

All changes in `wasm-patches/ares/n64/rdp/render.cpp`. Reference: `parallel-rdp/` submodule (Themaister/parallel-rdp-standalone).

### Fixes Applied

| # | What | What Changed |
|---|------|-------------|
| 1 | **Color Combiner** | Replaced broken `swapToG/swapToB` value-comparison hack with proper per-channel array evaluation. Each selector (`getSubA_RGB`, `getSubB_RGB`, `getMul_RGB`, `getAdd_RGB`) now takes a `ch` parameter (0=R, 1=G, 2=B) indexing into `sh[ch]`, `tex[ch]`, `prim[ch]`, `env[ch]`, `keyCenter[ch]`, `keyScale[ch]` arrays. |
| 2 | **Texture Rectangle S/T** | Fixed fixed-point conversion: S/T coords now `(s16)val << 5` (s.10.5 to s.10.10), DsDx/DtDy left as s.5.10. Texel extraction uses `>> 10` (was `>> 5`). Removed DsDx bit contamination (`\| rectangle.s.f >> 11`). |
| 3 | **loadBlock/loadTile** | Byte count now uses `(4 << size)` bits-per-texel with `(count * bitsPerTexel + 7) / 8`. Texel count uses `SH - SL + 1`. Same fix applied to `loadTile`. |
| 4 | **textureRectangleFlip** | Now properly swaps S/T stepping axes: T steps along X (`dtdx`), S steps along Y (`dsdy`). Was just calling `textureRectangle()`. |
| 5 | **Triangle Texture Coords** | Changed `cs >> 16` to `cs >> 21` — accounts for 5 sub-texel fraction bits in s.10.5 packed inside s.15.16 container. |
| 6 | **Z-Buffer** (new feature) | Full per-pixel depth testing in `renderTriangle()`. Reads/writes 16-bit Z via mask image (`set.mask.dramAddress`). Supports `other.zCompare`, `other.zUpdate`, `other.zSource` (per-pixel interpolated or flat `primitiveDepth.z`). Interpolants still step correctly on depth-test failure. |
| 7 | **Blender** (new feature) | Selector-based `(P*A + M*B)/(A+B)` in `renderTriangle()`. P/M sources: combined, memory, blend color, fog color (`blend1a`/`blend2a`). A sources: combined alpha, fog alpha, shade alpha, 0 (`blend1b`). B sources: 1-A, memory alpha, 1.0, 0 (`blend2b`). Reads framebuffer via `readPixel()`. Activated by `forceBlend` or semi-transparent alpha. |
| 8 | **TLUT Address** | `loadTLUT()` now uses tile descriptor's TMEM address instead of hardcoded 0x800. Entries stored with 8-byte stride (matching real N64 TMEM quadruplication). CI8 lookup uses `texel * 8`, CI4 uses `palette * 128 + texel * 8`. |

### Still TODO
- No perspective-correct texture mapping (W division)
- No anti-aliasing / coverage
- No color/alpha dithering
- Texture filtering is nearest-neighbor only (no bilinear)
- No LOD / mipmapping
- No 2-cycle combiner mode (only cycle 0 used)
- No noise generator in combiner (returns 0)
- Copy mode DsDx fallback may not match all games (Bug 9 — minor)
- Audio still not output (no Web Audio integration)

### parallel-rdp Reference Map

| Our Software RDP | parallel-rdp Reference |
|---|---|
| `render.cpp:colorCombine()` | `parallel-rdp/parallel-rdp/shaders/combiner.h` |
| `render.cpp:fillRectangle()` | `parallel-rdp/parallel-rdp/rdp_device.cpp:op_fill_rectangle()` |
| `render.cpp:textureRectangle()` | `parallel-rdp/parallel-rdp/rdp_device.cpp:op_tex_rect()` |
| `render.cpp:loadBlock()` | `parallel-rdp/parallel-rdp/rdp_device.cpp:op_load_block()` |
| `render.cpp:renderTriangle()` | `parallel-rdp/parallel-rdp/rdp_device.cpp` triangle setup + `shaders/` rasterization |
| `render.cpp:fetchTexel()` | `parallel-rdp/parallel-rdp/shaders/texture.h` |

## Known Limitations

### Performance
- CPU runs in interpreter mode (~3-5x slower than real-time for commercial games)
- No JIT recompiler on WASM (can't generate executable code)
- Single-threaded (no Web Workers yet)

### RDP Rendering
- angrylion-rdp-plus provides pixel-perfect RDP rendering (all features supported)
- Custom software RDP still available as fallback (limited features, see changelog)
- Performance is CPU-bound; angrylion parallelizes across Web Workers via pthreads

### Audio
- Audio samples are drained but not output (no Web Audio integration yet)

### Missing Features
- Save states
- SRAM/EEPROM/Flash save persistence
- PAL region support (NTSC only)
- 64DD support
- Multiple controller ports (only port 1)

## File Structure

```
ares-wasm/
  web/                    # Browser frontend
    index.html            # Main page with controls
    ares.js               # Emscripten JS (built)
    ares.wasm             # WASM binary (built)
    ares.data             # Preloaded PIF ROMs (built)
  wasm-patches/           # Source patches applied to wasm branch
    ares/                 # ares core patches
    nall/                 # nall library patches
    ruby/                 # ruby driver patches
    libco/                # coroutine backend
    thirdparty/           # sljit patches
    web-ui/               # WASM frontend source
  build_wasm/             # git worktree (wasm branch + patches)
  build-run.bat           # One-click build script
  parallel-rdp/           # Reference: Themaister's parallel-rdp (submodule)
  screenshots/            # Screenshot output directory
```
