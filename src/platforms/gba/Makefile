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
endif

HOSTCC ?= g++
HOSTCXXFLAGS := -O2 -std=gnu++17 -I. -I../../core -I../../hal -I../../../lib/lodePNG
CART_PACKER := $(BUILD)/cart_packer$(EXEEXT)
CART_PACKER_SRCS := cart_packer.cpp ../../core/real8_cart.cpp ../../core/real8_compression.cpp ../../../lib/lodePNG/lodePNG.cpp
CART_PACKER_GUI := $(BUILD)/pico2gba$(EXEEXT)
CART_PACKER_GUI_SRCS := cart_packer_gui.cpp ../../core/real8_cart.cpp ../../core/real8_compression.cpp ../../../lib/lodePNG/lodePNG.cpp

#---------------------------------------------------------------------------------
# Toolchain flags
#---------------------------------------------------------------------------------
ARCH := -mthumb -mthumb-interwork

CFLAGS  ?= -g -w -O2 $(ARCH) -D__GBA__ -DLUA_32BITS
CFLAGS  += -I$(DEVKITPRO)/libgba/include -DREAL8_GBA_DEBUG_DOT
REAL8_GBA_FB_IN_IWRAM ?= 1
CFLAGS += -DREAL8_GBA_FB_IN_IWRAM=$(REAL8_GBA_FB_IN_IWRAM)
REAL8_GBA_ENABLE_AUDIO ?= 0
CFLAGS += -DREAL8_GBA_ENABLE_AUDIO=$(REAL8_GBA_ENABLE_AUDIO)

CXXFLAGS ?= $(CFLAGS) -fexceptions -std=gnu++17
ASFLAGS  ?= -g $(ARCH)

LIBS ?= -lstdc++ -lgba -lm
LDFLAGS += -Wl,--defsym=__heap_size=0x10000 -Wl,--defsym=__stack_size=0x8000

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

export OFILES_SOURCES := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES_BIN := $(addsuffix .o,$(BINFILES))
export OFILES := $(OFILES_BIN) $(OFILES_SOURCES)

export HFILES := $(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
    $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
    -I$(CURDIR)/$(BUILD)

export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

export CFLAGS := $(CFLAGS) $(INCLUDE)
export CXXFLAGS := $(CFLAGS) -fexceptions -std=gnu++17

.PHONY: all clean rom tools

rom: $(BUILD) $(CART_BLOB) $(SPLASH_IMG) $(SPLASH_PAL)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

ifeq ($(OS),Windows_NT)
tools: $(CART_PACKER) $(CART_PACKER_GUI)
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

$(CART_PACKER_GUI): $(CART_PACKER_GUI_SRCS) $(CART_PACKER_GUI_RES)
	$(call MKDIR_P,$(BUILD))
	$(HOSTCC) $(HOSTCXXFLAGS) -o $@ $^ $(HOSTGUI_LDFLAGS)

ifeq ($(OS),Windows_NT)
$(CART_PACKER_RES): $(CART_PACKER_RC) icon.ico
	$(call MKDIR_P,$(BUILD))
	$(WINDRES) -i $< -o $@

$(CART_PACKER_GUI_RES): $(CART_PACKER_GUI_RC) icon.ico
	$(call MKDIR_P,$(BUILD))
	$(WINDRES) -i $< -o $@
endif

$(CART_BLOB): $(CART_PNG) $(CART_PACKER)
	@$(CART_PACKER) $(CART_PNG) $(CART_BLOB)

$(SPLASH_IMG) $(SPLASH_PAL): $(SPLASH_PNG)
	@$(GRIT) $(SPLASH_PNG) -gb -gB8 -m! -p -ftb -o splash
	$(call MV_F,splash.img.bin,$(SPLASH_IMG))
	$(call MV_F,splash.pal.bin,$(SPLASH_PAL))
	$(call RM_F,splash.h)

clean:
	@echo clean ...
	$(call RM_RF,$(BUILD))
	$(call RM_F,$(TARGET).gba)
	$(call RM_F,$(TARGET).elf)
	$(call RM_F,$(CART_BLOB))
	$(call RM_F,$(SPLASH_IMG))
	$(call RM_F,$(SPLASH_PAL))
	$(call RM_F,splash.img.bin)
	$(call RM_F,splash.pal.bin)
	$(call RM_F,splash.h)

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
