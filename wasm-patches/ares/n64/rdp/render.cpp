static const vector<string> commandNames = {
  "No_Operation", "Invalid_01", "Invalid_02", "Invalid_03",
  "Invalid_04",   "Invalid_05", "Invalid_06", "Invalid_07",
  "Unshaded_Triangle",
  "Unshaded_Zbuffer_Triangle",
  "Texture_Triangle",
  "Texture_Zbuffer_Triangle",
  "Shaded_Triangle",
  "Shaded_Zbuffer_Triangle",
  "Shaded_Texture_Triangle",
  "Shaded_Texture_Zbuffer_Triangle",
  "Invalid_10", "Invalid_11", "Invalid_12", "Invalid_13",
  "Invalid_14", "Invalid_15", "Invalid_16", "Invalid_17",
  "Invalid_18", "Invalid_19", "Invalid_1a", "Invalid_1b",
  "Invalid_1c", "Invalid_1d", "Invalid_1e", "Invalid_1f",
  "Invalid_20", "Invalid_21", "Invalid_22", "Invalid_23",
  "Texture_Rectangle",
  "Texture_Rectangle_Flip",
  "Sync_Load",
  "Sync_Pipe",
  "Sync_Tile",
  "Sync_Full",
  "Set_Key_GB",
  "Set_Key_R",
  "Set_Convert",
  "Set_Scissor",
  "Set_Primitive_Depth",
  "Set_Other_Modes",
  "Load_Texture_LUT",
  "Invalid_31",
  "Set_Tile_Size",
  "Load_Block",
  "Load_Tile",
  "Set_Tile",
  "Fill_Rectangle",
  "Set_Fill_Color",
  "Set_Fog_Color",
  "Set_Blend_Color",
  "Set_Primitive_Color",
  "Set_Environment_Color",
  "Set_Combine_Mode",
  "Set_Texture_Image",
  "Set_Mask_Image",
  "Set_Color_Image",
};

// Helper: write a pixel to the framebuffer in RDRAM
auto RDP::writePixel(u32 x, u32 y, u32 color) -> void {
  if(!set.color.dramAddress) return;
  u32 addr = set.color.dramAddress;
  u32 width = set.color.width + 1;

  if(set.color.size == 2) {
    // 16-bit framebuffer (5/5/5/1)
    u32 offset = addr + (y * width + x) * 2;
    u8 r = (color >> 16) & 0xff;
    u8 g = (color >>  8) & 0xff;
    u8 b = (color >>  0) & 0xff;
    u16 pixel = ((r >> 3) << 11) | ((g >> 3) << 6) | ((b >> 3) << 1) | 1;
    rdram.ram.write<Half>(offset, pixel);
  } else if(set.color.size == 3) {
    // 32-bit framebuffer
    u32 offset = addr + (y * width + x) * 4;
    rdram.ram.write<Word>(offset, color << 8 | 0xff);
  }
}

// Helper: read a pixel from the framebuffer
auto RDP::readPixel(u32 x, u32 y) -> u32 {
  if(!set.color.dramAddress) return 0;
  u32 addr = set.color.dramAddress;
  u32 width = set.color.width + 1;

  if(set.color.size == 2) {
    u32 offset = addr + (y * width + x) * 2;
    u16 pixel = rdram.ram.read<Half>(offset);
    u8 r = (pixel >> 11 & 0x1f) << 3;
    u8 g = (pixel >>  6 & 0x1f) << 3;
    u8 b = (pixel >>  1 & 0x1f) << 3;
    return (r << 16) | (g << 8) | b;
  } else if(set.color.size == 3) {
    u32 offset = addr + (y * width + x) * 4;
    return rdram.ram.read<Word>(offset) >> 8;
  }
  return 0;
}

// Helper: fetch a texel from TMEM
auto RDP::fetchTexel(u32 tileIdx, s32 s, s32 t) -> u32 {
  auto& td = tiles[tileIdx];

  // Apply mask (wrapping)
  if(td.s.mask) {
    u32 smask = (1 << (u32)td.s.mask) - 1;
    if(td.s.mirror && (s >> (u32)td.s.mask & 1)) s = smask - (s & smask);
    else s &= smask;
  }
  if(td.t.mask) {
    u32 tmask = (1 << (u32)td.t.mask) - 1;
    if(td.t.mirror && (t >> (u32)td.t.mask & 1)) t = tmask - (t & tmask);
    else t &= tmask;
  }

  // Clamp
  if(td.s.clamp) { if(s < 0) s = 0; if(s > (td.sh >> 2)) s = td.sh >> 2; }
  if(td.t.clamp) { if(t < 0) t = 0; if(t > (td.th >> 2)) t = td.th >> 2; }

  u32 tmemAddr = (u32)td.address * 8;
  u32 line = td.line ? (u32)td.line * 8 : 0;

  if(td.size == 2) {
    // 16-bit texels
    u32 offset = tmemAddr + t * line + s * 2;
    offset &= 0xfff;
    u16 texel = tmem[offset] << 8 | tmem[offset + 1 & 0xfff];

    if(td.format == 0) {
      // RGBA16: RRRRR GGGGG BBBBB A
      u8 r = (texel >> 11 & 0x1f) << 3;
      u8 g = (texel >>  6 & 0x1f) << 3;
      u8 b = (texel >>  1 & 0x1f) << 3;
      u8 a = (texel & 1) ? 255 : 0;
      return (a << 24) | (r << 16) | (g << 8) | b;
    }
    if(td.format == 3) {
      // IA16: I8 A8
      u8 i = texel >> 8;
      u8 a = texel & 0xff;
      return (a << 24) | (i << 16) | (i << 8) | i;
    }
    return (0xff << 24) | (texel >> 8 << 16) | (texel & 0xff << 8);
  }

  if(td.size == 3) {
    // 32-bit texels (RGBA32)
    u32 offset = tmemAddr + t * line + s * 4;
    offset &= 0xfff;
    u8 r = tmem[offset & 0xfff];
    u8 g = tmem[(offset + 1) & 0xfff];
    u8 b = tmem[(offset + 2) & 0xfff];
    u8 a = tmem[(offset + 3) & 0xfff];
    return (a << 24) | (r << 16) | (g << 8) | b;
  }

  if(td.size == 1) {
    // 8-bit texels
    u32 offset = tmemAddr + t * line + s;
    offset &= 0xfff;
    u8 texel = tmem[offset];
    if(td.format == 3) {
      // IA8: I4 A4
      u8 i = (texel >> 4) << 4;
      u8 a = (texel & 0xf) << 4;
      return (a << 24) | (i << 16) | (i << 8) | i;
    }
    if(td.format == 2) {
      // CI8: look up in TLUT (palette)
      u32 tlutAddr = 0x800 + texel * 2;
      u16 color = tmem[tlutAddr & 0xfff] << 8 | tmem[(tlutAddr + 1) & 0xfff];
      u8 r = (color >> 11 & 0x1f) << 3;
      u8 g = (color >>  6 & 0x1f) << 3;
      u8 b = (color >>  1 & 0x1f) << 3;
      return (0xff << 24) | (r << 16) | (g << 8) | b;
    }
    // I8
    return (0xff << 24) | (texel << 16) | (texel << 8) | texel;
  }

  if(td.size == 0) {
    // 4-bit texels
    u32 offset = tmemAddr + t * line + s / 2;
    offset &= 0xfff;
    u8 texel = (s & 1) ? (tmem[offset] & 0xf) : (tmem[offset] >> 4);
    if(td.format == 2) {
      // CI4: palette lookup
      u32 palBase = 0x800 + (u32)td.palette * 32 + texel * 2;
      u16 color = tmem[palBase & 0xfff] << 8 | tmem[(palBase + 1) & 0xfff];
      u8 r = (color >> 11 & 0x1f) << 3;
      u8 g = (color >>  6 & 0x1f) << 3;
      u8 b = (color >>  1 & 0x1f) << 3;
      return (0xff << 24) | (r << 16) | (g << 8) | b;
    }
    if(td.format == 3) {
      // IA4: I3 A1
      u8 i = (texel >> 1) << 5;
      u8 a = (texel & 1) ? 255 : 0;
      return (a << 24) | (i << 16) | (i << 8) | i;
    }
    // I4
    u8 i = texel << 4;
    return (0xff << 24) | (i << 16) | (i << 8) | i;
  }

  return 0xff000000;
}

