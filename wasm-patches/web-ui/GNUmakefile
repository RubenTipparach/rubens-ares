name := ares
build := optimized
threaded := true
openmp := false
vulkan := true
local := true
lto := true
flags += -I. -I.. -I../ares -I../thirdparty -DMIA_LIBRARY -pthread

# WASM SIMD: lets emscripten translate SSE4.1 intrinsics to WASM SIMD,
# enabling the SIMD path for the RSP VU (huge perf win for 3D games).
flags += -msimd128 -msse4.2 -DARCHITECTURE_SUPPORTS_SSE4_1=1

# angrylion RDP integration
angrylion.src := ../../angrylion-rdp-plus/src/core
flags += -DUSE_ANGRYLION -I$(angrylion.src) -I../angrylion

nall.path := ../nall
include $(nall.path)/GNUmakefile

libco.path := ../libco
include $(libco.path)/GNUmakefile

thirdparty.path := ../thirdparty
sljit.path := $(thirdparty.path)/sljit/sljit_src
libchdr.path := $(thirdparty.path)/libchdr
tzxfile.path := $(thirdparty.path)/TZXFile
include $(thirdparty.path)/GNUmakefile

ruby.path := ../ruby
include $(ruby.path)/GNUmakefile

profile := performance
cores := n64

ares.path := ../ares
include $(ares.path)/GNUmakefile

mia.path := ../mia

mia.objects := mia mia-resource
mia.objects := $(mia.objects:%=$(object.path)/%.o)

$(object.path)/mia.o: $(mia.path)/mia.cpp
$(object.path)/mia-resource.o: $(mia.path)/resource/resource.cpp

# Web-UI entry point (replaces desktop-ui for WASM builds)
web-ui.objects := $(object.path)/web-ui.o
$(object.path)/web-ui.o: main.cpp
	$(info Compiling web-ui/main.cpp ...)
	@$(compiler) $(flags.cpp) $(flags) $(flags.deps) -c $< $(call obj,$@)

# Filter out nall-main.o (provides its own main()) since web-ui has its own entry point
nall.objects.filtered := $(filter-out $(object.path)/nall-main.o,$(nall.objects))

# angrylion RDP objects (unity build: n64video.c #includes rdp.c and vi.c)
angrylion.objects := $(object.path)/alp-n64video.o
angrylion.objects += $(object.path)/alp-bridge.o $(object.path)/alp-parallel.o $(object.path)/alp-msg.o
angrylion.flags := -I$(angrylion.src) -I../angrylion -pthread

$(object.path)/alp-n64video.o: $(angrylion.src)/n64video.c
	$(info Compiling angrylion n64video.c ...)
	@$(compiler) -std=c11 $(angrylion.flags) $(flags.deps) -c $< $(call obj,$@)

$(object.path)/alp-bridge.o: ../angrylion/angrylion_bridge.c
	$(info Compiling angrylion bridge ...)
	@$(compiler) -std=c11 $(angrylion.flags) $(flags.deps) -c $< $(call obj,$@)

$(object.path)/alp-parallel.o: ../angrylion/parallel_stub.c
	$(info Compiling angrylion parallel stub ...)
	@$(compiler) -std=c11 $(angrylion.flags) $(flags.deps) -c $< $(call obj,$@)

$(object.path)/alp-msg.o: ../angrylion/msg_stub.c
	$(info Compiling angrylion msg stub ...)
	@$(compiler) -std=c11 $(angrylion.flags) $(flags.deps) -c $< $(call obj,$@)

all.objects := $(libco.objects) $(sljit.objects) $(libchdr.objects) $(tzxfile.objects) $(nall.objects.filtered) $(ruby.objects) $(ares.objects) $(mia.objects) $(web-ui.objects) $(angrylion.objects)
all.options := $(libco.options) $(sljit.options) $(libchdr.options) $(tzxfile.options) $(nall.options) $(ruby.options) $(ares.options) $(mia.options) $(options)

ifeq ($(wasm32),true)
  # Emscripten link-time options
  all.options += -sTOTAL_MEMORY=536870912
  all.options += --use-port=emdawnwebgpu
  all.options += -sALLOW_MEMORY_GROWTH=1
  all.options += -sEXPORTED_RUNTIME_METHODS=ccall,cwrap,FS -sFORCE_FILESYSTEM=1
  all.options += -sEXPORTED_FUNCTIONS=_main,_resetEmulator,_loadROM,_getBuildId,_malloc,_free
  all.options += --preload-file pif.ntsc.rom
  all.options += --preload-file pif.pal.rom
  # pthreads for angrylion parallel rendering
  all.options += -pthread -sPTHREAD_POOL_SIZE=4
  all.options += -sALLOW_BLOCKING_ON_MAIN_THREAD=1
