# Claude Code Rules

- You may commit and push changes to the designated feature branch (the branch name provided by the harness/task, typically `claude/*`). Do NOT push to `main` or force-push any branch.
- Builds are validated by the GitHub Actions workflow in `.github/workflows/deploy-pages.yml`; a Windows/`build.bat` run is not required from Claude Code on the web — the Linux-based CI handles the wasm build after push.
- The ares source tree lives in the `ares/` git submodule (pinned to HailToDodongo/ares-wasm). Do not commit modifications directly inside the submodule. To override ares files for the WASM build, add/update files under `wasm-patches/ares/...`; the CI copies `wasm-patches/*` on top of `build/` after seeding it from the submodule, so anything placed there wins at build time.
- This emulator targets libdragon homebrew. libdragon ships custom RSP microcodes. NEVER suggest or implement HLE microcode emulation (rsp-hle, rsp-cxd4, F3DEX pattern matching, etc.) — it will not work for libdragon. All RSP optimization must be LLE: recompiler, interpreter optimization, dispatch threading, etc.
