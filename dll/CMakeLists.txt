# GameComms replacement DLLs - one GAMECOMMS.DLL per original variant.
#
# Requires the N-Gage SDK toolchain (https://github.com/ngagesdk/ngage-toolchain):
#
#   set NGAGESDK=C:\path\to\ngage-toolchain
#   cmake -G "Ninja" -S dll -B build\dll
#   cmake --build build\dll
#
# Output: build\dll\out\vN\gamecomms.dll (N = 1..11, see docs/COMPATIBILITY.md)

cmake_minimum_required(VERSION 3.10)

if(DEFINED ENV{NGAGESDK})
    set(NGAGESDK $ENV{NGAGESDK})
    set(CMAKE_TOOLCHAIN_FILE ${NGAGESDK}/cmake/ngage-toolchain.cmake)
else()
    message(FATAL_ERROR "The environment variable NGAGESDK needs to be defined.")
endif()

project(gamecomms_dll C CXX)

set(UID1 0x10000079) # KDynamicLibraryUid (the old repo used the EXE uid 0x1000007a)
set(UID2 0x1000008d) # KSharedLibraryUid
set(UID3 0x101f5ee2) # GAMECOMMS.DLL

set(GC_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(GC_VARIANTS 1 2 3 4 5 6 7 8 9 10 11 CACHE STRING "Variants to build")

set(gc_sources
    ${GC_ROOT}/protocol/src/gc_codec.c
    ${GC_ROOT}/protocol/src/gc_link.c
    ${GC_ROOT}/protocol/src/gc_msg.c
    ${CMAKE_CURRENT_SOURCE_DIR}/core/gc_dev.c
    ${CMAKE_CURRENT_SOURCE_DIR}/symbian/GcConfig.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/symbian/GcTransport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/symbian/GcEngine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/symbian/GcListControl.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/symbian/GameBTComms.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/symbian/GcExports.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/symbian/bt/BTServiceSearcher.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/symbian/bt/MessageServiceSearcher.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/symbian/bt/SdpAttributeParser.cpp)

set(gc_libs
    ${EPOC_LIB}/euser.lib
    ${EPOC_LIB}/efsrv.lib
    ${EPOC_LIB}/esock.lib
    ${EPOC_LIB}/bluetooth.lib
    ${EPOC_LIB}/btextnotifiers.lib
    ${EPOC_LIB}/sdpagent.lib
    ${EPOC_LIB}/sdpdatabase.lib
    ${EPOC_LIB}/cone.lib
    ${EPOC_LIB}/eikcore.lib
    ${EPOC_LIB}/ws32.lib
    ${EPOC_LIB}/gdi.lib
    ${EPOC_LIB}/bafl.lib
    ${EPOC_PLATFORM}/gcc/lib/gcc-lib/arm-epoc-pe/2.9-psion-98r2/libgcc.a)

find_package(Python3 COMPONENTS Interpreter)
set(GC_DEF_DIR ${CMAKE_CURRENT_SOURCE_DIR}/variants/def)
if(Python3_FOUND)
    execute_process(COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/variants/variants.py defs ${GC_DEF_DIR})
endif()

set(GC_DLL_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(GC_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/out)
set(GC_UID1 ${UID1})
set(GC_UID2 ${UID2})
set(GC_UID3 ${UID3})

# Each variant gets its own binary directory (see variant/CMakeLists.txt).
foreach(v ${GC_VARIANTS})
    set(GC_VARIANT_NUMBER ${v})
    add_subdirectory(variant v${v})
endforeach()
