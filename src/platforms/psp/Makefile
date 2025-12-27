TARGET = REAL8
.DEFAULT_GOAL := all

SRCDIRS = . ../../core ../../hal ../../../lib/z8lua ../../../lib/lodePNG
INCDIR = . ../../core ../../hal ../../../lib/z8lua ../../../lib/lodePNG

CPPFILES := $(foreach dir,$(SRCDIRS),$(notdir $(wildcard $(dir)/*.cpp)))
CFILES := $(foreach dir,$(SRCDIRS),$(notdir $(wildcard $(dir)/*.c)))
WALLPAPER_PNG = wallpaper.png
WALLPAPER_OBJ = wallpaper_png.o

OBJS := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(WALLPAPER_OBJ)

VPATH := $(foreach dir,$(SRCDIRS),$(CURDIR)/$(dir))

CFLAGS = -O2 -G0 -Wall -Wno-sign-compare -D__PSP__ -DLUA_32BITS
CXXFLAGS = $(CFLAGS) -fexceptions -std=gnu++17
ASFLAGS = $(CFLAGS)

LIBS = -lpspgu -lpspgum -lpspge -lpspaudio -lpspctrl -lpspdisplay -lpsppower -lpspdebug -lstdc++ -lm

PSP_EBOOT_TITLE = REAL-8 Explorer

PSPSDK := $(shell psp-config --pspsdk-path)
PSPDEV := $(shell psp-config --pspdev-path)
ifeq ($(PSPDEV),)
PSPDEV := $(dir $(dir $(PSPSDK)))
endif
PSP_LD := $(PSPDEV)/bin/psp-ld

EXTRA_TARGETS    = EBOOT.PBP
PSP_EBOOT_ICON   = ICON0.PNG
# optional extras:
PSP_EBOOT_PIC1 = PIC1.PNG
PSP_EBOOT_PIC0 = PIC0.PNG

$(WALLPAPER_OBJ): $(WALLPAPER_PNG)
	$(PSP_LD) -r -b binary -o $(WALLPAPER_OBJ) $(WALLPAPER_PNG)

include $(PSPSDK)/lib/build.mak
