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
    ${EPOC_LIB}/euser.lib)

set(INC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")

set(gamecomms_sources
    "${SRC_DIR}/GameBTComms.cpp"
    "${SRC_DIR}/GameBTCommsNotify.cpp"
    "${SRC_DIR}/SGEDebugLog.cpp")

add_library(gamecomms STATIC ${gamecomms_sources})

build_dll(gamecomms dll ${UID1} ${UID2} ${UID3} "${gamecomms_libs}" "${SRC_DIR}/gamecomms.def")

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
