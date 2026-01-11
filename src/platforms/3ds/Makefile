#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

ROMFS_BASE := romfs
ROMFS := $(ROMFS_BASE)
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
ICON     ?= icon.png

# CIA banner assets (override for custom standalone builds)
BANNER_ICON  ?= banner/$(TARGET)-icon.png
BANNER_IMAGE ?= banner/$(TARGET)-banner.png
BANNER_AUDIO ?= banner/$(TARGET).wav

#---------------------------------------------------------------------------------
# Embedded cart blob (standalone template)
#---------------------------------------------------------------------------------
CART_TEMPLATE_CAPACITY ?= 1048576
CART_BLOB_BIN := $(TOPDIR)/data/cart_blob.bin
TEMPLATE_3DSX := $(TARGET)_template.3dsx
TEMPLATE_ELF := $(TARGET)_template.elf
EMBED_CART ?= 0
TEMPLATE_BUILD := build-template
EMBED_CART_DEFS :=
ifeq ($(EMBED_CART),1)
  DATA += data
  EMBED_CART_DEFS := -DREAL8_3DS_EMBED_CART
endif

#---------------------------------------------------------------------------------
# Host tools (Windows GUI)
#---------------------------------------------------------------------------------
ifeq ($(OS),Windows_NT)
  EXEEXT := .exe
else
  EXEEXT :=
endif

HOSTCC ?= g++
HOSTCXXFLAGS := -O2 -std=gnu++17 -Isource -I../../core -I../../hal -I../../../lib/lodePNG
HOSTGUI_LDFLAGS := -mwindows -lcomdlg32
CART_BLOB_GEN := $(BUILD)/cart_blob_gen$(EXEEXT)
CART_BLOB_GEN_SRCS := source/cart_blob_gen.cpp
PNG2ICO := $(BUILD)/png2ico$(EXEEXT)
PNG2ICO_SRCS := source/png2ico.cpp
PICO_ICON_PNG := $(TOPDIR)/$(ICON)
PICO_ICON_ICO := $(BUILD)/PicoTo3DS.ico

ifeq ($(OS),Windows_NT)
  CART_PACKER_GUI := $(BUILD)/PicoTo3DS$(EXEEXT)
  CART_PACKER_GUI_SRCS := source/cart_packer_gui.cpp ../../core/real8_cart.cpp ../../core/real8_compression.cpp ../../../lib/lodePNG/lodePNG.cpp
  WINDRES ?= windres
  EMBED_TEMPLATE ?= 1
  TEMPLATE_RCDATA_ID ?= 301
  TEMPLATE_ELF_RCDATA_ID ?= 302
  TEMPLATE_EMBED_RC := $(BUILD)/template_embed.rc
  TEMPLATE_EMBED_RES := $(BUILD)/template_embed_res.o
  TEMPLATE_3DSX_ABS := $(abspath $(TEMPLATE_3DSX))
  TEMPLATE_ELF_ABS := $(abspath $(TEMPLATE_ELF))
  PICO_ICON_ICO_ABS := $(abspath $(PICO_ICON_ICO))
endif

CART_PACKER_GUI_EXTRA_RES :=
ifeq ($(OS),Windows_NT)
ifeq ($(EMBED_TEMPLATE),1)
  CART_PACKER_GUI_EXTRA_RES := $(TEMPLATE_EMBED_RES)
endif
endif

#---------------------------------------------------------------------------------
# Standalone cart build (make game.p8.png)
#---------------------------------------------------------------------------------
STANDALONE_CART := $(firstword $(filter %.p8.png,$(MAKECMDGOALS)))
ifneq ($(STANDALONE_CART),)
REAL8_3DS_STANDALONE := 1
BUILD    := build-standalone
GFXBUILD := $(BUILD)
STANDALONE_CART_NAME := $(notdir $(STANDALONE_CART))
STANDALONE_CART_ROMFS := romfs:/$(STANDALONE_CART_NAME)
ROMFS    := $(BUILD)/romfs_standalone
endif

ifeq ($(REAL8_3DS_STANDALONE),1)
ifeq ($(BUILD),build)
BUILD := build-standalone
GFXBUILD := $(BUILD)
endif
endif

#---------------------------------------------------------------------------------
# Toolchain flags
#---------------------------------------------------------------------------------
CC = $(CXX)

