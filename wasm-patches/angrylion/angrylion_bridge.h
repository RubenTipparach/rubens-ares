// Bridge between ares N64 emulator and angrylion-rdp-plus
// Provides init/process functions that connect ares's RDRAM and registers
// to angrylion's software RDP renderer.
// Supports async mode: dispatch RDP work to a background thread,
// continue CPU/RSP, sync when needed.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

// Initialize angrylion with ares's memory pointers.
void angrylion_init(uint8_t* rdram_ptr, uint32_t rdram_size, uint8_t* dmem_ptr);

// Dispatch RDP command list asynchronously (returns immediately).
// The RDP processes in a background thread.
void angrylion_dispatch(uint32_t dp_start, uint32_t dp_end,
                        uint32_t dp_current, uint32_t dp_status);

// Wait for the background RDP thread to finish.
// Returns the updated dp_current value.
// Call this at sync_full or when you need the framebuffer to be ready.
uint32_t angrylion_sync(void);

// Combined sync dispatch (dispatch + immediate wait). Legacy API.
uint32_t angrylion_process(uint32_t dp_start, uint32_t dp_end,
                           uint32_t dp_current, uint32_t dp_status);

// Shut down angrylion.
void angrylion_close(void);

// Flag set by angrylion's sync_full handler
extern volatile bool angrylion_sync_full_pending;

#ifdef __cplusplus
}
#endif
