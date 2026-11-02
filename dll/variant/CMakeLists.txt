# One GAMECOMMS.DLL variant.  Included once per variant from ../CMakeLists.txt
# with its own binary directory: the GCC 2.9 dlltool writes temporary files
# with fixed names (dc.s, dc.o, ...) into the working directory, so variants
# linked in parallel in the same directory overwrite each other's files.

set(v ${GC_VARIANT_NUMBER})
set(lib gamecomms_v${v})

add_library(${lib} STATIC ${gc_sources})
target_compile_definitions(${lib} PRIVATE
    __DLL__ GC_VARIANT=${v} GC_LINK_QUEUE=16
    -D__SYMBIAN32__ -D__GCC32__ -D__EPOC32__ -D__MARM__ -D__MARM_ARMI__ -DNDEBUG -D_UNICODE)
target_include_directories(${lib} PRIVATE
    ${GC_ROOT}/protocol/include
    ${GC_DLL_DIR}/core
    ${GC_DLL_DIR}/symbian
    ${GC_DLL_DIR}/symbian/bt)
target_compile_options(${lib} PRIVATE -O2 -Wall)

# build_dll_ex expects the .def next to the library
configure_file(${GC_DEF_DIR}/gamecomms_v${v}.def ${CMAKE_CURRENT_BINARY_DIR}/${lib}.def COPYONLY)
build_dll_ex(${lib} ${lib} dll ${GC_UID1} ${GC_UID2} ${GC_UID3} "${gc_libs}" ${CMAKE_CURRENT_BINARY_DIR}/${lib}.def)

# The toolchain chains its steps only through file DEPENDS; make the order
# explicit so a parallel build never starts a step early.
add_dependencies(${lib}_tmp.exp ${lib})
add_dependencies(${lib}.bas     ${lib}_tmp.exp)
add_dependencies(${lib}.exp     ${lib}.bas)
add_dependencies(${lib}_tmp.lib ${lib}.exp)
add_dependencies(${lib}.map     ${lib}_tmp.lib)
add_dependencies(${lib}.dll     ${lib}.map)

add_custom_target(${lib}_out ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory ${GC_OUT_DIR}/v${v}
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/${lib}.dll ${GC_OUT_DIR}/v${v}/gamecomms.dll)
add_dependencies(${lib}_out ${lib}.dll)
