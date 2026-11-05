# PhantomLink - Symbian application (game selection and bridge setup).
#
# Included from ../dll/CMakeLists.txt, which provides the toolchain, the
# gc_libs list and the per-variant gamecomms_vN_out targets.  Output:
#   <build>/app/PhantomLink.sis
# See docs/BUILD.md.

set(PL_UID3 0x1000a9f0)  # PhantomLink.app UID
string(SUBSTRING ${PL_UID3} 2 -1 PL_UID3_PKG)
set(PL_UID3_PKG 0x${PL_UID3_PKG})

set(PL_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(PL_RES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/res")
set(PL_INC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/inc")

set(pl_sources
    ${PL_SRC_DIR}/PhantomLinkApp.cpp
    ${PL_SRC_DIR}/PhantomLinkAppUi.cpp
    ${PL_SRC_DIR}/PhantomLinkView.cpp
    ${PL_SRC_DIR}/BridgeClient.cpp
    ${PL_SRC_DIR}/GameList.cpp
    ${PL_SRC_DIR}/GameInstaller.cpp
    ${GC_DLL_DIR}/symbian/GcConfig.cpp
    ${GC_DLL_DIR}/symbian/GcTransport.cpp
    ${GC_DLL_DIR}/symbian/bt/BTServiceSearcher.cpp
    ${GC_DLL_DIR}/symbian/bt/MessageServiceSearcher.cpp
    ${GC_DLL_DIR}/symbian/bt/SdpAttributeParser.cpp)

set(pl_libs
    ${EPOC_LIB}/euser.lib
    ${EPOC_LIB}/apparc.lib
    ${EPOC_LIB}/cone.lib
    ${EPOC_LIB}/eikcore.lib
    ${EPOC_LIB}/eikcoctl.lib
    ${EPOC_LIB}/eikdlg.lib
    ${EPOC_LIB}/eikctl.lib
    ${EPOC_LIB}/avkon.lib
    ${EPOC_LIB}/efsrv.lib
    ${EPOC_LIB}/bafl.lib
    ${EPOC_LIB}/esock.lib
    ${EPOC_LIB}/bluetooth.lib
    ${EPOC_LIB}/btextnotifiers.lib
    ${EPOC_LIB}/sdpagent.lib
    ${EPOC_LIB}/sdpdatabase.lib
    ${EPOC_LIB}/charconv.lib
    ${EPOC_LIB}/ws32.lib
    ${EPOC_LIB}/gdi.lib
    ${EPOC_PLATFORM}/gcc/lib/gcc-lib/arm-epoc-pe/2.9-psion-98r2/libgcc.a)

add_library(PhantomLink STATIC ${pl_sources})
target_compile_definitions(PhantomLink PRIVATE
    __DLL__ UID1=${GC_UID1} UID2=0x100039ce UID3=${PL_UID3}
    -D__SYMBIAN32__ -D__GCC32__ -D__EPOC32__ -D__MARM__ -D__MARM_ARMI__ -DNDEBUG -D_UNICODE)
target_include_directories(PhantomLink PRIVATE
    ${PL_SRC_DIR}
    ${PL_INC_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
    ${GC_ROOT}/protocol/include
    ${GC_DLL_DIR}/core
    ${GC_DLL_DIR}/symbian
    ${GC_DLL_DIR}/symbian/bt)
target_compile_options(PhantomLink PRIVATE -O2 -Wall)

# An S60 application is a DLL with UID1 0x1000007a and UID2 0x100039ce.
build_dll(PhantomLink PhantomLink app 0x1000007a 0x100039ce ${PL_UID3} "${pl_libs}")

add_dependencies(PhantomLink_tmp.exp PhantomLink)
add_dependencies(PhantomLink.bas     PhantomLink_tmp.exp)
add_dependencies(PhantomLink.exp     PhantomLink.bas)
add_dependencies(PhantomLink_tmp.lib PhantomLink.exp)
add_dependencies(PhantomLink.map     PhantomLink_tmp.lib)
add_dependencies(PhantomLink.app     PhantomLink.map)

build_resource(${PL_RES_DIR} PhantomLink "")

# res/PhantomLink.mbm is committed; regenerate it after changing the icons
# with: python app/res/mkmbm.py app/res/PhantomLink.mbm icon42x29.bmp
# icon42x29_mask.bmp icon44x44.bmp icon44x44_mask.bmp (run in app/res).
build_aif(${PL_RES_DIR} PhantomLink ${PL_UID3})

# The resource header is needed by the application sources.
add_dependencies(PhantomLink PhantomLink.rsc)

# SIS installer: the application plus all game variants as dlls\gcvN.dll.
# The app's game list refers to every variant, so all of them are required.
set(PL_ALL_VARIANTS 1 2 3 4 5 6 7 8 9 10 11)
foreach(v ${PL_ALL_VARIANTS})
    if(NOT "${v}" IN_LIST GC_VARIANTS)
        message(FATAL_ERROR "PhantomLink.sis needs all DLL variants, but variant ${v} is not in GC_VARIANTS. Build all variants or use -DGC_BUILD_APP=OFF.")
    endif()
endforeach()
set(PL_BIN ${CMAKE_CURRENT_BINARY_DIR})
set(PL_DLL_LINES "")
set(pl_out_targets "")
foreach(v ${PL_ALL_VARIANTS})
    file(TO_NATIVE_PATH "${GC_OUT_DIR}/v${v}/gamecomms.dll" dll_native)
    string(APPEND PL_DLL_LINES "\"${dll_native}\"-\"!:\\System\\Apps\\PhantomLink\\dlls\\gcv${v}.dll\"\n")
    list(APPEND pl_out_targets gamecomms_v${v}_out)
endforeach()
file(TO_NATIVE_PATH "${PL_BIN}" PL_BIN_NATIVE)
set(PL_BIN ${PL_BIN_NATIVE})
configure_file(${PL_RES_DIR}/PhantomLink.pkg.in ${CMAKE_CURRENT_BINARY_DIR}/PhantomLink.pkg @ONLY)

build_sis(${CMAKE_CURRENT_BINARY_DIR} PhantomLink)
add_dependencies(PhantomLink.sis PhantomLink.app PhantomLink.rsc PhantomLink.aif ${pl_out_targets})
