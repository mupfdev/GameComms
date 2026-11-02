# GameComms - top level build of the replacement GAMECOMMS.DLL variants.
#
# This needs the N-Gage SDK toolchain (environment variable NGAGESDK):
#   cmake -G Ninja -B build
#   cmake --build build
# The DLLs end up in build/dll/out/vN/gamecomms.dll.
#
# Other components have their own, toolchain independent builds:
#   server/     endpoint server            (cmake -S server -B build/server)
#   firmware/   ESP32 bridge (PlatformIO)  (pio run -d firmware)
#   tests/      all host tests             (cmake -S tests -B build/tests)
# See docs/BUILD.md.

cmake_minimum_required(VERSION 3.10)

if(DEFINED ENV{NGAGESDK})
    set(NGAGESDK $ENV{NGAGESDK})
    set(CMAKE_TOOLCHAIN_FILE ${NGAGESDK}/cmake/ngage-toolchain.cmake)
else()
    message(FATAL_ERROR "The environment variable NGAGESDK needs to be defined (see docs/BUILD.md).")
endif()

project(gamecomms C CXX)
add_subdirectory(dll)
