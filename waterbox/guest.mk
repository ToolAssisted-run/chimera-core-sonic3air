# guest.mk - core.wbx: the same SDL, codec, engine, game and core sources as
# native.mk, built with miniBox's musl/libstdc++ guest toolchain (the engine
# is C++) and linked at the guest base. Objects land in build/guest; core.wbx
# is checked by miniBox's check-wbx.sh before it counts as built.
#
# Needs miniBox built WITH the C++ guest toolchain:
#   meson setup <miniBox>/build/meson-cpp <miniBox> -Dguest_cpp=true && ninja -C <miniBox>/build/meson-cpp
#
# Usage: make -f guest.mk -j32 [MB=<miniBox checkout>]

.DEFAULT_GOAL := all
include sources.mk

B      := $(ROOT)/build/guest
MBUILD := $(MB)/build/meson-cpp
SR     := $(MBUILD)/guest-sysroot
GCCVER := $(shell gcc -dumpfullversion)
# the system's compilers over the guest sysroot, through its specs (musl's
# headers and start files, -mno-red-zone), as the DOSBox-X core's cross file
CC     := gcc -specs $(SR)/lib/musl-gcc.specs
CXX    := g++ -specs $(SR)/lib/musl-gcc.specs

# The guest is built for the CPU this runs on (a package is built natively,
# never cross-built), with miniBox's own flags for that CPU (its meson.build,
# waterbox_cpu_flags); x86_64 keeps the flags it always had.
ARCH := $(shell uname -m)
ifeq ($(ARCH),aarch64)
CPUFLAGS := -mbranch-protection=none -mno-outline-atomics -ffp-contract=off -fsigned-char -ffixed-x18
else
CPUFLAGS := -mcmodel=large -mno-red-zone -fcf-protection=none
endif
WBFLAGS := -fvisibility=hidden $(CPUFLAGS) -mstack-protector-guard=global \
	-fno-stack-protector -fno-pic -fno-pie -DNDEBUG -DCHIMERA_GUEST
MBINCS := -I$(MB)/extern/emulibc -I$(MB)/source/guest/include -I$(MB)/extern/jsmn
CXXINCS := -I$(SR)/include/c++/$(GCCVER) -I$(SR)/include/c++/$(GCCVER)/$(ARCH)-linux-musl

SDL_CFLAGS := $(WBFLAGS) $(SDL_CFLAGS_COMMON) $(SDL_WARN)
CODEC_CFLAGS := $(WBFLAGS) $(CODEC_CFLAGS_COMMON)
S3_CXXFLAGS := $(WBFLAGS) $(S3_CXXFLAGS_COMMON) $(CXXINCS) -fexceptions -w
CORE_CFLAGS := $(WBFLAGS) $(CORE_CFLAGS_COMMON) $(MBINCS) -I. -Wall
CORE_CXXFLAGS := $(WBFLAGS) $(CORE_CXXFLAGS_COMMON) $(CXXINCS) $(MBINCS) -I. -fexceptions -Wall -Wno-unused-function

$(call flags_stamp,$(B),$(CC) | $(CXX) | $(SDL_CFLAGS) | $(CODEC_CFLAGS) | $(S3_CXXFLAGS) | $(CORE_CFLAGS) | $(CORE_CXXFLAGS))

SDL_OBJS := $(patsubst $(SDL)/%.c,$(B)/sdl/%.o,$(SDL_SRCS))
CODEC_OBJS := $(patsubst $(FW)/%.c,$(B)/codec/%.o,$(CODEC_SRCS))
S3_OBJS := $(patsubst $(S3)/%.cpp,$(B)/s3/%.o,$(UPSTREAM_SRCS))
CORE_OBJS := $(addprefix $(B)/core/,$(addsuffix .o,$(CORE_C_NAMES) $(CORE_CXX_NAMES))) $(B)/core/data.o

all: $(B)/core.wbx

$(SR)/lib/libstdc++.a:
	@echo "miniBox's C++ guest toolchain is missing: $(SR)" >&2
	@echo "build it: meson setup $(MBUILD) $(MB) -Dguest_cpp=true && ninja -C $(MBUILD)" >&2
	@false

$(B)/sdl/%.o: $(SDL)/%.c $(INC_DIR)/SDL2/.stamp $(B)/flags | $(SR)/lib/libstdc++.a
	@mkdir -p $(dir $@)
	$(CC) $(SDL_CFLAGS) -c -o $@ $<

$(B)/codec/%.o: $(FW)/%.c $(B)/flags | $(SR)/lib/libstdc++.a
	@mkdir -p $(dir $@)
	$(CC) $(CODEC_CFLAGS) -c -o $@ $<

$(B)/s3/%.o: $(S3)/%.cpp $(PATCH_STAMP) $(INC_DIR)/SDL2/.stamp $(B)/flags | $(SR)/lib/libstdc++.a
	@mkdir -p $(dir $@)
	$(CXX) $(S3_CXXFLAGS) -c -o $@ $<

$(B)/core/%.o: %.c $(CORE_HDRS) $(PATCH_STAMP) $(INC_DIR)/SDL2/.stamp $(B)/flags | $(SR)/lib/libstdc++.a
	@mkdir -p $(dir $@)
	$(CC) $(CORE_CFLAGS) -c -o $@ $<

$(B)/core/%.o: %.cpp $(CORE_HDRS) $(PATCH_STAMP) $(INC_DIR)/SDL2/.stamp $(B)/flags | $(SR)/lib/libstdc++.a
	@mkdir -p $(dir $@)
	$(CXX) $(CORE_CXXFLAGS) -c -o $@ $<

$(DATA_BLOB): data-pack.py $(PATCH_STAMP)
	@mkdir -p $(dir $@)
	python3 data-pack.py $(S3) $@

$(B)/core/data.o: data.S $(DATA_BLOB) | $(SR)/lib/libstdc++.a
	@mkdir -p $(dir $@)
	$(CC) $(WBFLAGS) -c -DS3AIR_DATA_BLOB='"$(abspath $(DATA_BLOB))"' -o $@ data.S

# the guest kit's link recipe (the DOSBox-X core's): the large code model's
# --no-relax, the weak pthread pulls libgcc_eh needs, cxxglue for the unwinder
$(B)/core.wbx: $(CORE_OBJS) $(S3_OBJS) $(CODEC_OBJS) $(SDL_OBJS)
	$(CXX) -static -no-pie -Wl,--eh-frame-hdr -Wl,-O2 -Wl,--no-relax -Wl,--strip-debug -Wl,-z,stack-size=8388608 \
		-T $(MB)/source/guest/linkscript.T \
		-Wl,-u,pthread_once -Wl,-u,pthread_cond_wait -Wl,-u,pthread_cond_broadcast -Wl,-u,pthread_key_create \
		-o $@.tmp $^ $(MBUILD)/source/guest/cxxglue.c.o $(MBUILD)/source/guest/emulibc.c.o $(WRAP_FLAGS) \
		-L$(SR)/lib -lstdc++ -lm -lgcc -lgcc_eh -lc
	# the guest has thread-local variables (the C++ library's): on aarch64 its
	# thread-pointer reads are pointed at the machine's slot, and on x86-64
	# they are listed for an Intel Mac to patch as it loads the image
	# (miniBox's wbx-tp-rewrite.py and MACHINE-SPEC)
	python3 $(MB)/source/guest/wbx-tp-rewrite.py $@.tmp
	sh $(MB)/source/guest/check-wbx.sh $@.tmp
	mv $@.tmp $@

clean:
	rm -rf $(B)

.PHONY: all clean
