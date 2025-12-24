#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

ROMFS := romfs
ROMFS_IMAGE := $(TARGET).romfs
3DSTOOL := $(firstword $(wildcard $(CURDIR)/3dstool/3dstool.exe) $(wildcard $(CURDIR)/3dstool/3dstool))
HAVE_3DSTOOL := $(strip $(3DSTOOL))

#---------------------------------------------------------------------------------
# Metadata (override with environment variables if desired)
#---------------------------------------------------------------------------------
APP_TITLE       ?= REAL-8 Explorer
APP_AUTHOR      ?= REAL-8
APP_VERSION     ?= 1.0
APP_DESCRIPTION ?= Real-8 3DS Host

APP_UNIQUE_ID    ?= 0x524E8
APP_PRODUCT_CODE ?= CTR-P-REAL

#---------------------------------------------------------------------------------
# Build layout
#---------------------------------------------------------------------------------
TARGET   := REAL8
BUILD    := build
SOURCES  := source ../../core ../../hal ../../../lib/z8lua ../../../lib/lodePNG
DATA     :=
INCLUDES := source ../../core ../../hal ../../../lib/z8lua ../../../lib/lodePNG
GRAPHICS :=
GFXBUILD := $(BUILD)
ICON     := icon.png

#---------------------------------------------------------------------------------
# Toolchain flags
#---------------------------------------------------------------------------------
CC = $(CXX)

ARCH    := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS  := -g -Wall -O2 -mword-relocations \
           -fomit-frame-pointer -ffunction-sections \
           $(ARCH)

CFLAGS  += $(INCLUDE) -D__3DS__ -DVER_STR=\"$(APP_VERSION)\" -DNOMINMAX

CXXFLAGS := $(CFLAGS) -fno-rtti -std=gnu++17

ASFLAGS := -g $(ARCH)
LDFLAGS = -specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS := -lcitro2d -lcitro3d -lctru -lm

CFLAGS   += -Wno-sign-compare
CXXFLAGS += -Wno-sign-compare

CXXFLAGS += -fexceptions
LDFLAGS  += -fexceptions

CFLAGS  += -DLUA_32BITS

# ---- Silence ALL warnings ----
CFLAGS   := $(filter-out -Werror,$(CFLAGS))
CXXFLAGS := $(filter-out -Werror,$(CXXFLAGS))

CFLAGS   += -w
CXXFLAGS += -w

#---------------------------------------------------------------------------------
# list of directories containing libraries, this must be the top level containing
# include and lib
#---------------------------------------------------------------------------------
LIBDIRS := $(CTRULIB)

#---------------------------------------------------------------------------------
# no real need to edit anything past this point unless you need to add additional
# rules for different file extensions
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT := $(CURDIR)/$(TARGET)
export TOPDIR := $(CURDIR)

