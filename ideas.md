# Performance Improvement Plan

## Current Profile (per browser frame avg)

```
CPU loop:        17.67 ms   (33%)
Sync (subsys):   34.59 ms   (66%)  <-- biggest bottleneck
  RDP angrylion: 12.18 ms
  RSP + VI + AI: ~22 ms
WebGPU render:    0.32 ms   (<1%)
Total:           52.79 ms
Frame range:     50.28 .. 57.23 ms

N64 FPS:   33.4  (target: 60)
Browser:   18.9
MIPS:      23.12 M
```

## Root Causes

1. **Angrylion RDP is single-threaded** -- `parallel_stub.c` forces `num_workers = 1`
   and runs `task(0)` on the main thread. The real parallel code supports scanline
   threading but the stub kills it. This accounts for ~12 ms/frame.

2. **CPU interpreter only** -- `recompiler.cpp` is `#ifdef`'d out for `__wasm__`
   because `nall/recompiler/generic` only targets amd64/arm64/ppc64.
   Every VR4300 instruction goes through the full decode-dispatch path.

3. **RSP interpreter only** -- same guard, same reason. RSP microcodes are
   compute-heavy (geometry, audio mixing) and run thousands of vector ops per frame.

4. **Sync interval = 128 instructions** -- reasonable tradeoff, but subsystem
   `main()` calls (VI, AI, RSP, RDP, PIF) happen every 128 CPU instructions.
   Most of those calls do nothing (clock hasn't expired) but still pay call overhead.

---

## Ideas (ordered by expected impact / effort)

### 1. ~~Enable angrylion parallel workers~~ TESTED - REJECTED

**Tried**: The Makefile was already linking the real `parallel.cpp` (not the stub)
with 4 pthread workers. Tested with condition_variable-based synchronization.

**Result**: Performance got WORSE. MIPS dropped from 23M to 7M, RDP time nearly
doubled (12ms -> 22ms), total frame time increased. The mutex/condvar overhead
in WASM pthreads (Atomics/futex roundtrips through the browser) is far too high
for angrylion's fine-grained `parallel_run` pattern (called dozens of times per
frame for individual scanlines).

**Fix applied**: Switched to `parallel_stub.c` (single-threaded) which is actually
faster. The pthread pool is no longer needed for angrylion.

**Lesson**: WASM pthreads are only worth it for coarse-grained parallelism (one
big chunk of work per thread), not fine-grained fork/join patterns. See idea #5
(RDP Web Worker offload) for a better threading approach.

---

### 2. Increase sync interval / lazy subsystem dispatch (MEDIUM impact, LOW effort)

**Problem**: Every 128 CPU instructions we call `synchronize()`, which calls
`vi.main()`, `ai.main()`, `rsp.main()`, `rdp.main()`, `pif.main()`, and
`queue.step()`. Most of these return immediately (clock >= 0) but still cost
function call + branch overhead. At ~23 MIPS that's ~180k sync calls per second.

**Options**:
- **Increase SYNC_INTERVAL to 256 or 512**: fewer sync calls, slight accuracy
  loss (libdragon is tolerant -- no cycle-exact timing tricks).
- **Lazy dispatch**: only call a subsystem's `main()` when its clock is actually
  negative. Add `if(vi.clock < 0) vi.main();` guards. Saves the function call
  overhead for subsystems that haven't accumulated enough clocks.
- **Combined**: increase interval AND add guards.

**Expected gain**: ~10-20% reduction in sync overhead (~3-7 ms saved).

**Files**: `wasm-patches/web-ui/main.cpp` (SYNC_INTERVAL),
`wasm-patches/ares/n64/cpu/cpu.cpp` (synchronize method)

---

### 3. RSP interpreter fast-path optimization (MEDIUM impact, MEDIUM effort)

**Problem**: RSP interpreter decodes every instruction from scratch. For libdragon
microcodes, the RSP runs tight loops of vector math (VU instructions).

