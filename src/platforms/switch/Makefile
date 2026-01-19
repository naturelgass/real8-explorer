#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>devkitPro")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules

#---------------------------------------------------------------------------------
# Metadata (override with environment variables if desired)
#---------------------------------------------------------------------------------
APP_TITLE ?=
APP_AUTHOR ?=
APP_VERSION ?= 1.1.0
ICON ?= icon.jpg

#---------------------------------------------------------------------------------
# Build layout
#---------------------------------------------------------------------------------
TARGET := Real8Switch
BUILD := build
SOURCES := . ../../core ../../../lib/z8lua
DATA :=
INCLUDES := . ../../core ../../../lib/z8lua
ROMFS_BASE := romfs
ROMFS := $(ROMFS_BASE)

#---------------------------------------------------------------------------------
# Embedded cart blob (template build)
#---------------------------------------------------------------------------------
CART_TEMPLATE_CAPACITY ?= 1048576
CART_BLOB_BIN := $(TOPDIR)/data/cart_blob.bin
CART_BLOB_GEN := $(BUILD)/cart_blob_gen$(EXEEXT)
CART_BLOB_GEN_SRCS := cart_blob_gen.cpp
EMBED_CART ?= 0
EMBED_CART_DEFS :=
ifeq ($(EMBED_CART),1)
  DATA += data
  EMBED_CART_DEFS := -DREAL8_SWITCH_EMBED_CART
endif

#---------------------------------------------------------------------------------
# Standalone cart build (make game.p8.png)
#---------------------------------------------------------------------------------
STANDALONE_CART := $(firstword $(filter %.p8 %.p8.png,$(MAKECMDGOALS)))
ifneq ($(STANDALONE_CART),)
REAL8_SWITCH_STANDALONE := 1
BUILD := build-standalone
STANDALONE_CART_NAME := $(notdir $(STANDALONE_CART))
STANDALONE_CART_BASE := $(basename $(basename $(STANDALONE_CART_NAME)))
STANDALONE_CART_ROMFS := romfs:/$(STANDALONE_CART_NAME)
ROMFS := $(BUILD)/romfs_standalone
TARGET := $(STANDALONE_CART_BASE)
endif

ifeq ($(strip $(APP_TITLE)),)
ifeq ($(REAL8_SWITCH_STANDALONE),1)
ifneq ($(strip $(STANDALONE_CART_BASE)),)
APP_TITLE := $(STANDALONE_CART_BASE)
else
APP_TITLE := $(TARGET)
endif
else
APP_TITLE := Real-8 VM
endif
endif

ifeq ($(strip $(APP_AUTHOR)),)
APP_AUTHOR := Real8 Dev
endif

#---------------------------------------------------------------------------------
# Switch runtime defaults (override via make REAL8_STRETCHED=1, etc.)
#---------------------------------------------------------------------------------
REAL8_STRETCHED ?= 0
REAL8_CRTFILTER ?= 0
REAL8_INTERPOL8 ?= 0

#---------------------------------------------------------------------------------
# Toolchain flags
#---------------------------------------------------------------------------------
ARCH := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
DEFINES := -DLUA_USE_POSIX -DCURL_STATICLIB
DEFINES += -DREAL8_STRETCHED=$(REAL8_STRETCHED)
DEFINES += -DREAL8_CRTFILTER=$(REAL8_CRTFILTER)
DEFINES += -DREAL8_INTERPOL8=$(REAL8_INTERPOL8)
DEFINES += $(EMBED_CART_DEFS)

ifeq ($(REAL8_SWITCH_STANDALONE),1)
DEFINES += -DREAL8_SWITCH_STANDALONE -DREAL8_SWITCH_STANDALONE_CART=\"$(STANDALONE_CART_ROMFS)\"
endif

CFLAGS := -g -O2 -ffunction-sections -fdata-sections $(ARCH) -ftls-model=local-exec $(DEFINES)
CFLAGS += $(INCLUDE) -D__SWITCH__
CXXFLAGS := $(CFLAGS) -std=gnu++17
ASFLAGS := -g $(ARCH)
LDFLAGS = -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS := -lSDL2 -lcurl -lz -lEGL -lglapi -ldrm_nouveau -lnx -lpthread -lm

#---------------------------------------------------------------------------------
# list of directories containing libraries, this must be the top level containing
# include and lib
#---------------------------------------------------------------------------------
LIBDIRS := $(PORTLIBS) $(LIBNX)

