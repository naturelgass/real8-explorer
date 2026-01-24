#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

include $(DEVKITARM)/gba_rules

#---------------------------------------------------------------------------------
# Build configuration
#---------------------------------------------------------------------------------
TOOLCHAIN ?= $(DEVKITARM)/bin
CC := $(TOOLCHAIN)/arm-none-eabi-gcc
CXX := $(TOOLCHAIN)/arm-none-eabi-g++
AS := $(TOOLCHAIN)/arm-none-eabi-as
AR := $(TOOLCHAIN)/arm-none-eabi-ar
LD := $(CXX)
OBJCOPY := $(TOOLCHAIN)/arm-none-eabi-objcopy
OBJDUMP := $(TOOLCHAIN)/arm-none-eabi-objdump
STRIP := $(TOOLCHAIN)/arm-none-eabi-strip
GRIT := $(DEVKITPRO)/tools/bin/grit

TARGET   := REAL8_GBA
BUILD    := build
SOURCES  := . ../../core ../../hal ../../../lib/z8lua ../../../lib/lodePNG
INCLUDES := . ../../core ../../hal ../../../lib/z8lua ../../../lib/lodePNG
LIBDIRS  := $(LIBGBA)

CART_PNG  := game.p8.png
CART_BLOB := cart_blob.bin
SPLASH_PNG := splash.png
SPLASH_IMG := splash_img.bin
SPLASH_PAL := splash_pal.bin
CUSTOM_SKIN_PNG := custom_skin.png
CUSTOM_SKIN_IMG := custom_skin_img.bin
CUSTOM_SKIN_PAL := custom_skin_pal.bin

#---------------------------------------------------------------------------------
# Template ROM build (for standalone Pico2GBA patcher)
#
# The template ROM embeds a placeholder cart_blob.bin where CartBlobHeader::comp_size
# is used as a *payload slot capacity*. Pico2GBA can then patch carts into the ROM
# without requiring devkitARM/make at runtime.
#
# Override this if you need a larger slot:
#   make template CART_TEMPLATE_CAPACITY=524288
#---------------------------------------------------------------------------------
CART_TEMPLATE_CAPACITY ?= 262144
TEMPLATE_GBA := $(TARGET)_template.gba

#---------------------------------------------------------------------------------
# Host cart packer
#---------------------------------------------------------------------------------
ifeq ($(OS),Windows_NT)
  EXEEXT := .exe
  WINDRES ?= windres
  CART_PACKER_RC := cart_packer.rc
  CART_PACKER_RES := $(BUILD)/cart_packer_res.o
  CART_PACKER_GUI_RC := cart_packer_gui.rc
  CART_PACKER_GUI_RES := $(BUILD)/cart_packer_gui_res.o
  HOSTGUI_LDFLAGS := -mwindows -lcomdlg32 -lshell32 -lole32

  # Embed the template ROM into pico2gba.exe as a Windows RCDATA resource (default ON)
  EMBED_TEMPLATE ?= 1
  TEMPLATE_RCDATA_ID ?= 201
  TEMPLATE_EMBED_RC := $(BUILD)/template_embed.rc
  TEMPLATE_EMBED_RES := $(BUILD)/template_embed_res.o
  TEMPLATE_GBA_ABS := $(abspath $(TEMPLATE_GBA))
  # windres is native Windows and cannot open MSYS /d/... paths; convert to D:/... when possible
  # TEMPLATE_GBA_RC_PATH is computed in the recipe for $(TEMPLATE_EMBED_RC) to avoid shell quoting issues.
else
  EXEEXT :=
endif

#---------------------------------------------------------------------------------
# Host command helpers
#---------------------------------------------------------------------------------
ifeq ($(REAL8_HOST_CMD),1)
SHELL := cmd.exe
define MKDIR_P
@if not exist "$(1)" mkdir "$(1)"
endef
define RM_RF
@if exist "$(1)" rmdir /s /q "$(1)"
endef
define RM_F
@if exist "$(1)" del /f /q "$(1)"
endef
define MV_F
@if exist "$(1)" move /Y "$(1)" "$(2)" >nul
endef
define CP_F
@copy /Y "$(1)" "$(2)" >nul
endef
export PATH := $(DEVKITPRO)/tools/bin;$(DEVKITARM)/bin;$(PATH)
BIN2S := $(DEVKITPRO)/tools/bin/bin2s
define bin2o
	@"$(BIN2S)" -a 4 -H $(subst .,_,$(<F)).h $(abspath $<) > $(<F).s
	@$(CC) -x assembler-with-cpp $(CPPFLAGS) $(ASFLAGS) -c $(<F).s -o $(<F).o
	$(call RM_F,$(<F).s)
