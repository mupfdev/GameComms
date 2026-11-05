# GameComms endpoint server
#
#   cmake -S server -B build/server -DCMAKE_BUILD_TYPE=Release
#   cmake --build build/server
#
# No third-party dependencies: POSIX sockets / Winsock2 only.

cmake_minimum_required(VERSION 3.14) # target_link_options, CheckPIESupported
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

if(WIN32)
    target_link_libraries(gamecommsd PRIVATE bcrypt) # BCryptGenRandom
endif()

# Exploit mitigations.  Every flag is probed so that older or unusual
# toolchains still build; none of them changes program behaviour.
include(CheckCCompilerFlag)
include(CheckPIESupported OPTIONAL RESULT_VARIABLE GCS_HAVE_PIE_CHECK)
set(GCS_HARDEN_C "")
set(GCS_HARDEN_LD "")
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    foreach(flag -fstack-protector-strong -fstack-clash-protection -fcf-protection=full
                 -Wformat=2 -Werror=format-security -Wvla -fno-strict-overflow -fno-delete-null-pointer-checks)
        string(MAKE_C_IDENTIFIER "GCS_HAS${flag}" var)
        check_c_compiler_flag(${flag} ${var})
        if(${var})
            list(APPEND GCS_HARDEN_C ${flag})
        endif()
    endforeach()
    # _FORTIFY_SOURCE needs optimisation; harmless (ignored) without it
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        list(APPEND GCS_HARDEN_C -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=2)
    endif()
    if(NOT WIN32 AND NOT APPLE)
        list(APPEND GCS_HARDEN_LD -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack)
    endif()
elseif(MSVC)
    list(APPEND GCS_HARDEN_C /GS /sdl /guard:cf)
    list(APPEND GCS_HARDEN_LD /guard:cf /DYNAMICBASE /NXCOMPAT)
endif()
if(GCS_HAVE_PIE_CHECK)
    check_pie_supported(LANGUAGES C)
endif()
foreach(t gc_protocol gcs_core gamecommsd)
    set_property(TARGET ${t} PROPERTY POSITION_INDEPENDENT_CODE ON)
endforeach()

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    foreach(t gc_protocol gcs_core gamecommsd)
        target_compile_options(${t} PRIVATE -Wall -Wextra ${GCS_HARDEN_C})
    endforeach()
elseif(MSVC)
    foreach(t gc_protocol gcs_core gamecommsd)
        target_compile_options(${t} PRIVATE /W3 /D_CRT_SECURE_NO_WARNINGS ${GCS_HARDEN_C})
    endforeach()
endif()
target_link_options(gamecommsd PRIVATE ${GCS_HARDEN_LD})

install(TARGETS gamecommsd RUNTIME DESTINATION bin)

option(GCS_BUILD_TESTS "Build server unit tests" ON)
if(GCS_BUILD_TESTS)
    enable_testing()
    add_executable(test_gcs_core tests/test_core.c)
    target_link_libraries(test_gcs_core PRIVATE gcs_core)
    add_test(NAME server_core COMMAND test_gcs_core)
endif()