#---------------------------------------------------------------------------------
# Host tools (Windows GUI)
#---------------------------------------------------------------------------------
ifeq ($(OS),Windows_NT)
EXEEXT := .exe
else
EXEEXT :=
endif

HOSTCC ?= g++
HOSTCXXFLAGS := -O2 -std=gnu++17 -I../../core -I../../hal -I../../../lib/lodePNG
HOSTGUI_LDFLAGS := -mwindows -luser32 -lgdi32 -lcomdlg32 -lshell32
TOOLS_DIR := build
TOOLS_EXE := $(TOOLS_DIR)/Pico2Switch$(EXEEXT)
TOOLS_SRCS := cart_packer_gui.cpp ../../core/real8_cart.cpp ../../core/real8_compression.cpp ../../../lib/lodePNG/lodePNG.cpp
PNG2ICO := $(TOOLS_DIR)/png2ico$(EXEEXT)
PNG2ICO_SRCS := png2ico.cpp
ICON_PNG := $(TOPDIR)/icon.png
ICON_ICO := $(TOOLS_DIR)/Pico2Switch.ico
ICON_ICO_ABS := $(abspath $(ICON_ICO))
TEMPLATE_TARGET := Real8Switch_template
TEMPLATE_BUILD := build-template
TEMPLATE_NRO := $(TOPDIR)/$(TEMPLATE_TARGET).nro
TEMPLATE_NRO_ABS := $(abspath $(TEMPLATE_NRO))
TEMPLATE_RCDATA_ID ?= 301
WINDRES ?= windres
TOOLS_RC := $(TOOLS_DIR)/Pico2Switch.rc
TOOLS_RES := $(TOOLS_DIR)/Pico2Switch_res.o

#---------------------------------------------------------------------------------
# no real need to edit anything past this point unless you need to add additional
# rules for different file extensions
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT := $(CURDIR)/$(TARGET)
export TOPDIR := $(CURDIR)

export VPATH := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
		$(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR := $(CURDIR)/$(BUILD)

CFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES := $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))
ifeq ($(EMBED_CART),1)
BINFILES := $(filter-out cart_blob.bin,$(BINFILES))
BINFILES += cart_blob.bin
endif

EXCLUDE_CPPFILES := lua.cpp luac.cpp lmathlib.cpp cart_packer_gui.cpp png2ico.cpp cart_blob_gen.cpp
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

