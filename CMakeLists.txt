cmake_minimum_required(VERSION 3.00)

if(BUILD_ON_ALT_PLATFORM)
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/CMakeLists.txt)
    return()
elseif(DEFINED ENV{NGAGESDK})
    set(NGAGESDK $ENV{NGAGESDK})
    set(CMAKE_TOOLCHAIN_FILE ${NGAGESDK}/cmake/ngage-toolchain.cmake)
else()
    message(FATAL_ERROR "The environment variable NGAGESDK needs to be defined.")
endif()

project(gamecomms CXX)

set(UID1 0x1000007a) # KDynamicLibraryUidValue, e32uid.h
set(UID2 0x1000008d)
set(UID3 0x101f5ee2) # gamecomms.dll UID

set(GCC_COMN_DEFS -D__SYMBIAN32__ -D__GCC32__ -D__EPOC32__ -D__MARM__ -D__MARM_ARMI__)
set(GCC_MODE_DEFS -DNDEBUG -D_UNICODE)
set(GCC_DEFS      ${GCC_COMN_DEFS} ${GCC_MODE_DEFS})

set(gamecomms_libs
    ${EPOC_PLATFORM}/gcc/lib/gcc-lib/arm-epoc-pe/2.9-psion-98r2/libgcc.a
    ${EPOC_LIB}/egcc.lib
    ${EPOC_LIB}/euser.lib
    ${EPOC_LIB}/estlib.lib
    ${EPOC_LIB}/ws32.lib
    ${EPOC_LIB}/hal.lib
    ${EPOC_LIB}/efsrv.lib
    ${EPOC_LIB}/scdv.lib
    ${EPOC_LIB}/gdi.lib)

set(INC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")

set(gamecomms_sources
    "${SRC_DIR}/GameBTComms.cpp")

add_library(gamecomms STATIC ${gamecomms_sources})

build_dll(gamecomms dll ${UID1} ${UID2} ${UID3} "${gamecomms_libs}")

target_compile_definitions(
    gamecomms
    PUBLIC
    __DLL__
    FUNCTION_NAME=__FUNCTION__
    STBI_NO_THREAD_LOCALS
    ${GCC_DEFS}
    UID1=${UID1}
    UID2=${UID2}
    UID3=${UID3})

target_compile_options(
    gamecomms
    PUBLIC
    -Wall
    -O3)

target_include_directories(
    gamecomms
    PUBLIC
    ${INC_DIR})