ARCH    := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS  := -g -Wall -O2 -mword-relocations \
           -fomit-frame-pointer -ffunction-sections \
           $(ARCH)

CFLAGS  += $(INCLUDE) -D__3DS__ -DVER_STR=\"$(APP_VERSION)\" -DNOMINMAX -DCURL_STATICLIB

CXXFLAGS := $(CFLAGS) -fno-rtti -std=gnu++17

ASFLAGS := -g $(ARCH)
LDFLAGS = -specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS := -lcitro2d -lcitro3d -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lz -lctru -lm

CFLAGS   += -Wno-sign-compare
CXXFLAGS += -Wno-sign-compare

CXXFLAGS += -fexceptions
LDFLAGS  += -fexceptions

CFLAGS  += -DLUA_32BITS
ifneq ($(strip $(EMBED_CART_DEFS)),)
  CFLAGS   += $(EMBED_CART_DEFS)
  CXXFLAGS += $(EMBED_CART_DEFS)
endif

ifeq ($(REAL8_3DS_STANDALONE),1)
CFLAGS  += -DREAL8_3DS_STANDALONE
CXXFLAGS += -DREAL8_3DS_STANDALONE
ifneq ($(STANDALONE_CART),)
  CFLAGS  += -DREAL8_3DS_STANDALONE_CART=\"$(STANDALONE_CART_ROMFS)\"
  CXXFLAGS += -DREAL8_3DS_STANDALONE_CART=\"$(STANDALONE_CART_ROMFS)\"
endif
endif

#---------------------------------------------------------------------------------
# CIA SMDH metadata (Home Menu title lines)
#---------------------------------------------------------------------------------
ifeq ($(REAL8_3DS_STANDALONE),1)
  CIA_TITLE_SHORT ?= $(TARGET)
  CIA_TITLE_LONG ?= $(APP_TITLE)
  CIA_TITLE_PUBLISHER ?= $(APP_AUTHOR)
else
  CIA_TITLE_SHORT ?= A PICO-8 Shell
  CIA_TITLE_LONG ?= REAL-8 EXPLORER
  CIA_TITLE_PUBLISHER ?= By @natureglass
endif

#---------------------------------------------------------------------------------
# 3DS runtime defaults (override via make REAL8_STRETCHED=1, etc.)
#---------------------------------------------------------------------------------
REAL8_STRETCHED ?= 0
REAL8_CRTFILTER ?= 0
REAL8_INTERPOL8 ?= 0
REAL8_TOP_NOBACK ?= 0
REAL8_BOTTOM_NOBACK ?= 0

CFLAGS  += -DREAL8_STRETCHED=$(REAL8_STRETCHED)
CXXFLAGS += -DREAL8_STRETCHED=$(REAL8_STRETCHED)
CFLAGS  += -DREAL8_CRTFILTER=$(REAL8_CRTFILTER)
CXXFLAGS += -DREAL8_CRTFILTER=$(REAL8_CRTFILTER)
CFLAGS  += -DREAL8_INTERPOL8=$(REAL8_INTERPOL8)
CXXFLAGS += -DREAL8_INTERPOL8=$(REAL8_INTERPOL8)
CFLAGS  += -DREAL8_TOP_NOBACK=$(REAL8_TOP_NOBACK)
CXXFLAGS += -DREAL8_TOP_NOBACK=$(REAL8_TOP_NOBACK)
CFLAGS  += -DREAL8_BOTTOM_NOBACK=$(REAL8_BOTTOM_NOBACK)
CXXFLAGS += -DREAL8_BOTTOM_NOBACK=$(REAL8_BOTTOM_NOBACK)

# ---- Silence ALL warnings ----
CFLAGS   := $(filter-out -Werror,$(CFLAGS))
CXXFLAGS := $(filter-out -Werror,$(CXXFLAGS))

CFLAGS   += -w
CXXFLAGS += -w

#---------------------------------------------------------------------------------
# list of directories containing libraries, this must be the top level containing
# include and lib
#---------------------------------------------------------------------------------
LIBDIRS := $(CTRULIB) $(PORTLIBS)

#---------------------------------------------------------------------------------
# no real need to edit anything past this point unless you need to add additional
# rules for different file extensions
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
# Default target for top-level invocation.
.DEFAULT_GOAL := all
#---------------------------------------------------------------------------------

export OUTPUT := $(CURDIR)/$(TARGET)
export TOPDIR := $(CURDIR)

