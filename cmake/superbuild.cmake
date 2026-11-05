# Superbuild for GC_COMPONENT=all (included from the top level CMakeLists.txt).
#
# Each component is configured as its own CMake project, because they need
# different toolchains: the PC bridge and the server use the host compiler,
# the GAMECOMMS.DLL variants use the N-Gage SDK toolchain (Windows only).
#
# Result (target "dist", part of ALL):
#
#   <build>/dist/PhantomLink/
#       pcbridge/windows/gcbridge.exe, gcbridge.ini
#       server/windows/gamecommsd.exe, gamecommsd.conf
#       ngage/vN/gamecomms.dll                  (N = GC_VARIANTS)
#       ngage/PhantomLink.sis                   (Symbian app with all variants)

include(ExternalProject)

if(NOT DEFINED ENV{NGAGESDK})
    message(FATAL_ERROR "GC_COMPONENT=all builds the N-Gage DLLs too: the environment variable NGAGESDK needs to be defined (see docs/BUILD.md).")
endif()
file(TO_CMAKE_PATH "$ENV{NGAGESDK}" GC_NGAGESDK)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(GC_VARIANTS 1 2 3 4 5 6 7 8 9 10 11 CACHE STRING "DLL variants to build")
string(REPLACE ";" "|" gc_variants_arg "${GC_VARIANTS}")

set(GC_DIST_DIR ${CMAKE_BINARY_DIR}/dist/PhantomLink)
set(GC_STAGE_DIR ${CMAKE_BINARY_DIR}/stage)

# host projects: same compiler as this superbuild
set(gc_host_args
    -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
    -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER})

ExternalProject_Add(pcbridge
    SOURCE_DIR   ${CMAKE_SOURCE_DIR}/pcbridge
    BINARY_DIR   ${CMAKE_BINARY_DIR}/pcbridge
    INSTALL_DIR  ${GC_STAGE_DIR}/pcbridge
    CMAKE_ARGS   ${gc_host_args} -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
    BUILD_ALWAYS ON)

ExternalProject_Add(server
    SOURCE_DIR   ${CMAKE_SOURCE_DIR}/server
    BINARY_DIR   ${CMAKE_BINARY_DIR}/server
    INSTALL_DIR  ${GC_STAGE_DIR}/server
    CMAKE_ARGS   ${gc_host_args} -DGCS_BUILD_TESTS=OFF -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
    BUILD_ALWAYS ON)

# dll/CMakeLists.txt selects the N-Gage toolchain itself (NGAGESDK).
# The DLLs are written to <binary dir>/out/vN/gamecomms.dll and the app
# installer to <binary dir>/app/PhantomLink.sis; no install step.
ExternalProject_Add(dll
    SOURCE_DIR     ${CMAKE_SOURCE_DIR}/dll
    BINARY_DIR     ${CMAKE_BINARY_DIR}/dll
    LIST_SEPARATOR |
    CMAKE_ARGS     -DCMAKE_BUILD_TYPE=Release
                   -DCMAKE_PREFIX_PATH=${GC_NGAGESDK}/sdk/extras/lib/cmake
                   -DGC_VARIANTS=${gc_variants_arg}
                   -DGC_BUILD_APP=ON
    INSTALL_COMMAND ""
    BUILD_ALWAYS   ON)

add_custom_target(dist ALL
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${GC_DIST_DIR}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${GC_DIST_DIR}/pcbridge/windows
    COMMAND ${CMAKE_COMMAND} -E make_directory ${GC_DIST_DIR}/server/windows
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${GC_STAGE_DIR}/pcbridge/bin ${GC_DIST_DIR}/pcbridge/windows
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${GC_STAGE_DIR}/server/bin ${GC_DIST_DIR}/server/windows
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/server/deploy/gamecommsd.conf ${GC_DIST_DIR}/server/windows/
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_BINARY_DIR}/dll/out ${GC_DIST_DIR}/ngage
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/dll/app/PhantomLink.sis ${GC_DIST_DIR}/ngage/
    COMMENT "Staging binaries in ${GC_DIST_DIR}"
    VERBATIM)
add_dependencies(dist pcbridge server dll)