export OFILES_BIN := $(addsuffix .o,$(BINFILES))
export OFILES_SRC := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES := $(OFILES_BIN) $(OFILES_SRC)
export HFILES_BIN := $(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
		$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
		-I$(PORTLIBS)/include/SDL2 \
		-I$(CURDIR)/$(BUILD)

export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

ifeq ($(strip $(ICON)),)
	icons := $(wildcard *.jpg)
	ifneq (,$(findstring $(TARGET).jpg,$(icons)))
		export APP_ICON := $(TOPDIR)/$(TARGET).jpg
	else
		ifneq (,$(findstring icon.jpg,$(icons)))
			export APP_ICON := $(TOPDIR)/icon.jpg
		endif
	endif
else
	export APP_ICON := $(TOPDIR)/$(ICON)
endif

export NROFLAGS += --icon=$(APP_ICON)
export NROFLAGS += --nacp=$(CURDIR)/$(TARGET).nacp

ifneq ($(ROMFS),)
	export NROFLAGS += --romfsdir=$(CURDIR)/$(ROMFS)
endif

.PHONY: $(BUILD) clean all tools standalone template

#---------------------------------------------------------------------------------
all: $(ROMFS) $(BUILD)

$(CART_BLOB_GEN): $(CART_BLOB_GEN_SRCS)
	@[ -d $(BUILD) ] || mkdir -p $(BUILD)
	$(HOSTCC) $(HOSTCXXFLAGS) -o $@ $^

$(CART_BLOB_BIN): $(CART_BLOB_GEN)
	@[ -d $(dir $@) ] || mkdir -p $(dir $@)
	@$(CART_BLOB_GEN) --template $@ $(CART_TEMPLATE_CAPACITY)

template: $(CART_BLOB_BIN)
	@$(MAKE) --no-print-directory BUILD=$(TEMPLATE_BUILD) TOPDIR=$(CURDIR) \
		REAL8_SWITCH_STANDALONE=1 EMBED_CART=1 TARGET=$(TEMPLATE_TARGET) \
		OUTPUT=$(CURDIR)/$(TEMPLATE_TARGET) all

$(TEMPLATE_NRO): template
	@if [ ! -f "$@" ]; then echo "Template NRO not found: $@"; exit 1; fi

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile BUILD=$(BUILD)

#---------------------------------------------------------------------------------
clean:
	@echo clean ...
	@rm -fr build build-standalone build-template *.nro *.elf *.nacp data/cart_blob.bin

#---------------------------------------------------------------------------------
ifeq ($(OS),Windows_NT)
tools: $(TOOLS_EXE)
$(TOOLS_EXE): $(TOOLS_SRCS) $(TOOLS_RES)
	@[ -d $(TOOLS_DIR) ] || mkdir -p $(TOOLS_DIR)
	$(HOSTCC) $(HOSTCXXFLAGS) -o $(TOOLS_EXE) $(TOOLS_SRCS) $(TOOLS_RES) $(HOSTGUI_LDFLAGS)

$(PNG2ICO): $(PNG2ICO_SRCS)
	@[ -d $(TOOLS_DIR) ] || mkdir -p $(TOOLS_DIR)
	$(HOSTCC) $(HOSTCXXFLAGS) -o $@ $^

$(ICON_ICO): $(PNG2ICO) $(ICON_PNG)
	@[ -d $(TOOLS_DIR) ] || mkdir -p $(TOOLS_DIR)
	@$(PNG2ICO) "$(ICON_PNG)" "$@"

$(TOOLS_RC): $(ICON_ICO) $(TEMPLATE_NRO)
	@[ -d $(TOOLS_DIR) ] || mkdir -p $(TOOLS_DIR)
	@ICON_PATH="$$(cygpath -m "$(ICON_ICO_ABS)" 2>/dev/null || echo "$(ICON_ICO_ABS)")"; \
	  ICON_PATH="$${ICON_PATH//\\//}"; \
	  TEMPLATE_PATH="$$(cygpath -m "$(TEMPLATE_NRO_ABS)" 2>/dev/null || echo "$(TEMPLATE_NRO_ABS)")"; \
	  TEMPLATE_PATH="$${TEMPLATE_PATH//\\//}"; \
	  printf "%s\n" "1 ICON \"$$ICON_PATH\"" > $@; \
	  printf "%s\n" "$(TEMPLATE_RCDATA_ID) RCDATA \"$$TEMPLATE_PATH\"" >> $@

$(TOOLS_RES): $(TOOLS_RC)
	@[ -d $(TOOLS_DIR) ] || mkdir -p $(TOOLS_DIR)
	$(WINDRES) -I. -i $< -O coff -o $@
else
tools:
	@echo "tools target is only supported on Windows."
endif

#---------------------------------------------------------------------------------
standalone: all

ifneq ($(STANDALONE_CART),)
.PHONY: $(STANDALONE_CART)
$(STANDALONE_CART): standalone
endif

%.p8.png: standalone
%.p8: standalone

ifeq ($(REAL8_SWITCH_STANDALONE),1)
$(ROMFS):
	@rm -fr "$(ROMFS)"
	@mkdir -p "$(ROMFS)"
	@if [ -d "$(ROMFS_BASE)" ]; then cp -f "$(ROMFS_BASE)"/* "$(ROMFS)"/; fi
ifneq ($(STANDALONE_CART),)
	@if [ ! -f "$(STANDALONE_CART)" ]; then echo "Standalone cart not found: $(STANDALONE_CART)"; exit 1; fi
	@cp -f "$(STANDALONE_CART)" "$(ROMFS)/$(STANDALONE_CART_NAME)"
endif
endif

#---------------------------------------------------------------------------------
else
.PHONY:	all

DEPENDS := $(OFILES:.o=.d)

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
all: $(OUTPUT).nro

$(OUTPUT).nro: $(OUTPUT).elf $(OUTPUT).nacp

$(OUTPUT).elf: $(OFILES)

$(OFILES_SRC): $(HFILES_BIN)

#---------------------------------------------------------------------------------
# you need a rule like this for each extension you use as binary data
#---------------------------------------------------------------------------------
%.bin.o %_bin.h: %.bin
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPENDS)

#---------------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------------
