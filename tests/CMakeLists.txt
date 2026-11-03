# Host build of everything that can run on a PC: protocol library,
# server, bridge core, N-Gage engine core and all automated tests.
#
#   cmake -S tests -B build/tests && cmake --build build/tests && ctest --test-dir build/tests
#
# Option GC_SANITIZE=ON builds with AddressSanitizer + UBSan (GCC/Clang).

cmake_minimum_required(VERSION 3.10)
project(gamecomms_tests C)
set(CMAKE_C_STANDARD 99)
enable_testing()

option(GC_SANITIZE "Build with ASan/UBSan" OFF)
set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -g)
    if(GC_SANITIZE)
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
    endif()
endif()
if(NOT WIN32)
    add_compile_definitions(_POSIX_C_SOURCE=200809L)
endif()

add_library(gc_protocol STATIC ${ROOT}/protocol/src/gc_codec.c ${ROOT}/protocol/src/gc_link.c ${ROOT}/protocol/src/gc_msg.c)
target_include_directories(gc_protocol PUBLIC ${ROOT}/protocol/include)

add_library(gc_server_core STATIC ${ROOT}/server/src/gcs_core.c)
target_include_directories(gc_server_core PUBLIC ${ROOT}/server/src)
target_link_libraries(gc_server_core PUBLIC gc_protocol)

add_library(gc_bridge_core STATIC ${ROOT}/firmware/src/bridge_core.c)
target_include_directories(gc_bridge_core PUBLIC ${ROOT}/firmware/src)
target_link_libraries(gc_bridge_core PUBLIC gc_protocol)

add_library(gc_dev_core STATIC ${ROOT}/dll/core/gc_dev.c)
target_include_directories(gc_dev_core PUBLIC ${ROOT}/dll/core)
target_link_libraries(gc_dev_core PUBLIC gc_protocol)

# the core sources must stay plain C89 (N-Gage GCC 2.9 / C++98 toolchain)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    set_source_files_properties(
        ${ROOT}/protocol/src/gc_codec.c ${ROOT}/protocol/src/gc_link.c ${ROOT}/protocol/src/gc_msg.c
        ${ROOT}/dll/core/gc_dev.c
        PROPERTIES COMPILE_OPTIONS "-std=c89;-pedantic;-Wno-long-long")
endif()

foreach(t codec link msg)
    add_executable(test_${t} ${ROOT}/protocol/tests/test_${t}.c)
    target_link_libraries(test_${t} gc_protocol)
    add_test(NAME protocol_${t} COMMAND test_${t})
endforeach()

add_executable(test_server_core ${ROOT}/server/tests/test_core.c)
target_link_libraries(test_server_core gc_server_core)
add_test(NAME server_core COMMAND test_server_core)

add_executable(test_e2e ${ROOT}/tests/sim/sim.c ${ROOT}/tests/sim/test_e2e.c)
target_link_libraries(test_e2e gc_dev_core gc_bridge_core gc_server_core)
add_test(NAME e2e_simulated COMMAND test_e2e)

# real sockets: start gamecommsd, run gcsim against it
add_executable(gamecommsd ${ROOT}/server/src/gcs_main.c)
target_link_libraries(gamecommsd gc_server_core)
add_executable(gcsim ${ROOT}/tests/net/gcsim.c)
target_link_libraries(gcsim gc_dev_core gc_bridge_core)
# PC bridge (without Bluetooth outside Windows; tested with TCP devices)
if(WIN32)
    add_executable(gcbridge ${ROOT}/pcbridge/src/pcb_main.c ${ROOT}/pcbridge/src/pcb_bt_win.c
                   ${ROOT}/pcbridge/src/pcb_com_win.c)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(gcbridge ${ROOT}/pcbridge/src/pcb_main.c ${ROOT}/pcbridge/src/pcb_bt_linux.c
                   ${ROOT}/pcbridge/src/pcb_com_posix.c)
else()
    add_executable(gcbridge ${ROOT}/pcbridge/src/pcb_main.c ${ROOT}/pcbridge/src/pcb_bt_none.c
                   ${ROOT}/pcbridge/src/pcb_com_posix.c)
endif()
target_include_directories(gcbridge PRIVATE ${ROOT}/pcbridge/src)
target_link_libraries(gcbridge gc_bridge_core)
if(WIN32)
    target_link_libraries(gamecommsd ws2_32)
    target_link_libraries(gcsim ws2_32)
    target_link_libraries(gcbridge ws2_32)
endif()

find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
    add_test(NAME e2e_network COMMAND ${Python3_EXECUTABLE} ${ROOT}/tests/net/run_net_test.py
             $<TARGET_FILE:gamecommsd> $<TARGET_FILE:gcsim>)
    add_test(NAME pcbridge_network COMMAND ${Python3_EXECUTABLE} ${ROOT}/tests/net/run_pcbridge_test.py
             $<TARGET_FILE:gamecommsd> $<TARGET_FILE:gcbridge> $<TARGET_FILE:gcsim>)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        # BlueZ backend against a fake BlueZ on a private D-Bus daemon (skipped without dbus-daemon / python dbus)
        add_test(NAME pcbridge_bluez COMMAND ${Python3_EXECUTABLE} ${ROOT}/tests/net/run_bluez_test.py
                 $<TARGET_FILE:gamecommsd> $<TARGET_FILE:gcbridge> $<TARGET_FILE:gcsim>)
        set_tests_properties(pcbridge_bluez PROPERTIES SKIP_RETURN_CODE 77)
    endif()
    add_test(NAME dll_variants COMMAND ${Python3_EXECUTABLE} ${ROOT}/tests/compat/check_variants.py)
endif()