// ── Command dispatch ─────────────────────────────────────────

auto RDP::render() -> void {
  #if defined(VULKAN)
  if(vulkan.enable && vulkan.render()) {
    const char *msg = vulkan.crashed();
    if(msg) crash(msg);
    return;
  }
  #endif

  auto& memory = !command.source ? rdram.ram : rsp.dmem;

  auto fetch = [&]() -> u64 {
    u64 op = memory.readUnaligned<Dual>(command.current);
    command.current += 8;
    return op;
  };

  auto fetchEdge = [&](u64 op) {
    edge.lmajor   = n1 (op >> 55);
    edge.level    = n3 (op >> 51);
    edge.tile     = n3 (op >> 48);
    edge.y.lo     = n14(op >> 32);
    edge.y.md     = n14(op >> 16);
    edge.y.hi     = n14(op >>  0);
    op = fetch();
    edge.x.lo.c.i = n16(op >> 48);
    edge.x.lo.c.f = n16(op >> 32);
    edge.x.lo.s.i = n16(op >> 16);
    edge.x.lo.s.f = n16(op >>  0);
    op = fetch();
    edge.x.hi.c.i = n16(op >> 48);
    edge.x.hi.c.f = n16(op >> 32);
    edge.x.hi.s.i = n16(op >> 16);
    edge.x.hi.s.f = n16(op >>  0);
    op = fetch();
    edge.x.md.c.i = n16(op >> 48);
    edge.x.md.c.f = n16(op >> 32);
    edge.x.md.s.i = n16(op >> 16);
    edge.x.md.s.f = n16(op >>  0);
  };

  auto fetchShade = [&](u64 op) {
    op = fetch();
    shade.r.c.i = n16(op >> 48); shade.g.c.i = n16(op >> 32);
    shade.b.c.i = n16(op >> 16); shade.a.c.i = n16(op >>  0);
    op = fetch();
    shade.r.x.i = n16(op >> 48); shade.g.x.i = n16(op >> 32);
    shade.b.x.i = n16(op >> 16); shade.a.x.i = n16(op >>  0);
    op = fetch();
    shade.r.c.f = n16(op >> 48); shade.g.c.f = n16(op >> 32);
    shade.b.c.f = n16(op >> 16); shade.a.c.f = n16(op >>  0);
    op = fetch();
    shade.r.x.f = n16(op >> 48); shade.g.x.f = n16(op >> 32);
    shade.b.x.f = n16(op >> 16); shade.a.x.f = n16(op >>  0);
    op = fetch();
    shade.r.e.i = n16(op >> 48); shade.g.e.i = n16(op >> 32);
    shade.b.e.i = n16(op >> 16); shade.a.e.i = n16(op >>  0);
    op = fetch();
    shade.r.y.i = n16(op >> 48); shade.g.y.i = n16(op >> 32);
    shade.b.y.i = n16(op >> 16); shade.a.y.i = n16(op >>  0);
    op = fetch();
    shade.r.e.f = n16(op >> 48); shade.g.e.f = n16(op >> 32);
    shade.b.e.f = n16(op >> 16); shade.a.e.f = n16(op >>  0);
    op = fetch();
    shade.r.y.f = n16(op >> 48); shade.g.y.f = n16(op >> 32);
    shade.b.y.f = n16(op >> 16); shade.a.y.f = n16(op >>  0);
  };

  auto fetchTexture = [&](u64 op) {
    op = fetch();
    texture.s.c.i = n16(op >> 48); texture.t.c.i = n16(op >> 32); texture.w.c.i = n16(op >> 16);
    op = fetch();
    texture.s.x.i = n16(op >> 48); texture.t.x.i = n16(op >> 32); texture.w.x.i = n16(op >> 16);
    op = fetch();
    texture.s.c.f = n16(op >> 48); texture.t.c.f = n16(op >> 32); texture.w.c.f = n16(op >> 16);
    op = fetch();
    texture.s.x.f = n16(op >> 48); texture.t.x.f = n16(op >> 32); texture.w.x.f = n16(op >> 16);
    op = fetch();
    texture.s.e.i = n16(op >> 48); texture.t.e.i = n16(op >> 32); texture.w.e.i = n16(op >> 16);
    op = fetch();
    texture.s.y.i = n16(op >> 48); texture.t.y.i = n16(op >> 32); texture.w.y.i = n16(op >> 16);
    op = fetch();
    texture.s.e.f = n16(op >> 48); texture.t.e.f = n16(op >> 32); texture.w.e.f = n16(op >> 16);
    op = fetch();
    texture.s.y.f = n16(op >> 48); texture.t.y.f = n16(op >> 32); texture.w.y.f = n16(op >> 16);
  };

  auto fetchZBuffer = [&](u64 op) {
    op = fetch();
    zbuffer.d.i = n16(op >> 48); zbuffer.d.f = n16(op >> 32);
    zbuffer.x.i = n16(op >> 16); zbuffer.x.f = n16(op >>  0);
    op = fetch();
    zbuffer.e.i = n16(op >> 48); zbuffer.e.f = n16(op >> 32);
    zbuffer.y.i = n16(op >> 16); zbuffer.y.f = n16(op >>  0);
  };

  auto fetchRectangle = [&](u64 op) {
    rectangle.x.lo = n12(op >> 44);
    rectangle.y.lo = n12(op >> 32);
    rectangle.tile = n3 (op >> 24);
    rectangle.x.hi = n12(op >> 12);
    rectangle.y.hi = n12(op >>  0);
    op = fetch();
    rectangle.s.i  = n16(op >> 48);
    rectangle.t.i  = n16(op >> 32);
    rectangle.s.f  = n16(op >> 16);
    rectangle.t.f  = n16(op >>  0);
  };

  while(command.current < command.end) {
    u64 op = fetch();

    if(debugger.tracer.command->enabled()) {
      debugger.command({hex(op, 16L), "  ", commandNames(op >> 56 & 0x3f, "Invalid")});
    }

    switch(op >> 56 & 0x3f) {
    case 0x00: noOperation(); break;
    case 0x01: case 0x02: case 0x03: case 0x04:
    case 0x05: case 0x06: case 0x07: invalidOperation(); break;
    case 0x08: fetchEdge(op); unshadedTriangle(); break;
    case 0x09: fetchEdge(op); fetchZBuffer(op); unshadedZbufferTriangle(); break;
    case 0x0a: fetchEdge(op); fetchTexture(op); textureTriangle(); break;
    case 0x0b: fetchEdge(op); fetchTexture(op); fetchZBuffer(op); textureZbufferTriangle(); break;
    case 0x0c: fetchEdge(op); fetchShade(op); shadedTriangle(); break;
    case 0x0d: fetchEdge(op); fetchShade(op); fetchZBuffer(op); shadedZbufferTriangle(); break;
    case 0x0e: fetchEdge(op); fetchShade(op); fetchTexture(op); shadedTextureTriangle(); break;
    case 0x0f: fetchEdge(op); fetchShade(op); fetchTexture(op); fetchZBuffer(op); shadedTextureZbufferTriangle(); break;
    case 0x10: case 0x11: case 0x12: case 0x13:
    case 0x14: case 0x15: case 0x16: case 0x17:
    case 0x18: case 0x19: case 0x1a: case 0x1b:
    case 0x1c: case 0x1d: case 0x1e: case 0x1f:
    case 0x20: case 0x21: case 0x22: case 0x23: invalidOperation(); break;
    case 0x24: fetchRectangle(op); textureRectangle(); break;
    case 0x25: fetchRectangle(op); textureRectangleFlip(); break;
    case 0x26: syncLoad(); break;
    case 0x27: syncPipe(); break;
    case 0x28: syncTile(); break;
    case 0x29: syncFull(); break;
    case 0x2a:
      key.g.width  = n12(op >> 44); key.b.width  = n12(op >> 32);
      key.g.center = n8 (op >> 24); key.g.scale  = n8 (op >> 16);
      key.b.center = n8 (op >>  8); key.b.scale  = n8 (op >>  0);
      setKeyGB(); break;
    case 0x2b:
      key.r.width  = n12(op >> 16); key.r.center = n8(op >> 8); key.r.scale = n8(op >> 0);
      setKeyR(); break;
    case 0x2c:
      convert.k[0] = n9(op >> 45); convert.k[1] = n9(op >> 36);
      convert.k[2] = n9(op >> 27); convert.k[3] = n9(op >> 18);
      convert.k[4] = n9(op >>  9); convert.k[5] = n8(op >>  0);
      setConvert(); break;
    case 0x2d:
      scissor.x.hi = n12(op >> 44); scissor.y.hi = n12(op >> 32);
      scissor.field = n1(op >> 25);  scissor.odd  = n1(op >> 24);
      scissor.x.lo = n12(op >> 12); scissor.y.lo = n12(op >>  0);
      setScissor(); break;
    case 0x2e:
      primitiveDepth.z = n16(op >> 16); primitiveDepth.deltaZ = n16(op >> 0);
      setPrimitiveDepth(); break;
    case 0x2f:
      other.atomicPrimitive = n1(op >> 55); other.reserved1 = n1(op >> 54);
      other.cycleType = n2(op >> 52); other.perspective = n1(op >> 51);
      other.detailTexture = n1(op >> 50); other.sharpenTexture = n1(op >> 49);
      other.lodTexture = n1(op >> 48); other.tlut = n1(op >> 47);
      other.tlutType = n1(op >> 46); other.sampleType = n1(op >> 45);
      other.midTexel = n1(op >> 44);
      other.bilerp[0] = n1(op >> 43); other.bilerp[1] = n1(op >> 42);
      other.convertOne = n1(op >> 41); other.colorKey = n1(op >> 40);
      other.colorDitherMode = n2(op >> 38); other.alphaDitherMode = n2(op >> 36);
      other.reserved2 = n4(op >> 32);
      other.blend1a[0] = n2(op >> 30); other.blend1a[1] = n2(op >> 28);
      other.blend1b[0] = n2(op >> 26); other.blend1b[1] = n2(op >> 24);
      other.blend2a[0] = n2(op >> 22); other.blend2a[1] = n2(op >> 20);
      other.blend2b[0] = n2(op >> 18); other.blend2b[1] = n2(op >> 16);
      other.reserved3 = n1(op >> 15); other.forceBlend = n1(op >> 14);
      other.alphaCoverage = n1(op >> 13); other.coverageXalpha = n1(op >> 12);
      other.zMode = n2(op >> 10); other.coverageMode = n2(op >> 8);
      other.colorOnCoverage = n1(op >> 7); other.imageRead = n1(op >> 6);
      other.zUpdate = n1(op >> 5); other.zCompare = n1(op >> 4);
      other.antialias = n1(op >> 3); other.zSource = n1(op >> 2);
      other.ditherAlpha = n1(op >> 1); other.alphaCompare = n1(op >> 0);
      setOtherModes(); break;
    case 0x30:
      tlut.s.lo = n12(op >> 44); tlut.t.lo = n12(op >> 32);
      tlut.index = n3(op >> 24);
      tlut.s.hi = n12(op >> 12); tlut.t.hi = n12(op >> 0);
      loadTLUT(); break;
    case 0x31: invalidOperation(); break;
    case 0x32:
      tileSize.s.lo = n12(op >> 44); tileSize.t.lo = n12(op >> 32);
      tileSize.index = n3(op >> 24);
      tileSize.s.hi = n12(op >> 12); tileSize.t.hi = n12(op >> 0);
      setTileSize(); break;
    case 0x33:
      load_.block.s.lo = n12(op >> 44); load_.block.t.lo = n12(op >> 32);
      load_.block.index = n3(op >> 24);
      load_.block.s.hi = n12(op >> 12); load_.block.t.hi = n12(op >> 0);
      loadBlock(); break;
    case 0x34:
      load_.tile.s.lo = n12(op >> 44); load_.tile.t.lo = n12(op >> 32);
      load_.tile.index = n3(op >> 24);
      load_.tile.s.hi = n12(op >> 12); load_.tile.t.hi = n12(op >> 0);
      loadTile(); break;
    case 0x35:
      tile.format = n3(op >> 53); tile.size = n2(op >> 51);
      tile.line = n9(op >> 41); tile.address = n9(op >> 32);
      tile.index = n3(op >> 24); tile.palette = n4(op >> 20);
      tile.t.clamp = n1(op >> 19); tile.t.mirror = n1(op >> 18);
      tile.t.mask = n4(op >> 14); tile.t.shift = n4(op >> 10);
      tile.s.clamp = n1(op >> 9); tile.s.mirror = n1(op >> 8);
      tile.s.mask = n4(op >> 4); tile.s.shift = n4(op >> 0);
      setTile(); break;
    case 0x36:
      fillRectangle_.x.lo = n12(op >> 44); fillRectangle_.y.lo = n12(op >> 32);
      fillRectangle_.x.hi = n12(op >> 12); fillRectangle_.y.hi = n12(op >> 0);
      fillRectangle(); break;
    case 0x37: set.fill.color = n32(op >> 0); setFillColor(); break;
    case 0x38:
      fog.red = n8(op >> 24); fog.green = n8(op >> 16);
      fog.blue = n8(op >> 8); fog.alpha = n8(op >> 0);
      setFogColor(); break;
    case 0x39:
      blend.red = n8(op >> 24); blend.green = n8(op >> 16);
      blend.blue = n8(op >> 8); blend.alpha = n8(op >> 0);
      setBlendColor(); break;
    case 0x3a:
      primitive.minimum = n4(op >> 40); primitive.fraction = n8(op >> 32);
      primitive.red = n8(op >> 24); primitive.green = n8(op >> 16);
      primitive.blue = n8(op >> 8); primitive.alpha = n8(op >> 0);
      setPrimitiveColor(); break;
    case 0x3b:
      environment.red = n8(op >> 24); environment.green = n8(op >> 16);
      environment.blue = n8(op >> 8); environment.alpha = n8(op >> 0);
      setEnvironmentColor(); break;
    case 0x3c:
      combine.sba.color[0] = n4(op >> 52); combine.mul.color[0] = n5(op >> 47);
      combine.sba.alpha[0] = n3(op >> 44); combine.mul.alpha[0] = n3(op >> 41);
      combine.sba.color[1] = n4(op >> 37); combine.mul.color[1] = n5(op >> 32);
      combine.sbb.color[0] = n4(op >> 28); combine.sbb.color[1] = n4(op >> 24);
      combine.sba.alpha[1] = n3(op >> 21); combine.mul.alpha[1] = n3(op >> 18);
      combine.add.color[0] = n3(op >> 15); combine.sbb.alpha[0] = n3(op >> 12);
      combine.add.alpha[0] = n3(op >> 9);  combine.add.color[1] = n3(op >> 6);
      combine.sbb.alpha[1] = n3(op >> 3);  combine.add.alpha[1] = n3(op >> 0);
      setCombineMode(); break;
    case 0x3d:
      set.texture.format = n3(op >> 53); set.texture.size = n2(op >> 51);
      set.texture.width = n10(op >> 32); set.texture.dramAddress = n26(op >> 0);
      setTextureImage(); break;
    case 0x3e:
      set.mask.dramAddress = n26(op >> 0);
      setMaskImage(); break;
    case 0x3f:
      set.color.format = n3(op >> 53); set.color.size = n2(op >> 51);
      set.color.width = n10(op >> 32); set.color.dramAddress = n26(op >> 0);
      setColorImage(); break;
    }
  }
}

