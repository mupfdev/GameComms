# GameComms PC bridge (gcbridge.exe)
#
#   cmake -S pcbridge -B build/pcbridge
#   cmake --build build/pcbridge --config Release
#
# Linux: any C compiler; Bluetooth through BlueZ over D-Bus, no libraries
# needed.  Windows: Visual Studio or MinGW, linked statically, so
# gcbridge.exe can be copied to any Windows PC.  Other systems build it
# without Bluetooth (test mode only).

cmake_minimum_required(VERSION 3.15)
cmake_policy(SET CMP0091 NEW)
project(gamecomms_pcbridge C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(GC_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

set(sources
    ${GC_ROOT}/protocol/src/gc_codec.c
    ${GC_ROOT}/protocol/src/gc_link.c
    ${GC_ROOT}/protocol/src/gc_msg.c
    ${GC_ROOT}/firmware/src/bridge_core.c
    src/pcb_main.c)
if(WIN32)
    list(APPEND sources src/pcb_bt_win.c src/pcb_com_win.c)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND sources src/pcb_bt_linux.c src/pcb_com_posix.c)
else()
    list(APPEND sources src/pcb_bt_none.c src/pcb_com_posix.c)
endif()

add_executable(gcbridge ${sources})
target_include_directories(gcbridge PRIVATE ${GC_ROOT}/protocol/include ${GC_ROOT}/firmware/src src)

if(WIN32)
    target_link_libraries(gcbridge PRIVATE ws2_32 bcrypt)
    if(MINGW)
        target_link_options(gcbridge PRIVATE -static)
    endif()
else()
    target_compile_definitions(gcbridge PRIVATE _POSIX_C_SOURCE=200809L)
endif()

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(gcbridge PRIVATE -Wall -Wextra)
elseif(MSVC)
    target_compile_options(gcbridge PRIVATE /W3 /D_CRT_SECURE_NO_WARNINGS /D_WINSOCK_DEPRECATED_NO_WARNINGS)
endif()

install(TARGETS gcbridge RUNTIME DESTINATION bin)
install(FILES gcbridge.ini DESTINATION bin)
