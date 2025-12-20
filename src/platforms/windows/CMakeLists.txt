cmake_minimum_required(VERSION 3.10)
project(Real8Windows)

# 1. Standard Settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# --- Handle MSVC Warnings & Definitions ---
if(MSVC)
    add_compile_options(/wd4267 /wd4244 /wd4018)
    add_compile_options(/permissive-)
    
    # 1. Prevent MSVC from assuming Lua is a DLL
    add_compile_definitions(LUA_BUILD_AS_DLL=0)
    
    # 2. Prevent Windows <windows.h> from defining min/max macros
    #    This fixes the syntax errors in fix32.h included by loadlib.c
    add_compile_definitions(NOMINMAX)
    
    # 3. Optional: Disable deprecation warnings (sprintf, etc.) to clean up log
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

add_definitions(-DLUA_CORE)

# 2. Find Packages
find_package(SDL2 REQUIRED)
find_package(lodepng CONFIG REQUIRED)

# 3. Define Sources
file(GLOB CORE_SOURCES "../../core/*.cpp")

# Find z8lua C files
file(GLOB Z8LUA_SOURCES "../../../lib/z8lua/*.cpp")

# --- FILTERING FIX ---
# Exclude lmathlib.c (Incompatible with fix32)
list(FILTER Z8LUA_SOURCES EXCLUDE REGEX ".*lmathlib\\.c$")

# Exclude lua.c (Contains the standalone interpreter 'main' function)
list(FILTER Z8LUA_SOURCES EXCLUDE REGEX ".*lua\\.c$")

# Exclude luac.c (Contains the compiler 'main' function)
list(FILTER Z8LUA_SOURCES EXCLUDE REGEX ".*luac\\.c$")
# ---------------------

# Force z8lua C files to compile as C++ (required for fix32 templates)
set_source_files_properties(${Z8LUA_SOURCES} PROPERTIES LANGUAGE CXX)

# Debug Check
if(NOT Z8LUA_SOURCES)
    message(FATAL_ERROR "No z8lua sources found! Checked path: ${CMAKE_CURRENT_SOURCE_DIR}/../../../lib/z8lua")
else()
    message(STATUS "Found z8lua sources (excluding lmathlib.cpp): ${Z8LUA_SOURCES}")
endif()

set(SOURCES 
    main.cpp
    resources.rc
    ${CORE_SOURCES}
    ${Z8LUA_SOURCES} 
)

# 4. Create Executable
add_executable(Real8Windows WIN32 ${SOURCES})

set_target_properties(Real8Windows PROPERTIES OUTPUT_NAME "Real-8 VM")

# 5. Include Directories
target_include_directories(Real8Windows PRIVATE "../../../lib/z8lua")

# 6. Link Libraries
target_link_libraries(Real8Windows PRIVATE 
    SDL2::SDL2 
    SDL2::SDL2main
    lodepng
)

# 7. Windows Helper: Copy SDL2.dll
if(WIN32)
    add_custom_command(TARGET Real8Windows POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:SDL2::SDL2>
        $<TARGET_FILE_DIR:Real8Windows>)
endif()