endef
else
define MKDIR_P
@mkdir -p "$(1)"
endef
define RM_RF
@rm -fr "$(1)"
endef
define RM_F
@rm -f "$(1)"
endef
define MV_F
@mv -f "$(1)" "$(2)"
endef
define CP_F
@cp -f "$(1)" "$(2)"
endef
endif

HOSTCC ?= g++
HOSTCXXFLAGS := -O2 -std=gnu++17 -I. -I../../core -I../../hal -I../../../lib/lodePNG
CART_PACKER := $(BUILD)/cart_packer$(EXEEXT)
CART_PACKER_SRCS := cart_packer.cpp ../../core/real8_cart.cpp ../../core/real8_compression.cpp ../../../lib/lodePNG/lodePNG.cpp
CART_PACKER_GUI := $(BUILD)/Pico2GBA$(EXEEXT)
CART_PACKER_GUI_SRCS := cart_packer_gui.cpp ../../core/real8_cart.cpp ../../core/real8_compression.cpp ../../../lib/lodePNG/lodePNG.cpp

# Choose which .rc file to feed into windres for pico2gba.exe
CART_PACKER_GUI_RC_INPUT := $(CART_PACKER_GUI_RC)

# Extra resources to link into pico2gba.exe
CART_PACKER_GUI_EXTRA_RES :=
ifeq ($(OS),Windows_NT)
ifeq ($(EMBED_TEMPLATE),1)
  CART_PACKER_GUI_EXTRA_RES := $(TEMPLATE_EMBED_RES)
endif
endif

#---------------------------------------------------------------------------------
# Toolchain flags
#---------------------------------------------------------------------------------
# Build knobs
OPT  ?= -O2          # try -Os for size-focused builds
WARN ?= -w           # set to empty to see warnings
LTO  ?= 1            # 1 enables -flto

# ARM7TDMI tuning (GBA CPU)
ARCH := -mthumb -mthumb-interwork -mcpu=arm7tdmi -mtune=arm7tdmi

# Base C/C++ flags (without per-directory include paths)
CFLAGS_BASE  ?= -g $(WARN) $(OPT) $(ARCH) -D__GBA__ -DLUA_32BITS
CFLAGS_BASE  += -I$(DEVKITPRO)/libgba/include

# Enable linker dead-stripping
CFLAGS_BASE  += -ffunction-sections -fdata-sections

# Reduce unwind/exception metadata bloat (especially important on GBA)
CFLAGS_BASE  += -fno-unwind-tables -fno-asynchronous-unwind-tables

# Optional debug dot (kept default ON to preserve current behavior)
REAL8_GBA_DEBUG_DOT ?= 1
ifeq ($(REAL8_GBA_DEBUG_DOT),1)
  CFLAGS_BASE += -DREAL8_GBA_DEBUG_DOT
endif

#---------------------------------------------------------------------------------
# Your existing feature toggles
#---------------------------------------------------------------------------------

REAL8_GBA_ENABLE_AUDIO ?= 0
CFLAGS_BASE += -DREAL8_GBA_ENABLE_AUDIO=$(REAL8_GBA_ENABLE_AUDIO)

REAL8_GBA_DEFAULT_SKIN_ON ?= 1
CFLAGS_BASE += -DREAL8_GBA_DEFAULT_SKIN_ON=$(REAL8_GBA_DEFAULT_SKIN_ON)

REAL8_GBA_CUSTOM_SKIN ?= 0
CFLAGS_BASE += -DREAL8_GBA_CUSTOM_SKIN=$(REAL8_GBA_CUSTOM_SKIN)

REAL8_GBA_FAST_LUA ?= 1
CFLAGS_BASE += -DREAL8_GBA_FAST_LUA=$(REAL8_GBA_FAST_LUA)

REAL8_GBA_SKIP_VBLANK ?= 1
CFLAGS_BASE += -DREAL8_GBA_SKIP_VBLANK=$(REAL8_GBA_SKIP_VBLANK)

