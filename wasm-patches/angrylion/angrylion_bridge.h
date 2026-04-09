// Bridge between ares N64 emulator and angrylion-rdp-plus
// Provides init/process functions that connect ares's RDRAM and registers
// to angrylion's software RDP renderer.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

// Initialize angrylion with ares's memory pointers.
// Call once after ares system is loaded and powered on.
//   rdram_ptr: pointer to ares's RDRAM (rdram.ram.data)
//   rdram_size: size of RDRAM (rdram.ram.size)
//   dmem_ptr: pointer to RSP DMEM (rsp.dmem.data)
void angrylion_init(uint8_t* rdram_ptr, uint32_t rdram_size, uint8_t* dmem_ptr);

// Process RDP command list.
// Call from ares's RDP::render() instead of the custom software renderer.
//   dp_start: command buffer start address (command.start)
//   dp_end: command buffer end address (command.end)
//   dp_current: current read position (command.current)
//   dp_status: DP status register (reconstructed from ares flags)
// Returns new dp_current value after processing.
uint32_t angrylion_process(uint32_t dp_start, uint32_t dp_end,
                           uint32_t dp_current, uint32_t dp_status);

// Shut down angrylion. Call on system unload.
void angrylion_close(void);

// Flag set by angrylion's sync_full handler — ares should raise MI::IRQ::DP
extern volatile bool angrylion_sync_full_pending;

#ifdef __cplusplus
}
#endif
