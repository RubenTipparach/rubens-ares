// Bridge between ares N64 emulator and angrylion-rdp-plus
// Simple synchronous bridge — no extra threads beyond angrylion's internal workers.

#include "angrylion_bridge.h"
#include "n64video.h"
#include <string.h>

static uint32_t dp_regs[DP_NUM_REG];
static uint32_t vi_regs[VI_NUM_REG];
static uint32_t mi_intr_reg;
static uint32_t* dp_reg_ptrs[DP_NUM_REG];
static uint32_t* vi_reg_ptrs[VI_NUM_REG];

volatile bool angrylion_sync_full_pending = false;

void angrylion_set_frameskip(int skip) { (void)skip; }

static void mi_intr_callback(void)
{
    angrylion_sync_full_pending = true;
}

void angrylion_init(uint8_t* rdram_ptr, uint32_t rdram_size, uint8_t* dmem_ptr)
{
    for (int i = 0; i < DP_NUM_REG; i++) {
        dp_regs[i] = 0;
        dp_reg_ptrs[i] = &dp_regs[i];
    }
    for (int i = 0; i < VI_NUM_REG; i++) {
        vi_regs[i] = 0;
        vi_reg_ptrs[i] = &vi_regs[i];
    }
    mi_intr_reg = 0;

    struct n64video_config config;
    n64video_config_init(&config);

    config.gfx.rdram = rdram_ptr;
    config.gfx.rdram_size = rdram_size;
    config.gfx.dmem = dmem_ptr;
    config.gfx.dp_reg = dp_reg_ptrs;
    config.gfx.vi_reg = vi_reg_ptrs;
    config.gfx.mi_intr_reg = &mi_intr_reg;
    config.gfx.mi_intr_cb = mi_intr_callback;

    // Internal scanline parallelism
    config.parallel = true;
    config.num_workers = 4;
    config.busyloop = false;

    config.vi.mode = VI_MODE_COLOR;
    config.vi.interp = VI_INTERP_NEAREST;
    config.dp.compat = DP_COMPAT_LOW;

    n64video_init(&config);
}

uint32_t angrylion_process(uint32_t dp_start, uint32_t dp_end,
                           uint32_t dp_current, uint32_t dp_status)
{
    dp_regs[DP_START]   = dp_start;
    dp_regs[DP_END]     = dp_end;
    dp_regs[DP_CURRENT] = dp_current;
    dp_regs[DP_STATUS]  = dp_status;
    angrylion_sync_full_pending = false;
    mi_intr_reg = 0;

    n64video_process_list();

    return dp_regs[DP_CURRENT];
}

void angrylion_dispatch(uint32_t dp_start, uint32_t dp_end,
                        uint32_t dp_current, uint32_t dp_status)
{
    angrylion_process(dp_start, dp_end, dp_current, dp_status);
}

uint32_t angrylion_sync(void)
{
    return dp_regs[DP_CURRENT];
}

void angrylion_close(void)
{
    n64video_close();
}
