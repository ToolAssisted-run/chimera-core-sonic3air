# native.mk - the native reference: the same SDL, codec, engine, game and core
# sources as guest.mk, built for the host, plus the two harnesses (run-native
# drives the exports directly; run-wbx drives core.wbx through the miniBox
# host exactly as the frontend does). Objects land in build/native.
#
# Usage: make -f native.mk -j32 [MB=<miniBox checkout>]

.DEFAULT_GOAL := all
include sources.mk

B := $(ROOT)/build/native
MBINCS := -Inative-shim -I$(MB)/source/guest/include -I$(MB)/extern/jsmn

SDL_CFLAGS := $(SDL_CFLAGS_COMMON) $(SDL_WARN)
CODEC_CFLAGS := $(CODEC_CFLAGS_COMMON)
S3_CXXFLAGS := $(S3_CXXFLAGS_COMMON) -w
CORE_CFLAGS := $(CORE_CFLAGS_COMMON) $(MBINCS) -I. -Wall -DS3AIR_NATIVE
CORE_CXXFLAGS := $(CORE_CXXFLAGS_COMMON) $(MBINCS) -I. -Wall -Wno-unused-function -DS3AIR_NATIVE

$(call flags_stamp,$(B),$(SDL_CFLAGS) | $(CODEC_CFLAGS) | $(S3_CXXFLAGS) | $(CORE_CFLAGS) | $(CORE_CXXFLAGS))

SDL_OBJS := $(patsubst $(SDL)/%.c,$(B)/sdl/%.o,$(SDL_SRCS))
CODEC_OBJS := $(patsubst $(FW)/%.c,$(B)/codec/%.o,$(CODEC_SRCS))
S3_OBJS := $(patsubst $(S3)/%.cpp,$(B)/s3/%.o,$(UPSTREAM_SRCS))
CORE_OBJS := $(addprefix $(B)/core/,$(addsuffix .o,$(CORE_C_NAMES) $(CORE_CXX_NAMES))) $(B)/core/data.o

all: $(B)/run-native $(B)/run-wbx

$(B)/sdl/%.o: $(SDL)/%.c $(INC_DIR)/SDL2/.stamp $(B)/flags
	@mkdir -p $(dir $@)
	gcc $(SDL_CFLAGS) -c -o $@ $<

$(B)/codec/%.o: $(FW)/%.c $(B)/flags
	@mkdir -p $(dir $@)
	gcc $(CODEC_CFLAGS) -c -o $@ $<

$(B)/s3/%.o: $(S3)/%.cpp $(PATCH_STAMP) $(INC_DIR)/SDL2/.stamp $(B)/flags
	@mkdir -p $(dir $@)
	g++ $(S3_CXXFLAGS) -c -o $@ $<

$(B)/core/%.o: %.c $(CORE_HDRS) $(PATCH_STAMP) $(INC_DIR)/SDL2/.stamp $(B)/flags
	@mkdir -p $(dir $@)
	gcc $(CORE_CFLAGS) -c -o $@ $<

$(B)/core/%.o: %.cpp $(CORE_HDRS) $(PATCH_STAMP) $(INC_DIR)/SDL2/.stamp $(B)/flags
	@mkdir -p $(dir $@)
	g++ $(CORE_CXXFLAGS) -c -o $@ $<

$(DATA_BLOB): data-pack.py $(PATCH_STAMP)
	@mkdir -p $(dir $@)
	python3 data-pack.py $(S3) $@

$(B)/core/data.o: data.S $(DATA_BLOB)
	@mkdir -p $(dir $@)
	gcc -c -DS3AIR_DATA_BLOB='"$(abspath $(DATA_BLOB))"' -o $@ data.S

$(B)/core/run-native.o: run-native.c gate-harness.h s3air-driver.h $(B)/flags
	@mkdir -p $(dir $@)
	gcc -O2 -Wall -DGATE_NATIVE -I. -c -o $@ $<

$(B)/run-native: $(CORE_OBJS) $(S3_OBJS) $(CODEC_OBJS) $(SDL_OBJS) $(B)/core/run-native.o
	g++ -o $@ $^ $(WRAP_FLAGS) -lm

# run-wbx links the miniBox host library
MBHOST := $(MB)/build/meson-linux/source/host
$(B)/run-wbx: run-wbx.c gate-harness.h s3air-driver.h $(B)/flags
	gcc -O2 -Wall -I. -I$(MB)/source/host -o $@ run-wbx.c $(MBHOST)/libminiboxhost.so -Wl,-rpath,$(MBHOST)

clean:
	rm -rf $(B)

.PHONY: all clean