// ── RDP command implementations ──────────────────────────────

auto RDP::noOperation() -> void {}
auto RDP::invalidOperation() -> void {}

// Fill rectangle with fill color
auto RDP::fillRectangle() -> void {
  u32 xlo = fillRectangle_.x.hi >> 2;
  u32 ylo = fillRectangle_.y.hi >> 2;
  u32 xhi = fillRectangle_.x.lo >> 2;
  u32 yhi = fillRectangle_.y.lo >> 2;

  // Scissor clamp
  u32 sxlo = scissor.x.hi >> 2, sylo = scissor.y.hi >> 2;
  u32 sxhi = scissor.x.lo >> 2, syhi = scissor.y.lo >> 2;
  if(xlo < sxlo) xlo = sxlo;
  if(ylo < sylo) ylo = sylo;
  if(xhi > sxhi) xhi = sxhi;
  if(yhi > syhi) yhi = syhi;

  u32 addr = set.color.dramAddress;
  u32 width = set.color.width + 1;

  for(u32 y = ylo; y < yhi; y++) {
    for(u32 x = xlo; x < xhi; x++) {
      if(set.color.size == 2) {
        u32 offset = addr + (y * width + x) * 2;
        // In fill mode, fill color contains two 16-bit pixels
        u16 pixel = (x & 1) ? (set.fill.color & 0xffff) : (set.fill.color >> 16);
        rdram.ram.write<Half>(offset, pixel);
      } else if(set.color.size == 3) {
        u32 offset = addr + (y * width + x) * 4;
        rdram.ram.write<Word>(offset, set.fill.color);
      }
    }
  }
}

