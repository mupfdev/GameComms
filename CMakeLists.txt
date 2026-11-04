# PhantomLink - top level build.
#
# GC_COMPONENT selects what this build tree contains (see CMakePresets.json):
#
#   dll       replacement GAMECOMMS.DLL variants (default, needs NGAGESDK)
#   pcbridge  PC bridge gcbridge(.exe)           (host compiler)
#   server    endpoint server gamecommsd(.exe)   (host compiler)
#   all       superbuild of pcbridge, server and dll, each with its own
#             toolchain, staged into <build>/dist/PhantomLink (Windows only)
#
# Default (DLLs, needs the N-Gage SDK toolchain, environment variable NGAGESDK):
#   cmake -G Ninja -B build
#   cmake --build build
# The DLLs end up in build/dll/out/vN/gamecomms.dll.
#
# With presets:
#   cmake --workflow --preset N-Gage            all DLL variants
#   cmake --workflow --preset pcbridge-windows  gcbridge.exe
#   cmake --workflow --preset server            gamecommsd
#   cmake --workflow --preset all               everything above
#
# Other components keep their own, toolchain independent builds:
#   server/     endpoint server            (cmake -S server -B build/server)
#   pcbridge/   PC bridge                  (cmake -S pcbridge -B build/pcbridge)
#   firmware/   ESP32 bridge (PlatformIO)  (pio run -d firmware)
#   tests/      all host tests             (cmake -S tests -B build/tests)
# See docs/BUILD.md.

cmake_minimum_required(VERSION 3.10)

# gcbridge.exe is linked against the static MSVC runtime; the policy has to
# be set before the first project() to take effect.
if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

set(GC_COMPONENT "dll" CACHE STRING "Component to build: dll, pcbridge, server or all")
set_property(CACHE GC_COMPONENT PROPERTY STRINGS dll pcbridge server all)

if(GC_COMPONENT STREQUAL "dll")
    if(DEFINED ENV{NGAGESDK})
        set(NGAGESDK $ENV{NGAGESDK})
        set(CMAKE_TOOLCHAIN_FILE ${NGAGESDK}/cmake/ngage-toolchain.cmake)
    else()
        message(FATAL_ERROR "The environment variable NGAGESDK needs to be defined (see docs/BUILD.md).")
    endif()

    project(gamecomms C CXX)
    add_subdirectory(dll)

elseif(GC_COMPONENT STREQUAL "pcbridge")
    project(phantomlink_pcbridge C)
    add_subdirectory(pcbridge)

elseif(GC_COMPONENT STREQUAL "server")
    project(phantomlink_server C)
    enable_testing()
    add_subdirectory(server)

elseif(GC_COMPONENT STREQUAL "all")
    project(phantomlink C)
    include(cmake/superbuild.cmake)

else()
    message(FATAL_ERROR "Unknown GC_COMPONENT '${GC_COMPONENT}' (dll, pcbridge, server or all).")
endif()