export VPATH := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
                $(foreach dir,$(GRAPHICS),$(CURDIR)/$(dir)) \
                $(foreach dir,$(DATA),$(CURDIR)/$(dir)) \
                $(TOPDIR)/data

export DEPSDIR := $(CURDIR)/$(BUILD)

CFILES     := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES     := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
PICAFILES  := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.v.pica)))
SHLISTFILES:= $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.shlist)))
GFXFILES   := $(foreach dir,$(GRAPHICS),$(notdir $(wildcard $(dir)/*.t3s)))
BINFILES   := $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))
ifeq ($(EMBED_CART),1)
BINFILES   := $(filter-out cart_blob.bin,$(BINFILES))
BINFILES   += cart_blob.bin
endif

EXCLUDE_CPPFILES := lua.cpp luac.cpp lmathlib.cpp cart_blob_gen.cpp
EXCLUDE_CPPFILES += png2ico.cpp
ifeq ($(REAL8_3DS_STANDALONE),1)
EXCLUDE_CPPFILES += real8_shell.cpp
endif
EXCLUDE_CPPFILES += cart_packer_gui.cpp
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

.PHONY: all clean cia standalone tools template

#---------------------------------------------------------------------------------
template: $(TEMPLATE_3DSX) $(TEMPLATE_ELF)

# Standalone template build (embedded cart blob, no SMDH)
$(TEMPLATE_3DSX) $(TEMPLATE_ELF): $(CART_BLOB_BIN)
	@$(MAKE) --no-print-directory BUILD=$(TEMPLATE_BUILD) TOPDIR=$(CURDIR) \
		REAL8_3DS_STANDALONE=1 EMBED_CART=1 OUTPUT=$(CURDIR)/$(TARGET)_template NO_SMDH=1 SKIP_CIA=1 _3DSXFLAGS= all

#---------------------------------------------------------------------------------
all: $(BUILD) $(GFXBUILD) $(DEPSDIR) $(ROMFS_T3XFILES) $(T3XHFILES) $(ROMFS)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile BUILD=$(BUILD) $(if $(STANDALONE_CART),STANDALONE_CART="$(STANDALONE_CART)")
ifneq ($(SKIP_CIA),1)
	@$(MAKE) --no-print-directory cia $(if $(STANDALONE_CART),STANDALONE_CART="$(STANDALONE_CART)")
endif

standalone: all

ifneq ($(STANDALONE_CART),)
.PHONY: $(STANDALONE_CART)
$(STANDALONE_CART): standalone
endif

%.p8.png: standalone

$(BUILD):
	@mkdir -p $@

$(CART_BLOB_GEN): $(CART_BLOB_GEN_SRCS)
	@mkdir -p $(BUILD)
	$(HOSTCC) $(HOSTCXXFLAGS) -o $@ $^

$(PNG2ICO): $(PNG2ICO_SRCS)
	@mkdir -p $(BUILD)
	$(HOSTCC) $(HOSTCXXFLAGS) -o $@ $^

$(PICO_ICON_ICO): $(PNG2ICO) $(PICO_ICON_PNG)
	@mkdir -p $(BUILD)
	@$(PNG2ICO) "$(PICO_ICON_PNG)" "$@"

$(CART_BLOB_BIN): $(CART_BLOB_GEN)
	@mkdir -p $(dir $@)
	@$(CART_BLOB_GEN) --template $@ $(CART_TEMPLATE_CAPACITY)

ifeq ($(REAL8_3DS_STANDALONE),1)
$(ROMFS):
	@rm -fr "$@"
	@mkdir -p "$@"
	@if [ -d "$(ROMFS_BASE)" ]; then cp -f "$(ROMFS_BASE)"/* "$@"/; fi
ifneq ($(STANDALONE_CART),)
	@cp -f "$(STANDALONE_CART)" "$@/$(STANDALONE_CART_NAME)"
endif
else
$(ROMFS):
	@mkdir -p "$@"
endif

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
	@rm -fr build build-standalone build-template $(TARGET).3dsx $(OUTPUT).smdh $(TARGET).elf $(GFXBUILD) \
		$(TARGET)-strip.elf $(TARGET)-cia.bnr $(TARGET)-cia.smdh $(TARGET).cia $(TARGET)_template.3dsx \
		$(TARGET)_template.elf \
		$(CART_BLOB_BIN)

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
$(TARGET)-cia.bnr: $(BANNER_IMAGE) $(BANNER_AUDIO)
	@bannertool makebanner -i "$(BANNER_IMAGE)" -a "$(BANNER_AUDIO)" -o $(TARGET)-cia.bnr
#---------------------------------------------------------------------------------
$(TARGET)-cia.smdh: $(BANNER_ICON)
	@bannertool makesmdh -s "$(CIA_TITLE_SHORT)" -l "$(CIA_TITLE_LONG)" -p "$(CIA_TITLE_PUBLISHER)" -i "$(BANNER_ICON)" -o $(TARGET)-cia.smdh -r regionfree

#---------------------------------------------------------------------------------
ROMFS_ENABLED := 0
ifneq ($(ROMFS),)
ifneq ($(HAVE_3DSTOOL),)
ifneq ($(wildcard $(ROMFS)),)
ROMFS_ENABLED := 1
endif
endif
endif
ifeq ($(REAL8_3DS_STANDALONE),1)
ifneq ($(HAVE_3DSTOOL),)
ROMFS_ENABLED := 1
endif
endif

cia: $(TARGET)-strip.elf $(TARGET)-cia.bnr $(TARGET)-cia.smdh $(if $(ROMFS_ENABLED),$(ROMFS_IMAGE),) banner/$(TARGET).rsf
	@makerom -f cia -elf $(TARGET)-strip.elf $(if $(ROMFS_ENABLED),-romfs $(ROMFS_IMAGE),) -icon $(TARGET)-cia.smdh -banner $(TARGET)-cia.bnr -desc app:4 -v -o $(TARGET).cia -target t -exefslogo -rsf banner/$(TARGET).rsf \
	-DAPP_TITLE="$(APP_TITLE)" -DAPP_PRODUCT_CODE="$(APP_PRODUCT_CODE)" -DAPP_UNIQUE_ID="$(APP_UNIQUE_ID)"
	@echo "built cia"
#---------------------------------------------------------------------------------

ifeq ($(OS),Windows_NT)
ifeq ($(EMBED_TEMPLATE),1)
tools: template $(CART_PACKER_GUI)
else
tools: $(CART_PACKER_GUI)
endif

$(CART_PACKER_GUI): $(CART_PACKER_GUI_SRCS) $(CART_PACKER_GUI_EXTRA_RES)
	@mkdir -p $(BUILD)
	$(HOSTCC) $(HOSTCXXFLAGS) -o $@ $^ $(HOSTGUI_LDFLAGS)

ifeq ($(EMBED_TEMPLATE),1)
$(TEMPLATE_EMBED_RC): $(TEMPLATE_3DSX) $(TEMPLATE_ELF) $(PICO_ICON_ICO)
	@mkdir -p $(BUILD)
	@TEMPLATE_3DSX_PATH="$$(cygpath -m "$(TEMPLATE_3DSX_ABS)" 2>/dev/null || echo "$(TEMPLATE_3DSX_ABS)")"; \
	  TEMPLATE_3DSX_PATH="$${TEMPLATE_3DSX_PATH//\\//}"; \
	  TEMPLATE_ELF_PATH="$$(cygpath -m "$(TEMPLATE_ELF_ABS)" 2>/dev/null || echo "$(TEMPLATE_ELF_ABS)")"; \
	  TEMPLATE_ELF_PATH="$${TEMPLATE_ELF_PATH//\\//}"; \
	  ICON_PATH="$$(cygpath -m "$(PICO_ICON_ICO_ABS)" 2>/dev/null || echo "$(PICO_ICON_ICO_ABS)")"; \
	  ICON_PATH="$${ICON_PATH//\\//}"; \
	  printf "%s\n" "$(TEMPLATE_RCDATA_ID) RCDATA \"$$TEMPLATE_3DSX_PATH\"" > $@; \
	  printf "%s\n" "$(TEMPLATE_ELF_RCDATA_ID) RCDATA \"$$TEMPLATE_ELF_PATH\"" >> $@; \
	  printf "%s\n" "1 ICON \"$$ICON_PATH\"" >> $@

$(TEMPLATE_EMBED_RES): $(TEMPLATE_EMBED_RC)
	@mkdir -p $(BUILD)
	$(WINDRES) -I. -i $< -O coff -o $@
endif
else
tools:
	@echo "tools target is only supported on Windows."
endif

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