// Color combiner: implements (A - B) * C + D per channel
auto RDP::colorCombine(u32 cycle, u8 texR, u8 texG, u8 texB, u8 texA,
                        u8 shR, u8 shG, u8 shB, u8 shA) -> u32 {
  if(other.cycleType == 3) return set.fill.color;  // fill mode
  if(other.cycleType == 2) return (texA << 24) | (texR << 16) | (texG << 8) | texB;  // copy mode

  // Per-channel color arrays: [R, G, B]
  u8 sh[3]   = {shR, shG, shB};
  u8 tex[3]  = {texR, texG, texB};
  u8 prim[3] = {(u8)(u32)primitive.red, (u8)(u32)primitive.green, (u8)(u32)primitive.blue};
  u8 env[3]  = {(u8)(u32)environment.red, (u8)(u32)environment.green, (u8)(u32)environment.blue};
  u8 keyCenter[3] = {(u8)(u32)key.r.center, (u8)(u32)key.g.center, (u8)(u32)key.b.center};
  u8 keyScale[3]  = {(u8)(u32)key.r.scale, (u8)(u32)key.g.scale, (u8)(u32)key.b.scale};

  // A (sub A): 4-bit selector, parameterized by channel
  auto getSubA_RGB = [&](u32 sel, u32 ch) -> s32 {
    switch(sel) {
      case 0: return sh[ch];    // combined (use shade for cycle 0)
      case 1: return tex[ch];
      case 2: return tex[ch];   // tex1 (same as tex0 for now)
      case 3: return prim[ch];
      case 4: return sh[ch];
      case 5: return env[ch];
      case 6: return 255;       // 1.0
      case 7: return 0;         // noise (TODO: LFSR)
      default: return 0;
    }
  };
  // B (sub B): 4-bit selector
  auto getSubB_RGB = [&](u32 sel, u32 ch) -> s32 {
    switch(sel) {
      case 0: return sh[ch];
      case 1: return tex[ch];
      case 2: return tex[ch];
      case 3: return prim[ch];
      case 4: return sh[ch];
      case 5: return env[ch];
      case 6: return (s32)keyCenter[ch];
      case 7: return 0;         // k4
      default: return 0;
    }
  };
  // C (multiply): 5-bit selector — cases 7+ are channel-independent (alpha/LOD sources)
  auto getMul_RGB = [&](u32 sel, u32 ch) -> s32 {
    switch(sel) {
      case 0: return sh[ch];
      case 1: return tex[ch];
      case 2: return tex[ch];
      case 3: return prim[ch];
      case 4: return sh[ch];
      case 5: return env[ch];
      case 6: return (s32)keyScale[ch];
      case 7: return shA;       // combined alpha
      case 8: return texA;
      case 9: return texA;
      case 10: return primitive.alpha;
      case 11: return shA;
      case 12: return environment.alpha;
      case 13: return primitive.fraction;  // LOD fraction
      case 14: return primitive.fraction;  // prim LOD
      case 15: return 0;        // k5
      default: return 0;
    }
  };
  // D (add): 3-bit selector
  auto getAdd_RGB = [&](u32 sel, u32 ch) -> s32 {
    switch(sel) {
      case 0: return sh[ch];
      case 1: return tex[ch];
      case 2: return tex[ch];
      case 3: return prim[ch];
      case 4: return sh[ch];
      case 5: return env[ch];
      case 6: return 255;
      default: return 0;
    }
  };

  // Alpha: (A - B) * C + D with 3-bit selectors (channel-independent)
  auto getSubA_A = [&](u32 sel) -> s32 {
    switch(sel) { case 0: return shA; case 1: return texA; case 2: return texA;
      case 3: return primitive.alpha; case 4: return shA; case 5: return environment.alpha;
      case 6: return 255; default: return 0; }
  };
  auto getSubB_A = [&](u32 sel) -> s32 {
    switch(sel) { case 0: return shA; case 1: return texA; case 2: return texA;
      case 3: return primitive.alpha; case 4: return shA; case 5: return environment.alpha;
      case 6: return 255; default: return 0; }
  };
  auto getMul_A = [&](u32 sel) -> s32 {
    switch(sel) { case 0: return primitive.fraction; case 1: return texA; case 2: return texA;
      case 3: return primitive.alpha; case 4: return shA; case 5: return environment.alpha;
      case 6: return 255; default: return 0; }
  };
  auto getAdd_A = [&](u32 sel) -> s32 {
    switch(sel) { case 0: return shA; case 1: return texA; case 2: return texA;
      case 3: return primitive.alpha; case 4: return shA; case 5: return environment.alpha;
      case 6: return 255; default: return 0; }
  };

  auto combineChannel = [](s32 a, s32 b, s32 c, s32 d) -> s32 {
    return std::clamp(((a - b) * c) / 256 + d, 0, 255);
  };

  u32 sbaC = combine.sba.color[cycle];
  u32 sbbC = combine.sbb.color[cycle];
  u32 mulC = combine.mul.color[cycle];
  u32 addC = combine.add.color[cycle];

  // Evaluate each RGB channel independently using the same selectors
  s32 outR = combineChannel(getSubA_RGB(sbaC, 0), getSubB_RGB(sbbC, 0), getMul_RGB(mulC, 0), getAdd_RGB(addC, 0));
  s32 outG = combineChannel(getSubA_RGB(sbaC, 1), getSubB_RGB(sbbC, 1), getMul_RGB(mulC, 1), getAdd_RGB(addC, 1));
  s32 outB = combineChannel(getSubA_RGB(sbaC, 2), getSubB_RGB(sbbC, 2), getMul_RGB(mulC, 2), getAdd_RGB(addC, 2));

  // Alpha
  u32 sbaA = combine.sba.alpha[cycle];
  u32 sbbA = combine.sbb.alpha[cycle];
  u32 mulA = combine.mul.alpha[cycle];
  u32 addA = combine.add.alpha[cycle];
  s32 outA = combineChannel(getSubA_A(sbaA), getSubB_A(sbbA), getMul_A(mulA), getAdd_A(addA));

  return ((u32)outA << 24) | ((u32)outR << 16) | ((u32)outG << 8) | (u32)outB;
}