REAL8_PROFILE_GBA ?= 0
CFLAGS_BASE += -DREAL8_PROFILE_GBA=$(REAL8_PROFILE_GBA)

LUA_GBA_BASELINE_JIT ?= 0
ifeq ($(LUA_GBA_BASELINE_JIT),1)
  CFLAGS_BASE += -DLUA_GBA_BASELINE_JIT
  CFLAGS_BASE += -DLUA_GBA_JIT_MAX_OPS=8192
endif

REAL8_GBA_JIT_IWRAM ?= 1
ifeq ($(REAL8_GBA_JIT_IWRAM),1)
  CFLAGS_BASE += -DLUA_GBA_JIT_IWRAM
endif

#---------------------------------------------------------------------------------

# C++ flags: strip exceptions/RTTI to reduce ROM + unwind tables on ARM7
CXXFLAGS_BASE ?= $(CFLAGS_BASE) -std=gnu++17 \
  -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit

# Assembler flags
ASFLAGS  ?= -g $(ARCH)

# Optional LTO
ifeq ($(LTO),1)
  CFLAGS_BASE   += -flto
  CXXFLAGS_BASE += -flto
  LDFLAGS       += -flto
endif

# Use setjmp/longjmp instead of throw/catch
CFLAGS_BASE += -DLUA_USE_LONGJMP

LIBS ?= -lstdc++ -lgba -lm

# Linker flags:
#  -gc-sections drops unused code/data (needs -ffunction-sections/-fdata-sections)
#  --print-memory-usage is handy to see IWRAM/EWRAM/ROM usage summary
LDFLAGS += -Wl,--gc-sections -Wl,--print-memory-usage

# Keep your existing heap/stack symbols (only meaningful if your linker script uses them)
LDFLAGS += -Wl,--defsym=__heap_size=0x10000 -Wl,--defsym=__stack_size=0x8000
LDFLAGS += -Wl,-Map,REAL8_GBA.map

#---------------------------------------------------------------------------------
# No real need to edit anything past this point unless you need to add rules
#---------------------------------------------------------------------------------

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT := $(CURDIR)/$(TARGET)
export TOPDIR := $(CURDIR)

export VPATH := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)

CFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))

EXCLUDE_CPPFILES := real8_shell.cpp real8_tools.cpp real8_cart.cpp real8_compression.cpp cart_packer.cpp cart_packer_gui.cpp
CPPFILES := $(filter-out $(EXCLUDE_CPPFILES),$(CPPFILES))

BINFILES := $(CART_BLOB) $(SPLASH_IMG) $(SPLASH_PAL)
ifeq ($(REAL8_GBA_CUSTOM_SKIN),1)
  BINFILES += $(CUSTOM_SKIN_IMG) $(CUSTOM_SKIN_PAL)
endif

export OFILES_SOURCES := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES_BIN := $(addsuffix .o,$(BINFILES))
export OFILES := $(OFILES_BIN) $(OFILES_SOURCES)

