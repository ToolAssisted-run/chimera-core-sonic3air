# sources.mk - what native.mk and guest.mk both build: the same files with the
# same defines, so the native reference and the sandboxed core are the same
# program. Included, not run.
#
# Everything comes from the submodule extern/sonic3air (Sonic 3 A.I.R., with
# the copies of SDL 2, zlib, libogg and libvorbis it carries in framework/),
# with patches/ applied, and from this folder.

ROOT   := ..
S3     := $(ROOT)/extern/sonic3air
FW     := $(S3)/framework/external
SDL    := $(FW)/sdl/SDL2
ZLIB   := $(FW)/zlib/zlib
OGG    := $(FW)/ogg-vorbis/libogg
VORBIS := $(FW)/ogg-vorbis/libvorbis
RMX    := $(S3)/librmx/source
LEMON  := $(S3)/Oxygen/lemonscript/source
OXY    := $(S3)/Oxygen/oxygenengine/source
GAME   := $(S3)/Oxygen/sonic3air/source
MB     ?= $(MINIBOX_DIR)
ifeq ($(MB),)
$(error miniBox not given: make MB=<miniBox checkout>, or set MINIBOX_DIR)
endif
INC_DIR := $(ROOT)/build/include

# ---- SDL 2 (upstream's copy, 2.32), built the way its own Makefile.minimal
# builds it: the platform-neutral configuration (include/SDL_config_minimal.h -
# the dummy video and audio drivers, no threads, no timers, no joysticks, no
# dynamic loading) and the portable C of every subsystem the engine touches.
# On top of that, as the SDLPoP and RSDK cores build it:
#   SDL_CPUINFO_DISABLED  SDL asks the CPU nothing, so every blitter and
#                         audio converter is the C one on every host
#   DYNAPI_NEEDS_DLOPEN   (without HAVE_DLOPEN) turns off SDL's dynamic API
#   HAVE_MALLOC, HAVE_STDIO_H
#                         SDL allocates and opens files with the C library
SDL_DIRS := src src/atomic src/audio src/audio/dummy src/cpuinfo src/events src/file \
	src/haptic src/haptic/dummy src/hidapi src/joystick src/joystick/dummy src/loadso/dummy \
	src/power src/filesystem/dummy src/locale src/locale/dummy src/misc src/misc/dummy \
	src/render src/render/software src/sensor src/sensor/dummy src/stdlib src/libm \
	src/thread src/thread/generic src/timer src/timer/dummy src/video src/video/yuv2rgb \
	src/video/dummy