// Texture rectangle
auto RDP::textureRectangle() -> void {
  u32 xlo = rectangle.x.hi >> 2;
  u32 ylo = rectangle.y.hi >> 2;
  u32 xhi = rectangle.x.lo >> 2;
  u32 yhi = rectangle.y.lo >> 2;

  // S and T coordinates are s.10.5 (16 bits), DsDx/DtDy are s.5.10 (16 bits)
  // Convert both to a common 10.10 fixed-point format for accumulation
  s32 s0 = (s32)(s16)rectangle.s.i << 5;   // s.10.5 -> s.10.10
  s32 t0 = (s32)(s16)rectangle.t.i << 5;   // s.10.5 -> s.10.10
  s32 dsdx = (s32)(s16)rectangle.s.f;      // s.5.10 (already 10 frac bits)
  s32 dtdy = (s32)(s16)rectangle.t.f;      // s.5.10

  // In copy mode, dsdx is typically 4.0 (= 1 texel per pixel)
  if(other.cycleType == 2) {
    if(dsdx == 0) dsdx = 1 << 10;  // 1.0 in s.5.10
    if(dtdy == 0) dtdy = 1 << 10;
  }

  u32 tileIdx = rectangle.tile;
  s32 t = t0;

  for(u32 y = ylo; y < yhi; y++) {
    s32 s = s0;
    for(u32 x = xlo; x < xhi; x++) {
      s32 ss = s >> 10;  // s.10.10 -> integer texel
      s32 tt = t >> 10;
      u32 texel = fetchTexel(tileIdx, ss, tt);

      if(other.cycleType == 2) {
        // Copy mode: write texel directly
        u8 a = texel >> 24;
        if(a > 0) writePixel(x, y, texel & 0xffffff);
      } else {
        u32 combined = colorCombine(0, texel >> 16 & 0xff, texel >> 8 & 0xff, texel & 0xff, texel >> 24,
                                     primitive.red, primitive.green, primitive.blue, primitive.alpha);
        u8 a = combined >> 24;
        if(a > 0) writePixel(x, y, combined & 0xffffff);
      }
      s += dsdx;
    }
    t += dtdy;
  }
}