**Options**:
- **Computed goto dispatch**: replace switch/case with a jump table using
  `__attribute__((musttail))` or computed goto. Clang/emscripten supports this.
- **Hot-path inlining**: identify the most common RSP opcodes in libdragon
  microcodes and manually inline/simplify them.
- **WASM SIMD verification**: build already has `-msimd128` but verify that
  vector ops (VCL, VCH, VMULF, VMACF, etc.) actually emit WASM SIMD and not
  scalar fallbacks.

**Expected gain**: 10-30% RSP speedup (RSP is a large chunk of the ~22 ms
non-RDP sync time).

**Files**: `ares/n64/rsp/interpreter*.cpp`, `ares/n64/rsp/rsp.cpp`

---

### 4. CPU interpreter optimization (MEDIUM impact, MEDIUM effort)

**Problem**: VR4300 interpreter at 23 MIPS uses ~17 ms/frame. Native ares with
recompiler runs at 100+ MIPS.

**Options**:
- **Threaded interpreter**: pre-decode instructions into an opcode table, use
  computed goto for dispatch. Eliminates fetch + decode overhead per instruction.
- **Block caching**: cache decoded instruction sequences keyed by PC. Avoids
  re-decoding hot loops.
- **TLB fast-path**: TLB lookups are expensive -- add a small direct-mapped
  cache for the most recent translations.

**Expected gain**: 20-40% CPU improvement (~4-7 ms saved).

**Files**: `ares/n64/cpu/interpreter*.cpp`, `ares/n64/cpu/cpu.cpp`

---

### 5. Offload RDP to a Web Worker (HIGH impact, HIGH effort)

**Problem**: Even with parallel scanline workers, angrylion still blocks the main
thread. The CPU stalls while RDP processes display lists.

**Approach**: Run angrylion in a dedicated Web Worker communicating via
SharedArrayBuffer. The CPU thread writes RDP commands to a shared ring buffer;
the RDP worker processes them asynchronously. Sync only on SYNC_FULL.

This decouples CPU and RDP execution, letting the CPU continue while RDP renders
the previous frame.

**Expected gain**: Could hide most of the 12 ms RDP cost. Total could drop
to ~35-40 ms (approaching 30 fps N64).

**Complexity**: High -- needs shared memory protocol, careful synchronization
for RDRAM reads, and correct interrupt delivery back to the CPU thread.

**Files**: `wasm-patches/angrylion/`, `wasm-patches/web-ui/main.cpp`,
new worker JS glue

---

### 6. WASM-targeted CPU recompiler (VERY HIGH impact, VERY HIGH effort)

**Problem**: The existing recompiler targets native ISAs. WASM has no JIT, but
we could AOT-compile hot N64 code blocks to WASM functions at load time.

**Approach**: At ROM load, scan and translate hot code regions into WASM modules
using the browser's `WebAssembly.compile()`. Map N64 registers to WASM locals.
Fall back to interpreter for self-modifying code.

**Expected gain**: 3-5x CPU throughput improvement. Would bring MIPS from ~23
to ~70-100+.

**Complexity**: Very high -- essentially writing a new compiler backend. Could
start with a simpler "cached interpreter" (idea 4) as a stepping stone.

---

## Recommended Order

| Phase | Idea | Effort | Expected ms saved |
|-------|------|--------|-------------------|
| 1     | Angrylion parallel workers | 1-2 days | 4-8 ms |
| 2     | Lazy sync dispatch + interval increase | 1 day | 3-7 ms |
| 3     | RSP interpreter fast-path | 3-5 days | 2-5 ms |
| 4     | CPU interpreter optimization | 3-5 days | 4-7 ms |
| 5     | RDP Web Worker offload | 1-2 weeks | 8-12 ms |
| 6     | WASM recompiler | months | 15-30 ms |

Phases 1+2 alone could bring total from ~53 ms to ~40 ms (~25 FPS).
Phases 1-4 could reach ~30-35 ms (~30 FPS, full N64 speed for many games).
Phase 5 could push past 30 FPS toward 60.