SDL_WARN := -w -Wno-error=incompatible-pointer-types
SDL_SRCS := $(sort $(foreach d,$(SDL_DIRS),$(wildcard $(SDL)/$(d)/*.c)))
SDL_DEFS := -DSDL_CPUINFO_DISABLED=1 -DDYNAPI_NEEDS_DLOPEN=1 -DHAVE_MALLOC=1 -DHAVE_STDLIB_H=1 -DHAVE_STDIO_H=1 -DHAVE_STRING_H=1

# the transcendental functions, which detmath.c answers in both builds (the
# link wraps every object): not builtins, so the compiler neither folds nor
# fuses them
MATH_FUNCS := sin cos tan acos atan atan2 exp log pow exp2 log2 log10 sinf cosf tanf acosf asinf atanf atan2f \
	expf logf powf exp2f log2f log10f
NO_BUILTIN_MATH := $(foreach f,$(MATH_FUNCS) sincos sincosf,-fno-builtin-$(f))

# ---- zlib and minizip (upstream's copies): the engine's packages, the PNG
# images, the mods' zips. libogg and libvorbis (upstream's copies): the
# engine's Ogg sounds and the remastered soundtrack.
ZLIB_NAMES := adler32 compress crc32 deflate infback inffast inflate inftrees trees uncompr zutil
ZLIB_SRCS := $(addprefix $(ZLIB)/,$(addsuffix .c,$(ZLIB_NAMES))) \
	$(ZLIB)/contrib/minizip/ioapi.c $(ZLIB)/contrib/minizip/unzip.c $(ZLIB)/contrib/minizip/zip.c
OGG_SRCS := $(OGG)/src/bitwise.c $(OGG)/src/framing.c
VORBIS_SRCS := $(filter-out %/barkmel.c %/psytune.c %/tone.c,$(wildcard $(VORBIS)/lib/*.c))
CODEC_SRCS := $(ZLIB_SRCS) $(OGG_SRCS) $(VORBIS_SRCS)
CODEC_INCS := -I$(ZLIB) -I$(ZLIB)/contrib/minizip -I$(OGG)/include -I$(VORBIS)/include -I$(VORBIS)/lib
CODEC_CFLAGS_COMMON := -std=gnu11 -O2 -w $(CODEC_INCS) $(NO_BUILTIN_MATH) -DNOCRYPT -DNOUNCRYPT

# ---- the engine and the game, as upstream's CMakeLists.txt builds them for
# Linux, but for:
#   - no OpenGL (RMX_NO_OPENGL, patches/): the software renderer only
#   - no Dear ImGui (USE_IMGUI unset), no Discord (USE_DISCORD unset), no
#     libcurl (patches/: the downloader is not built in)
#   - rmxbase's FileIO asks the core's memfs.cpp (patches/): every file is in the
#     machine's memory
#   - oxygen_netcore's sockets are the core's (no-network.cpp): none opens
RMX_SRCS := $(shell find $(RMX)/rmxbase -name '*.cpp') \
	$(shell find $(RMX)/rmxmedia -name '*.cpp') \
	$(wildcard $(RMX)/rmxext_oggvorbis/*.cpp)
LEMON_SRCS := $(shell find $(LEMON)/lemon -name '*.cpp')
NETCORE_SRCS := $(filter-out %/network/Sockets.cpp,$(shell find $(OXY)/oxygen_netcore -name '*.cpp'))
OXY_SRCS := $(shell find $(OXY)/oxygen -name '*.cpp')
GAME_SRCS := $(filter-out %/main.cpp,$(shell find $(GAME)/sonic3air -name '*.cpp'))
UPSTREAM_SRCS := $(RMX_SRCS) $(LEMON_SRCS) $(NETCORE_SRCS) $(OXY_SRCS) $(GAME_SRCS)

S3_DEFS := -DRMX_NO_OPENGL -DCHIMERA -DENDUSER
S3_INCS := -I$(INC_DIR) -I$(SDL)/include $(CODEC_INCS) -I$(RMX) -I$(LEMON) -I$(OXY) -I$(GAME) -I$(GAME)/external
S3_CXXFLAGS_COMMON := -std=gnu++17 -O2 $(NO_BUILTIN_MATH) $(S3_DEFS) $(S3_INCS) -Wno-psabi

# ---- the core
CORE_C_NAMES := seams detmath wbx-entry
CORE_CXX_NAMES := s3air-driver s3air-files memfs no-network game-state
CORE_HDRS := s3air-driver.h s3air-files.h memfs.h
CORE_CFLAGS_COMMON := -std=gnu11 -O2 -I$(INC_DIR) -I$(SDL)/include
CORE_CXXFLAGS_COMMON := $(S3_CXXFLAGS_COMMON)

# the data the core carries (data-pack.py): upstream's data folders and its
# scripts, as one blob linked into the core
DATA_BLOB := $(ROOT)/build/data/s3air-data.bin

# ---- the calls the core answers itself
WRAPS := fopen fclose time localtime rand srand SDL_GetPerformanceCounter SDL_GetPerformanceFrequency \
	SDL_GetTicks SDL_GetTicks64 SDL_Delay SDL_Init SDL_InitSubSystem SDL_OpenAudioDevice SDL_OpenAudio \
	SDL_PauseAudioDevice SDL_PauseAudio SDL_CloseAudioDevice SDL_CloseAudio SDL_LockAudio SDL_UnlockAudio \
	SDL_LockAudioDevice SDL_UnlockAudioDevice SDL_CreateThread SDL_CreateThreadWithStackSize \
	$(MATH_FUNCS)
WRAP_FLAGS := $(foreach w,$(WRAPS),-Wl,--wrap=$(w))

# the patch series goes onto the submodule before anything of it builds
PATCH_STAMP := $(ROOT)/build/patches.stamp
$(PATCH_STAMP): $(wildcard $(ROOT)/patches/*.patch) apply-patches.sh
	sh apply-patches.sh
	@mkdir -p $(dir $@)
	@touch $@

# The engine includes <SDL2/SDL.h>: a directory (INC_DIR) that has SDL's
# headers under SDL2/
SDL_HDRS := $(wildcard $(SDL)/include/*.h)
$(INC_DIR)/SDL2/.stamp: $(SDL_HDRS)
	@mkdir -p $(INC_DIR)/SDL2
	cp $(SDL_HDRS) $(INC_DIR)/SDL2/
	@touch $@

SDL_CFLAGS_COMMON := -O2 -fno-strict-aliasing $(SDL_DEFS) -I$(SDL)/include

# Every object depends on the flags it was built with: a change to them
# rebuilds it.
define flags_stamp
$(shell mkdir -p $(1); printf '%s\n' '$(2)' | cmp -s - $(1)/flags || printf '%s\n' '$(2)' > $(1)/flags)
endef
