// Bridge between ares N64 emulator and angrylion-rdp-plus
// Async mode: RDP processing runs on a dedicated pthread worker.
// Uses mutex/condvar for wake-up (one signal per dispatch, not per-scanline).

#include "angrylion_bridge.h"
#include "n64video.h"
#include <string.h>
#include <pthread.h>
#include <stdatomic.h>

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

// ── Async worker ────────────────────────────────────────────

static pthread_t rdp_thread;
static pthread_mutex_t rdp_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t rdp_cond_work = PTHREAD_COND_INITIALIZER;
static pthread_cond_t rdp_cond_done = PTHREAD_COND_INITIALIZER;
static volatile int rdp_has_work = 0;
static volatile int rdp_busy = 0;
static volatile int rdp_shutdown = 0;
static int worker_started = 0;

static void* rdp_worker(void* arg)
{
    (void)arg;
    pthread_mutex_lock(&rdp_mutex);
    for (;;) {
        // Wait for work
        while (!rdp_has_work && !rdp_shutdown) {
            pthread_cond_wait(&rdp_cond_work, &rdp_mutex);
        }
        if (rdp_shutdown) break;

        rdp_has_work = 0;
        pthread_mutex_unlock(&rdp_mutex);

        // Do the actual RDP work (outside the lock)
        n64video_process_list();

        pthread_mutex_lock(&rdp_mutex);
        rdp_busy = 0;
        pthread_cond_signal(&rdp_cond_done);
    }
    pthread_mutex_unlock(&rdp_mutex);
    return NULL;
}

// ── Public API ──────────────────────────────────────────────

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

    config.parallel = true;
    config.num_workers = 4;
    config.busyloop = false;

    config.vi.mode = VI_MODE_COLOR;
    config.vi.interp = VI_INTERP_NEAREST;
    config.dp.compat = DP_COMPAT_LOW;

    n64video_init(&config);
}

void angrylion_dispatch(uint32_t dp_start, uint32_t dp_end,
                        uint32_t dp_current, uint32_t dp_status)
{
    // Lazy-start worker on first dispatch
    if (!worker_started) {
        pthread_create(&rdp_thread, NULL, rdp_worker, NULL);
        worker_started = 1;
    }

    // Wait for previous work to finish
    pthread_mutex_lock(&rdp_mutex);
    while (rdp_busy) {
        pthread_cond_wait(&rdp_cond_done, &rdp_mutex);
    }

    // Set up registers for the worker
    dp_regs[DP_START]   = dp_start;
    dp_regs[DP_END]     = dp_end;
    dp_regs[DP_CURRENT] = dp_current;
    dp_regs[DP_STATUS]  = dp_status;
    angrylion_sync_full_pending = false;
    mi_intr_reg = 0;

    // Signal worker
    rdp_busy = 1;
    rdp_has_work = 1;
    pthread_cond_signal(&rdp_cond_work);
    pthread_mutex_unlock(&rdp_mutex);
}

uint32_t angrylion_sync(void)
{
    pthread_mutex_lock(&rdp_mutex);
    while (rdp_busy) {
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
    if (worker_started) {
        pthread_mutex_lock(&rdp_mutex);
        rdp_shutdown = 1;
        pthread_cond_signal(&rdp_cond_work);
        pthread_mutex_unlock(&rdp_mutex);
        pthread_join(rdp_thread, NULL);
    }
    n64video_close();
}
