template<u32 Size>
inline auto Bus::read(u32 address, u32& cycles) -> u64 {
  static constexpr u64 unmapped = 0;
  address &= 0x1fff'ffff - (Size - 1);

  if(address <= 0x007f'ffff) return rdram.ram.read<Size>(address);
  if(address <= 0x03ef'ffff) return unmapped;
  if(address <= 0x03ff'ffff) return rdram.read<Size>(address, cycles);
  if(address <= 0x0407'ffff) return rsp.read<Size>(address, cycles);
  if(address <= 0x040f'ffff) return rsp.status.read<Size>(address, cycles);
  if(address <= 0x041f'ffff) return rdp.read<Size>(address, cycles);
  if(address <= 0x042f'ffff) return rdp.io.read<Size>(address, cycles);
  if(address <= 0x043f'ffff) return mi.read<Size>(address, cycles);
  if(address <= 0x044f'ffff) return vi.read<Size>(address, cycles);
  if(address <= 0x045f'ffff) return ai.read<Size>(address, cycles);
  if(address <= 0x046f'ffff) return pi.read<Size>(address, cycles);
  if(address <= 0x047f'ffff) return ri.read<Size>(address, cycles);
  if(address <= 0x048f'ffff) return si.read<Size>(address, cycles);
  if(address <= 0x04ff'ffff) return unmapped;
  if(address <= 0x1fbf'ffff) return pi.read<Size>(address, cycles);
  if(address <= 0x1fcf'ffff) return si.read<Size>(address, cycles);
  if(address <= 0x7fff'ffff) return pi.read<Size>(address, cycles);
  return unmapped;
}

template<u32 Size>
inline auto Bus::write(u32 address, u64 data, u32& cycles) -> void {
  address &= 0x1fff'ffff - (Size - 1);
  if constexpr(Accuracy::CPU::Recompiler) {
    cpu.recompiler.invalidate(address + 0); if constexpr(Size == Dual)
    cpu.recompiler.invalidate(address + 4);
  }

  if(address <= 0x007f'ffff) {
    //MI repeat-mode fill: the very next RDRAM store after the CPU arms the
    //latch (by writing MI_WMODE_SET_REPEAT_MODE|len-1 to MI_MODE) is replayed
    //by the MI fabric across (repeatLength + 1) bytes starting at the store
    //target, then the latch + init-mode both clear. libdragon's mi_memset.S
    //is the canonical reference for this contract and only uses sd stores.
    if(mi.io.repeatModeLatch) {
      mi.io.repeatModeLatch = 0;
      mi.io.initializeMode  = 0;
      if constexpr(Size == Dual || Size == Word) {
        u64 pattern;
        if constexpr(Size == Dual) {
          pattern = data;
        } else {
          //sw: broadcast the 32-bit word across both halves of the 64-bit pattern.
          u32 word = (u32)data;
          pattern = ((u64)word << 32) | (u64)word;
        }
        u32 length = (u32)mi.io.repeatLength + 1;
        u32 offset = 0;
        while(offset + 8 <= length) {
          u32 target = address + offset;
          rdram.ram.write<Dual>(target, pattern);
          if constexpr(Accuracy::CPU::Recompiler) {
            cpu.recompiler.invalidate(target + 0);
            cpu.recompiler.invalidate(target + 4);
          }
          offset += 8;
        }
        if(offset + 4 <= length) {
          u32 target = address + offset;
          rdram.ram.write<Word>(target, (u32)(pattern >> 32));
          if constexpr(Accuracy::CPU::Recompiler) cpu.recompiler.invalidate(target);
          offset += 4;
        }
        while(offset < length) {
          u32 target = address + offset;
          u32 shift  = (7 - (offset & 7)) * 8;
          rdram.ram.write<Byte>(target, (u8)(pattern >> shift));
          offset += 1;
        }
        return;
      }
      //Size == Byte or Half: the latch still drains on the next access, but
      //libdragon never uses sub-word stores with repeat mode, so fall through
      //to a plain write instead of speculating on hardware semantics.
    }
    return rdram.ram.write<Size>(address, data);
  }
  if(address <= 0x03ef'ffff) return;
  if(address <= 0x03ff'ffff) return rdram.write<Size>(address, data, cycles);
  if(address <= 0x0407'ffff) return rsp.write<Size>(address, data, cycles);
  if(address <= 0x040f'ffff) return rsp.status.write<Size>(address, data, cycles);
  if(address <= 0x041f'ffff) return rdp.write<Size>(address, data, cycles);
  if(address <= 0x042f'ffff) return rdp.io.write<Size>(address, data, cycles);
  if(address <= 0x043f'ffff) return mi.write<Size>(address, data, cycles);
  if(address <= 0x044f'ffff) return vi.write<Size>(address, data, cycles);
  if(address <= 0x045f'ffff) return ai.write<Size>(address, data, cycles);
  if(address <= 0x046f'ffff) return pi.write<Size>(address, data, cycles);
  if(address <= 0x047f'ffff) return ri.write<Size>(address, data, cycles);
  if(address <= 0x048f'ffff) return si.write<Size>(address, data, cycles);
  if(address <= 0x04ff'ffff) return;
  if(address <= 0x1fbf'ffff) return pi.write<Size>(address, data, cycles);
  if(address <= 0x1fcf'ffff) return si.write<Size>(address, data, cycles);
  if(address <= 0x7fff'ffff) return pi.write<Size>(address, data, cycles);
  return;
}
