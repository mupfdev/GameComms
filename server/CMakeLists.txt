# GameComms endpoint server
#
#   cmake -S server -B build/server -DCMAKE_BUILD_TYPE=Release
#   cmake --build build/server
#
# No third-party dependencies: POSIX sockets / Winsock2 only.

cmake_minimum_required(VERSION 3.10)
project(gamecomms_server C)

set(GC_PROTOCOL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../protocol)

add_library(gc_protocol STATIC
    ${GC_PROTOCOL_DIR}/src/gc_codec.c
    ${GC_PROTOCOL_DIR}/src/gc_link.c
    ${GC_PROTOCOL_DIR}/src/gc_msg.c)
target_include_directories(gc_protocol PUBLIC ${GC_PROTOCOL_DIR}/include)

add_library(gcs_core STATIC src/gcs_core.c)
target_link_libraries(gcs_core PUBLIC gc_protocol)
target_include_directories(gcs_core PUBLIC src)

add_executable(gamecommsd src/gcs_main.c)
target_link_libraries(gamecommsd PRIVATE gcs_core)
if(WIN32)
    target_link_libraries(gamecommsd PRIVATE ws2_32)
endif()

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    foreach(t gc_protocol gcs_core gamecommsd)
        target_compile_options(${t} PRIVATE -Wall -Wextra)
    endforeach()
elseif(MSVC)
    foreach(t gc_protocol gcs_core gamecommsd)
        target_compile_options(${t} PRIVATE /W3 /D_CRT_SECURE_NO_WARNINGS)
    endforeach()
endif()

install(TARGETS gamecommsd RUNTIME DESTINATION bin)

option(GCS_BUILD_TESTS "Build server unit tests" ON)
if(GCS_BUILD_TESTS)
    enable_testing()
    add_executable(test_gcs_core tests/test_core.c)
    target_link_libraries(test_gcs_core PRIVATE gcs_core)
    add_test(NAME server_core COMMAND test_gcs_core)
endif()
