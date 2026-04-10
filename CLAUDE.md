# Claude Code Rules

- Do NOT push to remote repositories
- Do NOT create git commits unless explicitly asked
- Always ask before any git operations that modify history
- After every code change, rebuild and deploy by running: `cmd.exe /c "C:\Users\santi\repos\rubens-ares\build.bat"`
- This emulator targets libdragon homebrew. libdragon ships custom RSP microcodes. NEVER suggest or implement HLE microcode emulation (rsp-hle, rsp-cxd4, F3DEX pattern matching, etc.) — it will not work for libdragon. All RSP optimization must be LLE: recompiler, interpreter optimization, dispatch threading, etc.
