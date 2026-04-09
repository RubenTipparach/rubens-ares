# rubens-ares

N64 emulator running in the browser via WebAssembly + WebGPU.

Built on [ares](https://github.com/ares-emulator/ares) (included as a git submodule) with a custom software RDP renderer and WebGPU display pipeline.

## Quick Start

```bash
# Clone with submodule
git clone --recursive https://github.com/RubenTipparach/rubens-ares.git
cd rubens-ares

# Install dependencies (emsdk, npm)
install.bat

# Build and run
build-run.bat
```

Then open http://localhost:3000 and load a ROM.

## Requirements

- [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html)
- [Node.js](https://nodejs.org/) (for dev server)
- Chrome 113+ or Edge 113+ (WebGPU required)
- GNU Make (comes with emsdk)

## What this adds to ares

| Component | Description |
|-----------|-------------|
| Software RDP | Triangle rasterizer, texture mapping, color combiner, TMEM — ares only had Vulkan GPU rendering |
| WebGPU display | Reads N64 framebuffer from RDRAM, uploads to GPU texture via Dawn |
| WASM frontend | ares::Platform implementation, ROM loading, keyboard input, time-sliced main loop |
| Build patches | Fixes for emscripten compilation (C++20, no threads, no JIT, no X11) |

See [wasm-webgpu.md](wasm-webgpu.md) for the full technical documentation.

## Controls

| Key | N64 | Key | N64 |
|-----|-----|-----|-----|
| X | A | A / S | L / R |
| Z | B | I / J / K / L | C-buttons |
| C | Z | W / Q / D / E | Analog stick |
| Enter | Start | Arrows | D-pad |

## Project Structure

```
rubens-ares/
  ares/              # git submodule (original ares emulator)
  wasm-patches/      # our source modifications applied over ares
  web/               # browser frontend (HTML + WebGPU)
  build-run.bat      # one-click build and serve
  install.bat        # dependency installer
  wasm-webgpu.md     # technical documentation
```

## License

ares is licensed under the [ISC License](ares/LICENSE). Custom code in this repo follows the same license.
