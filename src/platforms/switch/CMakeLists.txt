cmake_minimum_required(VERSION 3.10)

# --- 1. TOOLCHAIN SETUP ---
set(DKP_PATH "/c/devkitPro")
#set(CMAKE_TOOLCHAIN_FILE "${DKP_PATH}/cmake/Switch.cmake")

find_program(ELF2NRO elf2nro PATHS "${DKP_PATH}/tools/bin" NO_DEFAULT_PATH)
find_program(NACPTOOL nacptool PATHS "${DKP_PATH}/tools/bin" NO_DEFAULT_PATH)

if(NOT ELF2NRO)
    message(FATAL_ERROR "Could not find elf2nro. Please run: pacman -S switch-tools")
endif()

project(Real8Switch C CXX ASM)

# --- 2. DEFINE MACROS MANUALLY ---
macro(nx_generate_nacp VARname)
    set(oneValueArgs NAME AUTHOR VERSION)
    cmake_parse_arguments(NACP "" "${oneValueArgs}" "" ${ARGN})
    set(${VARname} ${CMAKE_CURRENT_BINARY_DIR}/${NACP_NAME}.nacp)
    add_custom_command(OUTPUT ${${VARname}}
        COMMAND ${NACPTOOL} --create "${NACP_NAME}" "${NACP_AUTHOR}" "${NACP_VERSION}" ${${VARname}}
        COMMENT "Generating NACP info..."
    )
endmacro()

macro(nx_add_nro target)
    set(oneValueArgs NACP ICON)
    cmake_parse_arguments(ARG "" "${oneValueArgs}" "" ${ARGN})
    set(NRO_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${target}.nro)
    if(ARG_NACP)
        set(NACP_FLAG --nacp=${ARG_NACP})
    else()
        set(NACP_FLAG "")
    endif()
    if(ARG_ICON)
        set(ICON_FLAG --icon=${ARG_ICON})
    else()
        set(ICON_FLAG "")
    endif()
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${ELF2NRO} $<TARGET_FILE:${target}> ${NRO_OUTPUT} ${NACP_FLAG} ${ICON_FLAG}
        COMMENT "Converting ELF to NRO..."
    )
endmacro()

# --- 3. STANDARD SETTINGS ---
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_compile_definitions(LUA_USE_POSIX)
add_compile_definitions(__SWITCH__)

list(APPEND CMAKE_PREFIX_PATH "${DKP_PATH}/portlibs/switch")
list(APPEND CMAKE_PREFIX_PATH "${DKP_PATH}/portlibs/switch/lib/cmake")

find_package(SDL2 CONFIG REQUIRED)

# --- 4. FIND PACKAGES ---
find_package(SDL2 REQUIRED)
find_package(ZLIB REQUIRED)
find_package(CURL REQUIRED)

# --- 5. SOURCES ---
file(GLOB CORE_SOURCES "../../core/*.cpp")
# Look for .cpp files since you renamed them
file(GLOB Z8LUA_SOURCES "../../../lib/z8lua/*.cpp")

# Exclude standalone Lua/Luac
list(FILTER Z8LUA_SOURCES EXCLUDE REGEX ".*lmathlib\\.cpp$")
list(FILTER Z8LUA_SOURCES EXCLUDE REGEX ".*lua\\.cpp$")
list(FILTER Z8LUA_SOURCES EXCLUDE REGEX ".*luac\\.cpp$")

# --- 6. GENERATE NACP (Moved UP) ---
# We define this BEFORE add_executable so we can add it as a dependency
nx_generate_nacp(APP_NACP
    NAME "Real-8 VM"
    AUTHOR "Real8 Dev"
    VERSION "1.1.0"
)

set(SOURCES 
    main.cpp
    lodepng.cpp
    ${CORE_SOURCES}
    ${Z8LUA_SOURCES}
    ${APP_NACP}  # <--- CRITICAL FIX: Add NACP to sources to force generation
)

# --- 7. BUILD EXECUTABLE ---
add_executable(Real8Switch ${SOURCES})

target_include_directories(Real8Switch PRIVATE 
    "../../../lib/z8lua"
    ${SDL2_INCLUDE_DIRS}
    .
)

target_link_libraries(Real8Switch PRIVATE 
    SDL2::SDL2 
    CURL::libcurl
    z
    EGL 
    glapi 
    drm_nouveau 
)

# --- 8. GENERATE NRO ---
nx_add_nro(Real8Switch 
    NACP ${APP_NACP}
    ICON "${CMAKE_CURRENT_SOURCE_DIR}/icon.jpg"
)