endif

$(all.objects): | $(object.path)

all: $(all.objects) | $(output.path)
	$(info Linking $(output.path)/$(name)$(extension) ...)
	+@$(compiler) $(call exe,$(output.path)/$(name)$(extension)) $(all.objects) $(all.options)
	$(call copy,../LICENSE,$(output.path)/LICENSE.txt)
ifeq ($(platform),macos)
# Apply workaround for buggy linker in Xcode < 11.4.1
	@$(compiler) -o $(output.path)/macos-fix-jit macos-fix-jit.cpp
	$(output.path)/macos-fix-jit $(output.path)/$(name)
	rm -f $(output.path)/macos-fix-jit
	rm -rf $(output.path)/$(name).app
	install_name_tool -add_rpath @executable_path/../Frameworks $(output.path)/$(name)
	mkdir -p $(output.path)/$(name).app/Contents/MacOS/
	mkdir -p $(output.path)/$(name).app/Contents/Resources/
	mkdir -p $(output.path)/$(name).app/Contents/Frameworks/
ifneq ($(ares.dylibs),)
	cp $(ares.dylibs) $(output.path)/$(name).app/Contents/Frameworks/
endif
	mv $(output.path)/$(name) $(output.path)/$(name).app/Contents/MacOS/$(name)
	cp resource/$(name).plist $(output.path)/$(name).app/Contents/Info.plist
	cp -R $(ares.path)/Shaders $(output.path)/$(name).app/Contents/Resources/
	cp -R $(mia.path)/Database $(output.path)/$(name).app/Contents/Resources/	
	sips -s format icns resource/$(name).png --out $(output.path)/$(name).app/Contents/Resources/$(name).icns
	codesign --force --deep --options runtime --entitlements resource/$(name).selfsigned.entitlements --sign - $(output.path)/$(name).app
else ifeq ($(platform),windows)
	$(call mkdir,$(output.path)/Shaders/)
	$(call mkdir,$(output.path)/Database/)
	$(call rcopy,$(ares.path)/Shaders/*,$(output.path)/Shaders/)
	$(call rcopy,$(mia.path)/Database/*,$(output.path)/Database/)
endif

verbose: nall.verbose ruby.verbose hiro.verbose all;

clean:
ifeq ($(platform),macos)
	rm -rf $(output.path)/$(name).app
endif
	$(call rdelete,$(object.path))
	$(call rdelete,$(output.path))

install: all
ifeq ($(platform),windows)
	$(call mkdir,$(prefix)/$(name)/)
else ifeq ($(shell id -un),root)
	$(error "make install should not be run as root")
else ifeq ($(platform),macos)
	mkdir -p ~/Library/Application\ Support/$(name)/
	cp -R $(output.path)/$(name).app /Applications/$(name).app
else ifneq ($(filter $(platform),linux bsd),)
	mkdir -p $(prefix)/bin/
	mkdir -p $(prefix)/share/applications/
	mkdir -p $(prefix)/share/icons/hicolor/256x256/apps/
	mkdir -p $(prefix)/share/$(name)/Shaders/
	mkdir -p $(prefix)/share/$(name)/Database/
	cp $(output.path)/$(name) $(prefix)/bin/$(name)
	cp -R $(ares.path)/Shaders/* $(prefix)/share/$(name)/Shaders/
	cp -R $(mia.path)/Database/* $(prefix)/share/$(name)/Database/
	cp resource/$(name).desktop $(prefix)/share/applications/$(name).desktop
	cp resource/$(name).png $(prefix)/share/icons/hicolor/256x256/apps/$(name).png
endif

uninstall:
ifeq ($(platform),windows)
else ifeq ($(shell id -un),root)
	$(error "make uninstall should not be run as root")
else ifeq ($(platform),macos)
	rm -rf /Applications/$(name).app
else ifneq ($(filter $(platform),linux bsd),)
	rm -f $(prefix)/bin/$(name)
	rm -f $(prefix)/share/applications/$(name).desktop
	rm -f $(prefix)/share/icons/hicolor/256x256/apps/$(name).png
endif

-include $(object.path)/*.d