auto RDP::textureRectangleFlip() -> void {
  u32 xlo = rectangle.x.hi >> 2;
  u32 ylo = rectangle.y.hi >> 2;
  u32 xhi = rectangle.x.lo >> 2;
  u32 yhi = rectangle.y.lo >> 2;

  // Same fixed-point conversion as textureRectangle
  s32 s0 = (s32)(s16)rectangle.s.i << 5;   // s.10.5 -> s.10.10
  s32 t0 = (s32)(s16)rectangle.t.i << 5;   // s.10.5 -> s.10.10
  // Flip: DsDx becomes DtDx, DtDy becomes DsDy
  s32 dtdx = (s32)(s16)rectangle.s.f;      // s.5.10 — originally dsdx, now steps T along X
  s32 dsdy = (s32)(s16)rectangle.t.f;      // s.5.10 — originally dtdy, now steps S along Y

  if(other.cycleType == 2) {
    if(dtdx == 0) dtdx = 1 << 10;
    if(dsdy == 0) dsdy = 1 << 10;
  }

  u32 tileIdx = rectangle.tile;
  s32 s = s0;

  for(u32 y = ylo; y < yhi; y++) {
    s32 t = t0;
    for(u32 x = xlo; x < xhi; x++) {
      s32 ss = s >> 10;
      s32 tt = t >> 10;
      u32 texel = fetchTexel(tileIdx, ss, tt);

      if(other.cycleType == 2) {
        u8 a = texel >> 24;
        if(a > 0) writePixel(x, y, texel & 0xffffff);
      } else {
        u32 combined = colorCombine(0, texel >> 16 & 0xff, texel >> 8 & 0xff, texel & 0xff, texel >> 24,
                                     primitive.red, primitive.green, primitive.blue, primitive.alpha);
        u8 a = combined >> 24;
        if(a > 0) writePixel(x, y, combined & 0xffffff);
      }
      t += dtdx;  // T steps along X
    }
    s += dsdy;  // S steps along Y
  }
}

