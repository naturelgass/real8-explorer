TARGET_NAME := real8

DEBUG ?= 0

# Size-focused knobs (safe defaults)
OPTIMIZE_SIZE ?= 1        # 1 => -Os, 0 => -O3
USE_LTO ?= 1              # 1 => enable LTO (often smaller)
STATIC_RUNTIMES ?= 0      # 0 => smaller DLL, but needs runtime DLLs present
STRIP_DLL ?= 1            # 1 => strip release DLL after link

SOURCES_CXX :=
SOURCES_C   :=

# 1. Libretro Sources
SOURCES_CXX += libretro.cpp

# 2. Core Sources
CORE_DIR := ../../core
SOURCES_CXX += $(wildcard $(CORE_DIR)/*.cpp)

# 3. z8lua Sources
Z8LUA_DIR := ../../../lib/z8lua
SOURCES_CXX += $(filter-out \
  $(Z8LUA_DIR)/lua.cpp $(Z8LUA_DIR)/luac.cpp $(Z8LUA_DIR)/lmathlib.cpp, \
  $(wildcard $(Z8LUA_DIR)/*.cpp))

SOURCES_CXX += ../../../lib/lodePNG/lodePNG.cpp

INCFLAGS := -I. -I$(CORE_DIR) -I$(Z8LUA_DIR) -I../../../lib/lodePNG

UNAME_S := $(shell uname -s)

ifeq ($(platform),)
platform = unix
ifeq ($(UNAME_S),)
   platform = win
else ifneq ($(findstring MINGW,$(UNAME_S)),)
   platform = win
else ifneq ($(findstring MSYS,$(UNAME_S)),)
   platform = win
else ifneq ($(findstring Darwin,$(UNAME_S)),)
   platform = osx
else ifneq ($(findstring win,$(UNAME_S)),)
   platform = win
endif
endif

LIBS :=

# --- Platform targets / toolchain ---
ifneq (,$(findstring unix,$(platform)))
   TARGET := $(TARGET_NAME)_libretro.so
   fpic := -fPIC
   SHARED := -shared -Wl,--no-undefined -Wl,--version-script=link.T
   STRIP ?= strip
else ifneq (,$(findstring win,$(platform)))
   TARGET := $(TARGET_NAME)_libretro.dll
   CC  ?= x86_64-w64-mingw32-gcc
   CXX ?= x86_64-w64-mingw32-g++
   STRIP ?= x86_64-w64-mingw32-strip
   SHARED := -shared -Wl,--no-undefined exports.def
else ifneq (,$(findstring osx,$(platform)))
   TARGET := $(TARGET_NAME)_libretro.dylib
   fpic := -fPIC
   SHARED := -dynamiclib
   STRIP ?= strip
endif

# --- Common flags ---
CFLAGS   += $(fpic) $(INCFLAGS) -DNOMINMAX
CXXFLAGS += $(fpic) $(INCFLAGS) -DNOMINMAX -std=c++17

# Help the linker drop unused code/data
CFLAGS   += -ffunction-sections -fdata-sections
CXXFLAGS += -ffunction-sections -fdata-sections
LDFLAGS  += -Wl,--gc-sections

# Optional LTO (often reduces size)
ifeq ($(USE_LTO),1)
  CFLAGS   += -flto
  CXXFLAGS += -flto
  LDFLAGS  += -flto
endif

# Debug / Release
ifeq ($(DEBUG),1)
   CFLAGS   += -O0 -g
   CXXFLAGS += -O0 -g
else
   ifeq ($(OPTIMIZE_SIZE),1)
      CFLAGS   += -Os -DNDEBUG
      CXXFLAGS += -Os -DNDEBUG
   else
      CFLAGS   += -O3 -DNDEBUG
      CXXFLAGS += -O3 -DNDEBUG
   endif
endif

# Static runtimes make DLL bigger (default OFF)
ifeq ($(STATIC_RUNTIMES),1)
  ifneq (,$(findstring win,$(platform)))
    SHARED += -static-libgcc -static-libstdc++
  endif
endif

OBJECTS := $(SOURCES_CXX:.cpp=.o) $(SOURCES_C:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(fpic) $(SHARED) -o $@ $(OBJECTS) $(LIBS) $(LDFLAGS)
ifeq ($(DEBUG),0)
ifeq ($(STRIP_DLL),1)
	$(STRIP) --strip-unneeded $@
endif
endif

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

%.o: %.c
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJECTS) $(TARGET)

.PHONY: clean
