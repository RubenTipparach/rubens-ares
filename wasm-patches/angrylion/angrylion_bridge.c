// Bridge between ares N64 emulator and angrylion-rdp-plus
//
// angrylion expects DP/VI register pointers and an MI interrupt callback.
// We provide static register arrays that ares populates before each call.

#include "angrylion_bridge.h"
#include "n64video.h"
#include <string.h>

// Register storage — angrylion reads/writes these via pointers
static uint32_t dp_regs[DP_NUM_REG];
static uint32_t vi_regs[VI_NUM_REG];
static uint32_t mi_intr_reg;

// Pointer arrays — angrylion dereferences these (uint32_t** dp_reg means dp_reg[i] is a uint32_t*)
static uint32_t* dp_reg_ptrs[DP_NUM_REG];
static uint32_t* vi_reg_ptrs[VI_NUM_REG];

// Sync full flag — set by MI interrupt callback, read by ares
volatile bool angrylion_sync_full_pending = false;

static void mi_intr_callback(void)
{
    // angrylion calls this on sync_full — ares will check this flag
    // and raise MI::IRQ::DP
    angrylion_sync_full_pending = true;
}

void angrylion_init(uint8_t* rdram_ptr, uint32_t rdram_size, uint8_t* dmem_ptr)
{
    // Set up register pointer arrays
    for (int i = 0; i < DP_NUM_REG; i++) {
        dp_regs[i] = 0;
        dp_reg_ptrs[i] = &dp_regs[i];
    }
    for (int i = 0; i < VI_NUM_REG; i++) {
        vi_regs[i] = 0;
        vi_reg_ptrs[i] = &vi_regs[i];
    }
    mi_intr_reg = 0;

    // Configure angrylion
    struct n64video_config config;
    n64video_config_init(&config);

    config.gfx.rdram = rdram_ptr;
    config.gfx.rdram_size = rdram_size;
    config.gfx.dmem = dmem_ptr;
    config.gfx.dp_reg = dp_reg_ptrs;
    config.gfx.vi_reg = vi_reg_ptrs;
    config.gfx.mi_intr_reg = &mi_intr_reg;
    config.gfx.mi_intr_cb = mi_intr_callback;

    // Single-threaded for WASM
    config.parallel = false;
    config.num_workers = 1;

    // Fast VI mode — ares handles its own VI output
    config.vi.mode = VI_MODE_COLOR;
    config.vi.interp = VI_INTERP_NEAREST;

    // High compat — safest for accuracy
    config.dp.compat = DP_COMPAT_HIGH;

    n64video_init(&config);
}

uint32_t angrylion_process(uint32_t dp_start, uint32_t dp_end,
                           uint32_t dp_current, uint32_t dp_status)
{
    // Populate DP registers for angrylion
    dp_regs[DP_START]   = dp_start;
    dp_regs[DP_END]     = dp_end;
    dp_regs[DP_CURRENT] = dp_current;
    dp_regs[DP_STATUS]  = dp_status;

    // Clear sync flag before processing
    angrylion_sync_full_pending = false;
    mi_intr_reg = 0;

    // Process RDP commands
    n64video_process_list();

    // Return updated current position
    return dp_regs[DP_CURRENT];
}

void angrylion_close(void)
{
    n64video_close();
}