// Triangle rasterization (simplified scanline approach)
auto RDP::renderTriangle(bool shade_, bool texture_, bool zbuffer_) -> void {
  // Convert 11.2 fixed-point Y coordinates
  s32 yh = (s32)(s16)edge.y.hi >> 2;
  s32 ym = (s32)(s16)edge.y.md >> 2;
  s32 yl = (s32)(s16)edge.y.lo >> 2;

  // Scissor clamp
  s32 sylo = scissor.y.hi >> 2;
  s32 syhi = scissor.y.lo >> 2;
  if(yh < sylo) yh = sylo;
  if(yl > syhi) yl = syhi;

  // Edge walking: compute x coordinates along edges using 16.16 fixed-point
  s32 xh = ((s32)(s16)edge.x.hi.c.i << 16) | (u16)edge.x.hi.c.f;
  s32 xm = ((s32)(s16)edge.x.md.c.i << 16) | (u16)edge.x.md.c.f;
  s32 xl = ((s32)(s16)edge.x.lo.c.i << 16) | (u16)edge.x.lo.c.f;

  s32 dxhdy = ((s32)(s16)edge.x.hi.s.i << 16) | (u16)edge.x.hi.s.f;
  s32 dxmdy = ((s32)(s16)edge.x.md.s.i << 16) | (u16)edge.x.md.s.f;
  s32 dxldy = ((s32)(s16)edge.x.lo.s.i << 16) | (u16)edge.x.lo.s.f;

  // Shade color (16.16 fixed-point RGBA)
  s32 sr = ((s32)(s16)shade.r.c.i << 16) | (u16)shade.r.c.f;
  s32 sg = ((s32)(s16)shade.g.c.i << 16) | (u16)shade.g.c.f;
  s32 sb = ((s32)(s16)shade.b.c.i << 16) | (u16)shade.b.c.f;
  s32 sa = ((s32)(s16)shade.a.c.i << 16) | (u16)shade.a.c.f;
  s32 drdx = ((s32)(s16)shade.r.x.i << 16) | (u16)shade.r.x.f;
  s32 dgdx = ((s32)(s16)shade.g.x.i << 16) | (u16)shade.g.x.f;
  s32 dbdx = ((s32)(s16)shade.b.x.i << 16) | (u16)shade.b.x.f;
  s32 dadx = ((s32)(s16)shade.a.x.i << 16) | (u16)shade.a.x.f;
  s32 drde = ((s32)(s16)shade.r.e.i << 16) | (u16)shade.r.e.f;
  s32 dgde = ((s32)(s16)shade.g.e.i << 16) | (u16)shade.g.e.f;
  s32 dbde = ((s32)(s16)shade.b.e.i << 16) | (u16)shade.b.e.f;
  s32 dade = ((s32)(s16)shade.a.e.i << 16) | (u16)shade.a.e.f;

  // Texture coordinates (16.16 fixed-point)
  s32 ts = ((s32)(s16)texture.s.c.i << 16) | (u16)texture.s.c.f;
  s32 tt = ((s32)(s16)texture.t.c.i << 16) | (u16)texture.t.c.f;
  s32 tw = ((s32)(s16)texture.w.c.i << 16) | (u16)texture.w.c.f;
  s32 dsdx = ((s32)(s16)texture.s.x.i << 16) | (u16)texture.s.x.f;
  s32 dtdx = ((s32)(s16)texture.t.x.i << 16) | (u16)texture.t.x.f;
  s32 dwdx = ((s32)(s16)texture.w.x.i << 16) | (u16)texture.w.x.f;
  s32 dsde = ((s32)(s16)texture.s.e.i << 16) | (u16)texture.s.e.f;
  s32 dtde = ((s32)(s16)texture.t.e.i << 16) | (u16)texture.t.e.f;
  s32 dwde = ((s32)(s16)texture.w.e.i << 16) | (u16)texture.w.e.f;

  // Z-buffer depth (16.16 fixed-point)
  s32 zval = 0, dzdx_val = 0, dzde_val = 0;
  if(zbuffer_) {
    zval     = ((s32)(s16)zbuffer.d.i << 16) | (u16)zbuffer.d.f;
    dzdx_val = ((s32)(s16)zbuffer.x.i << 16) | (u16)zbuffer.x.f;
    dzde_val = ((s32)(s16)zbuffer.e.i << 16) | (u16)zbuffer.e.f;
  }

  u32 tileIdx = edge.tile;
  u32 fbWidth = set.color.width + 1;

  // Rasterize scanlines
  for(s32 y = yh; y < yl; y++) {
    // Determine left and right X from edges
    s32 leftX, rightX;
    if(y < ym) {
      if(edge.lmajor) { leftX = xh; rightX = xm; }
      else { leftX = xm; rightX = xh; }
    } else {
      if(edge.lmajor) { leftX = xh; rightX = xl; }
      else { leftX = xl; rightX = xh; }
    }

    s32 x0 = leftX >> 16;
    s32 x1 = rightX >> 16;
    if(x0 > x1) { s32 tmp = x0; x0 = x1; x1 = tmp; }

    // Scissor X
    s32 sxlo = scissor.x.hi >> 2;
    s32 sxhi = scissor.x.lo >> 2;
    if(x0 < sxlo) x0 = sxlo;
    if(x1 > sxhi) x1 = sxhi;

    s32 cr = sr, cg = sg, cb = sb, ca = sa;
    s32 cs = ts, ct = tt, cw = tw;
    s32 cz = zval;

    for(s32 x = x0; x < x1; x++) {
      // ── Z-buffer test ──
      if(zbuffer_ && other.zCompare && set.mask.dramAddress) {
        u16 pixelZ;
        if(other.zSource) {
          pixelZ = (u16)primitiveDepth.z;  // flat Z from SetPrimitiveDepth
        } else {
          pixelZ = (u16)((u32)cz >> 16);   // per-pixel interpolated Z (upper 16 bits)
        }
        u32 zAddr = set.mask.dramAddress + ((u32)y * fbWidth + (u32)x) * 2;
        u16 storedZ = rdram.ram.read<Half>(zAddr);
        // N64 Z: lower value = closer. Skip pixel if it's behind what's already drawn.
        if(pixelZ > storedZ) {
          // Fail depth test — skip this pixel but still step interpolants
          if(shade_)   { cr += drdx; cg += dgdx; cb += dbdx; ca += dadx; }
          if(texture_) { cs += dsdx; ct += dtdx; cw += dwdx; }
          cz += dzdx_val;
          continue;
        }
      }

      u8 shR, shG, shB, shA;
      if(shade_) {
        shR = std::clamp(cr >> 16, 0, 255);
        shG = std::clamp(cg >> 16, 0, 255);
        shB = std::clamp(cb >> 16, 0, 255);
        shA = std::clamp(ca >> 16, 0, 255);
      } else {
        shR = primitive.red;
        shG = primitive.green;
        shB = primitive.blue;
        shA = primitive.alpha;
      }

      u8 texR = 255, texG = 255, texB = 255, texA = 255;
      if(texture_) {
        // Convert 16.16 texture coords to integer texel coordinates
        // S and T are in 10.5 format internally (upper 16 bits are 11.5)
        s32 si = cs >> 21;
        s32 ti = ct >> 21;
        u32 texel = fetchTexel(tileIdx, si, ti);
        texR = (texel >> 16) & 0xff;
        texG = (texel >>  8) & 0xff;
        texB = (texel >>  0) & 0xff;
        texA = (texel >> 24) & 0xff;
      }

      u32 combined = colorCombine(0, texR, texG, texB, texA, shR, shG, shB, shA);
      u8 a = combined >> 24;
      if(a > 0) {
        writePixel(x, y, combined & 0xffffff);

        // ── Z-buffer update ──
        if(zbuffer_ && other.zUpdate && set.mask.dramAddress) {
          u16 pixelZ = other.zSource ? (u16)primitiveDepth.z : (u16)((u32)cz >> 16);
          u32 zAddr = set.mask.dramAddress + ((u32)y * fbWidth + (u32)x) * 2;
          rdram.ram.write<Half>(zAddr, pixelZ);
        }
      }

      // ── Step interpolants per X ──
      if(shade_)   { cr += drdx; cg += dgdx; cb += dbdx; ca += dadx; }
      if(texture_) { cs += dsdx; ct += dtdx; cw += dwdx; }
      if(zbuffer_) { cz += dzdx_val; }
    }

    // Step edges
    if(y < ym) { xm += dxmdy; }
    else       { xl += dxldy; }
    xh += dxhdy;

    // Step shade along edge
    if(shade_) {
      sr += drde; sg += dgde; sb += dbde; sa += dade;
    }
    // Step texture along edge
    if(texture_) {
      ts += dsde; tt += dtde; tw += dwde;
    }
    // Step Z along edge
    if(zbuffer_) {
      zval += dzde_val;
    }
  }
}

