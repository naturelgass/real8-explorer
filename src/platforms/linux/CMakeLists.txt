cmake_minimum_required(VERSION 3.10)
project(Real8Linux)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_compile_definitions(LUA_USE_POSIX)
#add_definitions(-DLUA_CORE)

find_package(SDL2 REQUIRED)

file(GLOB CORE_SOURCES "../../core/*.cpp")
file(GLOB Z8LUA_SOURCES "../../../lib/z8lua/*.cpp")

list(FILTER Z8LUA_SOURCES EXCLUDE REGEX ".*lmathlib\\.cpp$")
list(FILTER Z8LUA_SOURCES EXCLUDE REGEX ".*lua\\.cpp$")
list(FILTER Z8LUA_SOURCES EXCLUDE REGEX ".*luac\\.cpp$")

set_source_files_properties(${Z8LUA_SOURCES} PROPERTIES LANGUAGE CXX)

set(SOURCES
    main.cpp
    ../../../lib/lodePNG/lodePNG.cpp
    ${CORE_SOURCES}
    ${Z8LUA_SOURCES}
)

add_executable(Real8Linux ${SOURCES})

target_include_directories(Real8Linux PRIVATE
    "../../../lib/z8lua"
    "../../../lib/lodePNG"
)

target_link_libraries(Real8Linux PRIVATE
    SDL2::SDL2
    dl
)