export VPATH := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
                $(foreach dir,$(GRAPHICS),$(CURDIR)/$(dir)) \
                $(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR := $(CURDIR)/$(BUILD)

CFILES     := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES     := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
PICAFILES  := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.v.pica)))
SHLISTFILES:= $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.shlist)))
GFXFILES   := $(foreach dir,$(GRAPHICS),$(notdir $(wildcard $(dir)/*.t3s)))
BINFILES   := $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

EXCLUDE_CPPFILES := lua.cpp luac.cpp lmathlib.cpp
CPPFILES := $(filter-out $(EXCLUDE_CPPFILES),$(CPPFILES))

#---------------------------------------------------------------------------------
# use CXX for linking C++ projects, CC for standard C
#---------------------------------------------------------------------------------
ifeq ($(strip $(CPPFILES)),)
#---------------------------------------------------------------------------------
	export LD := $(CC)
#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------
	export LD := $(CXX)
#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------

#---------------------------------------------------------------------------------
ifeq ($(GFXBUILD),$(BUILD))
#---------------------------------------------------------------------------------
export T3XFILES :=  $(GFXFILES:.t3s=.t3x)
#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------
export ROMFS_T3XFILES := $(patsubst %.t3s, $(GFXBUILD)/%.t3x, $(GFXFILES))
export T3XHFILES      := $(patsubst %.t3s, $(BUILD)/%.h, $(GFXFILES))
#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------

export OFILES_SOURCES := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)

export OFILES_BIN := $(addsuffix .o,$(BINFILES)) \
                     $(PICAFILES:.v.pica=.shbin.o) $(SHLISTFILES:.shlist=.shbin.o) \
                     $(addsuffix .o,$(T3XFILES))

export OFILES := $(OFILES_BIN) $(OFILES_SOURCES)

export HFILES := $(PICAFILES:.v.pica=_shbin.h) $(SHLISTFILES:.shlist=_shbin.h) \
                 $(addsuffix .h,$(subst .,_,$(BINFILES))) \
                 $(GFXFILES:.t3s=.h)

export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                  $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                  -I$(CURDIR)/$(BUILD)

export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

export _3DSXDEPS := $(if $(NO_SMDH),,$(OUTPUT).smdh)

ifeq ($(strip $(ICON)),)
	icons := $(wildcard *.png)
	ifneq (,$(findstring $(TARGET).png,$(icons)))
		export APP_ICON := $(TOPDIR)/$(TARGET).png
	else
		ifneq (,$(findstring icon.png,$(icons)))
			export APP_ICON := $(TOPDIR)/icon.png
		endif
	endif
else
	export APP_ICON := $(TOPDIR)/$(ICON)
endif

ifeq ($(strip $(NO_SMDH)),)
	export _3DSXFLAGS += --smdh=$(CURDIR)/$(TARGET).smdh
endif

ifneq ($(ROMFS),)
	export _3DSXFLAGS += --romfs=$(CURDIR)/$(ROMFS)
endif

.PHONY: all clean cia

#---------------------------------------------------------------------------------
all: $(BUILD) $(GFXBUILD) $(DEPSDIR) $(ROMFS_T3XFILES) $(T3XHFILES)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile
	@$(MAKE) --no-print-directory cia

$(BUILD):
	@mkdir -p $@

ifneq ($(GFXBUILD),$(BUILD))
$(GFXBUILD):
	@mkdir -p $@
endif

ifneq ($(DEPSDIR),$(BUILD))
$(DEPSDIR):
	@mkdir -p $@
endif

#---------------------------------------------------------------------------------
clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).3dsx $(OUTPUT).smdh $(TARGET).elf $(GFXBUILD) \
		$(TARGET)-strip.elf $(TARGET)-cia.bnr $(TARGET)-cia.smdh $(TARGET).cia

#---------------------------------------------------------------------------------
$(GFXBUILD)/%.t3x	$(BUILD)/%.h	:	%.t3s
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@tex3ds -i $< -H $(BUILD)/$*.h -d $(DEPSDIR)/$*.d -o $(GFXBUILD)/$*.t3x

#---------------------------------------------------------------------------------

#---------------------------------------------------------------------------------
$(ROMFS_IMAGE): $(ROMFS)
	@$(3DSTOOL) -cvtf romfs $(ROMFS_IMAGE) --romfs-dir $(ROMFS)/
	@echo "built romfs"
#---------------------------------------------------------------------------------
$(TARGET)-strip.elf: $(BUILD)
	@$(STRIP) $(TARGET).elf -o $(TARGET)-strip.elf
#---------------------------------------------------------------------------------
$(TARGET)-cia.bnr: banner/$(TARGET)-banner.png banner/$(TARGET).wav
	@bannertool makebanner -i banner/$(TARGET)-banner.png -a banner/$(TARGET).wav -o $(TARGET)-cia.bnr
#---------------------------------------------------------------------------------
$(TARGET)-cia.smdh: banner/$(TARGET)-icon.png
	@bannertool makesmdh -s "$(TARGET)" -l "$(APP_TITLE)" -p "$(APP_AUTHOR)" -i banner/$(TARGET)-icon.png -o $(TARGET)-cia.smdh -r regionfree

#---------------------------------------------------------------------------------
cia: $(TARGET)-strip.elf $(TARGET)-cia.bnr $(TARGET)-cia.smdh $(if $(and $(wildcard $(ROMFS)),$(HAVE_3DSTOOL)),$(ROMFS_IMAGE),) banner/$(TARGET).rsf
	@makerom -f cia -elf $(TARGET)-strip.elf $(if $(and $(wildcard $(ROMFS)),$(HAVE_3DSTOOL)),-romfs $(ROMFS_IMAGE),) -icon $(TARGET)-cia.smdh -banner $(TARGET)-cia.bnr -desc app:4 -v -o $(TARGET).cia -target t -exefslogo -rsf banner/$(TARGET).rsf \
	-DAPP_TITLE="$(APP_TITLE)" -DAPP_PRODUCT_CODE="$(APP_PRODUCT_CODE)" -DAPP_UNIQUE_ID="$(APP_UNIQUE_ID)"
	@echo "built cia"
#---------------------------------------------------------------------------------

else

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
$(OUTPUT).3dsx	:	$(OUTPUT).elf $(_3DSXDEPS)

$(OFILES_SOURCES) : $(HFILES)

$(OUTPUT).elf	:	$(OFILES)

#---------------------------------------------------------------------------------
# you need a rule like this for each extension you use as binary data
#---------------------------------------------------------------------------------
%.bin.o	%_bin.h :	%.bin
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)

#---------------------------------------------------------------------------------
.PRECIOUS	:	%.t3x
#---------------------------------------------------------------------------------
%.t3x.o	%_t3x.h :	%.t3x
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)

#---------------------------------------------------------------------------------
# rules for assembling GPU shaders
#---------------------------------------------------------------------------------
define shader-as
	$(eval CURBIN := $*.shbin)
	$(eval DEPSFILE := $(DEPSDIR)/$*.shbin.d)
	echo "$(CURBIN).o: $< $1" > $(DEPSFILE)
	echo "extern const u8" `(echo $(CURBIN) | sed -e 's/^\([0-9]\)/_\1/' | tr . _)`"_end[];" > `(echo $(CURBIN) | tr . _)`.h
	echo "extern const u8" `(echo $(CURBIN) | sed -e 's/^\([0-9]\)/_\1/' | tr . _)`"[];" >> `(echo $(CURBIN) | tr . _)`.h
	echo "extern const u32" `(echo $(CURBIN) | sed -e 's/^\([0-9]\)/_\1/' | tr . _)`_size";" >> `(echo $(CURBIN) | tr . _)`.h
	picasso -o $(CURBIN) $1
	bin2s $(CURBIN) | $(AS) -o $*.shbin.o
endef

%.shbin.o %_shbin.h : %.v.pica %.g.pica
	@echo $(notdir $^)
	@$(call shader-as,$^)

%.shbin.o %_shbin.h : %.v.pica
	@echo $(notdir $<)
	@$(call shader-as,$<)

%.shbin.o %_shbin.h : %.shlist
	@echo $(notdir $<)
	@$(call shader-as,$(foreach file,$(shell cat $<),$(dir $<)$(file)))

#---------------------------------------------------------------------------------
%.t3x	%.h	:	%.t3s
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@tex3ds -i $< -H $*.h -d $*.d -o $*.t3x

-include $(DEPSDIR)/*.d

#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------
