// Bridge between ares N64 emulator and angrylion-rdp-plus
//
// Supports async mode: dispatch RDP work to a background pthread,
// CPU/RSP continues on main thread, sync when sync_full is needed.

#include "angrylion_bridge.h"
#include "n64video.h"
#include <string.h>
#include <pthread.h>

// Register storage
static uint32_t dp_regs[DP_NUM_REG];
static uint32_t vi_regs[VI_NUM_REG];
static uint32_t mi_intr_reg;
static uint32_t* dp_reg_ptrs[DP_NUM_REG];
static uint32_t* vi_reg_ptrs[VI_NUM_REG];

// Sync full flag
volatile bool angrylion_sync_full_pending = false;

// Async state
static pthread_t rdp_thread;
static pthread_mutex_t rdp_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t rdp_cond_start = PTHREAD_COND_INITIALIZER;
static pthread_cond_t rdp_cond_done = PTHREAD_COND_INITIALIZER;
static volatile bool rdp_work_pending = false;
static volatile bool rdp_busy = false;
static volatile bool rdp_thread_running = false;

static void mi_intr_callback(void)
{
    angrylion_sync_full_pending = true;
}

// Background RDP worker thread
static void* rdp_worker(void* arg)
{
    (void)arg;
    while (rdp_thread_running) {
        pthread_mutex_lock(&rdp_mutex);
        while (!rdp_work_pending && rdp_thread_running) {
            pthread_cond_wait(&rdp_cond_start, &rdp_mutex);
        }
        if (!rdp_thread_running) {
            pthread_mutex_unlock(&rdp_mutex);
            break;
        }
        rdp_work_pending = false;
        rdp_busy = true;
        pthread_mutex_unlock(&rdp_mutex);

        // Do the actual RDP work
        n64video_process_list();

        pthread_mutex_lock(&rdp_mutex);
        rdp_busy = false;
        pthread_cond_signal(&rdp_cond_done);
        pthread_mutex_unlock(&rdp_mutex);
    }
    return NULL;
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

    // Parallel scanline rendering within angrylion
    // Must match -sPTHREAD_POOL_SIZE in GNUmakefile (we need +1 for the dispatch thread)
    config.parallel = true;
    config.num_workers = 3;  // 3 scanline workers + 1 dispatch thread = 4 pool threads
    config.busyloop = false;

    config.vi.mode = VI_MODE_COLOR;
    config.vi.interp = VI_INTERP_NEAREST;
    config.dp.compat = DP_COMPAT_LOW;

    n64video_init(&config);

    // Start background RDP dispatch thread
    rdp_thread_running = true;
    pthread_create(&rdp_thread, NULL, rdp_worker, NULL);
}

void angrylion_dispatch(uint32_t dp_start, uint32_t dp_end,
                        uint32_t dp_current, uint32_t dp_status)
{
    // Wait for any previous RDP work to finish first
    pthread_mutex_lock(&rdp_mutex);
    while (rdp_busy) {
        pthread_cond_wait(&rdp_cond_done, &rdp_mutex);
    }
    pthread_mutex_unlock(&rdp_mutex);

    // Set up registers
    dp_regs[DP_START]   = dp_start;
    dp_regs[DP_END]     = dp_end;
    dp_regs[DP_CURRENT] = dp_current;
    dp_regs[DP_STATUS]  = dp_status;
    angrylion_sync_full_pending = false;
    mi_intr_reg = 0;

    // Signal the worker to start
    pthread_mutex_lock(&rdp_mutex);
    rdp_work_pending = true;
    pthread_cond_signal(&rdp_cond_start);
    pthread_mutex_unlock(&rdp_mutex);
}

uint32_t angrylion_sync(void)
{
    // Wait for RDP to finish
    pthread_mutex_lock(&rdp_mutex);
    while (rdp_busy || rdp_work_pending) {
        pthread_cond_wait(&rdp_cond_done, &rdp_mutex);
    }
    pthread_mutex_unlock(&rdp_mutex);

    return dp_regs[DP_CURRENT];
}

uint32_t angrylion_process(uint32_t dp_start, uint32_t dp_end,
                           uint32_t dp_current, uint32_t dp_status)
{
    angrylion_dispatch(dp_start, dp_end, dp_current, dp_status);
    return angrylion_sync();
}

void angrylion_close(void)
{
    rdp_thread_running = false;
    pthread_mutex_lock(&rdp_mutex);
    pthread_cond_signal(&rdp_cond_start);
    pthread_mutex_unlock(&rdp_mutex);
    pthread_join(rdp_thread, NULL);
    n64video_close();
}