export HFILES := $(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
    $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
    -I$(CURDIR)/$(BUILD)

export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

# Apply include paths to the exported compiler flags used in the build dir.
export CFLAGS   := $(CFLAGS_BASE) $(INCLUDE)
export CXXFLAGS := $(CXXFLAGS_BASE) $(INCLUDE)

.PHONY: all clean rom tools template

rom: $(BUILD) $(BINFILES)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

# Build a ROM template (REAL8_GBA_template.gba) with a placeholder cart blob slot.
template: $(TEMPLATE_GBA)

$(TEMPLATE_GBA): $(CART_PACKER)
	$(call RM_F,$(CART_BLOB))
	@$(MAKE) --no-print-directory REAL8_TEMPLATE=1 rom
	$(call CP_F,$(TARGET).gba,$@)
	@echo Wrote template: $@

ifeq ($(OS),Windows_NT)
ifeq ($(EMBED_TEMPLATE),1)
tools: $(CART_PACKER) $(TEMPLATE_GBA) $(CART_PACKER_GUI)
else
tools: $(CART_PACKER) $(CART_PACKER_GUI)
endif
else
tools: $(CART_PACKER)
endif

all: tools
ifneq ($(wildcard $(CART_PNG)),)
all: rom
endif

$(BUILD):
	$(call MKDIR_P,$@)

$(CART_PACKER): $(CART_PACKER_SRCS) $(CART_PACKER_RES)
	$(call MKDIR_P,$(BUILD))
	$(HOSTCC) $(HOSTCXXFLAGS) -o $@ $^

$(CART_PACKER_GUI): $(CART_PACKER_GUI_SRCS) $(CART_PACKER_GUI_RES) $(CART_PACKER_GUI_EXTRA_RES)
	$(call MKDIR_P,$(BUILD))
	$(HOSTCC) $(HOSTCXXFLAGS) -o $@ $^ $(HOSTGUI_LDFLAGS)

ifeq ($(OS),Windows_NT)
$(CART_PACKER_RES): $(CART_PACKER_RC) icon.ico
	$(call MKDIR_P,$(BUILD))
	$(WINDRES) -I. -i $< -O coff -o $@
$(TEMPLATE_EMBED_RC): $(TEMPLATE_GBA)
	$(call MKDIR_P,$(BUILD))
	@TEMPLATE_RC_PATH="$$(cygpath -m "$(TEMPLATE_GBA_ABS)" 2>/dev/null || echo "$(TEMPLATE_GBA_ABS)")"; \
	  TEMPLATE_RC_PATH="$${TEMPLATE_RC_PATH//\\//}"; \
	  printf "%s\n" "$(TEMPLATE_RCDATA_ID) RCDATA \"$$TEMPLATE_RC_PATH\"" > $@

$(TEMPLATE_EMBED_RES): $(TEMPLATE_EMBED_RC)
	$(call MKDIR_P,$(BUILD))
	$(WINDRES) -I. -i $< -O coff -o $@


$(CART_PACKER_GUI_RES): $(CART_PACKER_GUI_RC_INPUT) icon.ico REAL8-banner.png
	$(call MKDIR_P,$(BUILD))
	$(WINDRES) -I. -i $< -O coff -o $@
endif

ifeq ($(REAL8_TEMPLATE),1)
$(CART_BLOB): $(CART_PACKER)
	@$(CART_PACKER) --template $(CART_BLOB) $(CART_TEMPLATE_CAPACITY)
else
$(CART_BLOB): $(CART_PNG) $(CART_PACKER)
	@$(CART_PACKER) $(CART_PNG) $(CART_BLOB)
endif

$(SPLASH_IMG) $(SPLASH_PAL): $(SPLASH_PNG)
	@$(GRIT) $(SPLASH_PNG) -gb -gB8 -m! -p -ftb -o splash
	$(call MV_F,splash.img.bin,$(SPLASH_IMG))
	$(call MV_F,splash.pal.bin,$(SPLASH_PAL))
	$(call RM_F,splash.h)

ifeq ($(REAL8_GBA_CUSTOM_SKIN),1)
$(CUSTOM_SKIN_IMG) $(CUSTOM_SKIN_PAL): $(CUSTOM_SKIN_PNG)
	@$(GRIT) $(CUSTOM_SKIN_PNG) -gb -gB8 -m! -p -ftb -o custom_skin
	$(call MV_F,custom_skin.img.bin,$(CUSTOM_SKIN_IMG))
	$(call MV_F,custom_skin.pal.bin,$(CUSTOM_SKIN_PAL))
	$(call RM_F,custom_skin.h)
endif

clean:
	@echo clean ...
	$(call RM_RF,$(BUILD))
	$(call RM_F,$(TARGET).gba)
	$(call RM_F,$(TARGET).elf)
	$(call RM_F,$(CART_BLOB))
	$(call RM_F,$(SPLASH_IMG))
	$(call RM_F,$(SPLASH_PAL))
	$(call RM_F,$(CUSTOM_SKIN_IMG))
	$(call RM_F,$(CUSTOM_SKIN_PAL))
	$(call RM_F,splash.img.bin)
	$(call RM_F,splash.pal.bin)
	$(call RM_F,splash.h)
	$(call RM_F,custom_skin.img.bin)
	$(call RM_F,custom_skin.pal.bin)
	$(call RM_F,custom_skin.h)

else

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
$(OUTPUT).gba: $(OUTPUT).elf

$(OFILES_SOURCES): $(HFILES)

$(OUTPUT).elf: $(OFILES)

#---------------------------------------------------------------------------------
# you need a rule like this for each extension you use as binary data
#---------------------------------------------------------------------------------
%.bin.o %_bin.h : %.bin
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPSDIR)/*.d

#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------