auto RDP::unshadedTriangle() -> void { renderTriangle(false, false, false); }
auto RDP::unshadedZbufferTriangle() -> void { renderTriangle(false, false, true); }
auto RDP::textureTriangle() -> void { renderTriangle(false, true, false); }
auto RDP::textureZbufferTriangle() -> void { renderTriangle(false, true, true); }
auto RDP::shadedTriangle() -> void { renderTriangle(true, false, false); }
auto RDP::shadedZbufferTriangle() -> void { renderTriangle(true, false, true); }
auto RDP::shadedTextureTriangle() -> void { renderTriangle(true, true, false); }
auto RDP::shadedTextureZbufferTriangle() -> void { renderTriangle(true, true, true); }

// Sync commands
auto RDP::syncLoad() -> void {}
auto RDP::syncPipe() -> void {}
auto RDP::syncTile() -> void {}
auto RDP::syncFull() -> void {
  if(!command.crashed) {
    mi.raise(MI::IRQ::DP);
    command.bufferBusy = 0;
    command.pipeBusy = 0;
  }
  command.startGclk = 0;
}

// State-setting commands (already parsed in dispatch, just store to tiles)
auto RDP::setKeyGB() -> void {}
auto RDP::setKeyR() -> void {}
auto RDP::setConvert() -> void {}
auto RDP::setScissor() -> void {}
auto RDP::setPrimitiveDepth() -> void {}
auto RDP::setOtherModes() -> void {}

auto RDP::setTile() -> void {
  auto& td = tiles[tile.index];
  td.format  = tile.format;
  td.size    = tile.size;
  td.line    = tile.line;
  td.address = tile.address;
  td.palette = tile.palette;
  td.s.clamp = tile.s.clamp; td.s.mirror = tile.s.mirror;
  td.s.mask  = tile.s.mask;  td.s.shift  = tile.s.shift;
  td.t.clamp = tile.t.clamp; td.t.mirror = tile.t.mirror;
  td.t.mask  = tile.t.mask;  td.t.shift  = tile.t.shift;
}

auto RDP::setTileSize() -> void {
  auto& td = tiles[tileSize.index];
  td.sl = tileSize.s.hi;
  td.tl = tileSize.t.hi;
  td.sh = tileSize.s.lo;
  td.th = tileSize.t.lo;
}

// Load texture data from RDRAM into TMEM
auto RDP::loadBlock() -> void {
  auto& td = tiles[load_.block.index];
  u32 tmemAddr = (u32)td.address * 8;
  u32 dramAddr = set.texture.dramAddress;
  u32 count = (u32)load_.block.s.hi - (u32)load_.block.s.lo + 1;  // number of texels
  // size field: 0=4-bit, 1=8-bit, 2=16-bit, 3=32-bit → bits per texel = 4 << size
  u32 bitsPerTexel = 4 << (u32)set.texture.size;

  u32 bytes = (count * bitsPerTexel + 7) / 8;
  if(bytes > 4096) bytes = 4096;

  for(u32 i = 0; i < bytes; i++) {
    tmem[(tmemAddr + i) & 0xfff] = rdram.ram.read<Byte>(dramAddr + i);
  }
}

auto RDP::loadTile() -> void {
  auto& td = tiles[load_.tile.index];
  u32 tmemAddr = (u32)td.address * 8;
  u32 dramAddr = set.texture.dramAddress;
  // size field: 0=4-bit, 1=8-bit, 2=16-bit, 3=32-bit
  u32 bitsPerTexel = 4 << (u32)set.texture.size;
  u32 bytesPerTexel = bitsPerTexel / 8;
  if(bytesPerTexel == 0) bytesPerTexel = 1;  // 4-bit: handle as 1 byte per 2 texels
  u32 texWidth = set.texture.width + 1;

  u32 sl = load_.tile.s.hi >> 2;
  u32 tl = load_.tile.t.hi >> 2;
  u32 sh = load_.tile.s.lo >> 2;
  u32 th = load_.tile.t.lo >> 2;

  u32 line = td.line ? (u32)td.line * 8 : (sh - sl + 1) * bytesPerTexel;

  for(u32 t = tl; t <= th; t++) {
    for(u32 s = sl; s <= sh; s++) {
      u32 srcAddr = dramAddr + (t * texWidth + s) * bytesPerTexel;
      u32 dstAddr = tmemAddr + (t - tl) * line + (s - sl) * bytesPerTexel;
      for(u32 b = 0; b < bytesPerTexel; b++) {
        tmem[(dstAddr + b) & 0xfff] = rdram.ram.read<Byte>(srcAddr + b);
      }
    }
  }
}

auto RDP::loadTLUT() -> void {
  auto& td = tiles[tlut.index];
  u32 tmemAddr = (u32)td.address * 8;
  u32 dramAddr = set.texture.dramAddress;
  u32 count = ((tlut.s.hi >> 2) - (tlut.s.lo >> 2) + 1);

  // TLUT entries are 16-bit, stored in upper half of TMEM (0x800-0xFFF)
  for(u32 i = 0; i < count && i < 256; i++) {
    u16 entry = rdram.ram.read<Half>(dramAddr + i * 2);
    u32 dst = tmemAddr + i * 2;
    // TLUT goes to upper TMEM
    tmem[(0x800 + i * 2) & 0xfff] = entry >> 8;
    tmem[(0x800 + i * 2 + 1) & 0xfff] = entry & 0xff;
  }
}

auto RDP::setFillColor() -> void {}
auto RDP::setFogColor() -> void {}
auto RDP::setBlendColor() -> void {}
auto RDP::setPrimitiveColor() -> void {}
auto RDP::setEnvironmentColor() -> void {}
auto RDP::setCombineMode() -> void {}
auto RDP::setTextureImage() -> void {}
auto RDP::setMaskImage() -> void {}
auto RDP::setColorImage() -> void {}
