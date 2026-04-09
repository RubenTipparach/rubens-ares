// Single-threaded parallel stub for WASM
// angrylion's parallel.h expects these functions

#include "parallel.h"

static uint32_t num_workers = 1;

void parallel_init(uint32_t num, bool busy)
{
    (void)busy;
    num_workers = (num > 0) ? 1 : 1;  // always single-threaded on WASM
}

void parallel_run(void task(uint32_t))
{
    task(0);  // run on main thread only
}

uint32_t parallel_num_workers(void)
{
    return num_workers;
}

void parallel_close(void)
{
    // nothing to clean up
}
