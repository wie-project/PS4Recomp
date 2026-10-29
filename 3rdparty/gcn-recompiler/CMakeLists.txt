# SPDX-FileCopyrightText: Copyright 2026 PS4Recomp contributors
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Standalone build of shadPS4's GCN shader recompiler (see README.md).

cmake_minimum_required(VERSION 3.20)
project(gcn_recompiler CXX C)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
if (APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES arm64)
endif()

option(GCN_BUILD_MSL "Build the SPIR-V -> MSL helper (gcn_msl.cpp) against SPIRV-Cross" ON)
option(GCN_BUILD_TOOLS "Build the gcn_test host tool" ON)
set(GCN_SPIRV_CROSS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../spirv-cross" CACHE PATH "SPIRV-Cross source dir (with prebuilt libspirv-cross.a)")

set(EXT ${CMAKE_CURRENT_SOURCE_DIR}/externals)

file(GLOB_RECURSE GCN_RECOMPILER_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/shader_recompiler/*.cpp)
file(GLOB SIRIT_SOURCES CONFIGURE_DEPENDS
    ${EXT}/sirit/src/*.cpp ${EXT}/sirit/src/instructions/*.cpp)

set(GCN_SOURCES
    ${GCN_RECOMPILER_SOURCES}
    ${SIRIT_SOURCES}
    ${EXT}/fmt/src/format.cc
    src/video_core/amdgpu/pixel_format.cpp
    src/compat/shim.cpp
    src/api/gcn_recompiler.cpp
    src/api/gcn_msl.cpp
    src/api/gcn_shader_cache.cpp
    src/api/gcn_rectlist.cpp
)

# Build identity for persistent shader caches (gcn_shader_cache_id): a hash of every source that
# influences the translation output, regenerated whenever one of them changes.
file(GLOB_RECURSE GCN_ID_INPUTS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h
    ${EXT}/sirit/src/*.cpp ${EXT}/sirit/src/*.h ${EXT}/sirit/include/*.h)
file(GLOB GCN_ID_SPIRV_CROSS_INPUTS CONFIGURE_DEPENDS
    ${GCN_SPIRV_CROSS_DIR}/spirv_*.cpp ${GCN_SPIRV_CROSS_DIR}/spirv_*.hpp
    ${GCN_SPIRV_CROSS_DIR}/libspirv-cross.a)
list(APPEND GCN_ID_INPUTS ${GCN_ID_SPIRV_CROSS_INPUTS})
set(GCN_ID_LIST ${CMAKE_CURRENT_BINARY_DIR}/gcn_build_id_inputs.txt)
string(REPLACE ";" "\n" GCN_ID_LIST_CONTENT "${GCN_ID_INPUTS}")
file(CONFIGURE OUTPUT ${GCN_ID_LIST} CONTENT "${GCN_ID_LIST_CONTENT}\n")
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/gcn_build_id.h
    COMMAND ${CMAKE_COMMAND} -DLIST=${GCN_ID_LIST} -DOUT=${CMAKE_CURRENT_BINARY_DIR}/gcn_build_id.h
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/gcn_build_id.cmake
    DEPENDS ${GCN_ID_INPUTS} ${GCN_ID_LIST} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/gcn_build_id.cmake
    COMMENT "Computing the gcn-recompiler build ID"
    VERBATIM)
list(APPEND GCN_SOURCES ${CMAKE_CURRENT_BINARY_DIR}/gcn_build_id.h)

add_library(gcn_recompiler STATIC ${GCN_SOURCES})
target_include_directories(gcn_recompiler
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_BINARY_DIR}
            ${EXT}/fmt/include
            ${EXT}/magic_enum/include
            ${EXT}/boost
            ${EXT}/sirit/include
            ${EXT}/sirit/src
            ${EXT}/sirit/externals/SPIRV-Headers/include)
target_compile_definitions(gcn_recompiler PRIVATE FMT_USE_NONTYPE_TEMPLATE_ARGS=0)
target_compile_options(gcn_recompiler PRIVATE
    -Wno-unused-parameter -Wno-invalid-offsetof -Wno-unknown-attributes
    -Wno-deprecated-declarations -Wno-ignored-attributes -fvisibility=hidden)
if (GCN_BUILD_MSL)
    target_include_directories(gcn_recompiler PRIVATE ${GCN_SPIRV_CROSS_DIR})
    target_compile_definitions(gcn_recompiler PRIVATE GCN_HAVE_MSL=1)
endif()

if (GCN_BUILD_TOOLS)
    enable_language(OBJCXX)
    add_executable(gcn_test tools/gcn_test.mm)
    target_link_libraries(gcn_test PRIVATE gcn_recompiler "-framework Foundation" "-framework Metal")
    target_compile_options(gcn_test PRIVATE -fobjc-arc)
    if (GCN_BUILD_MSL)
        target_link_libraries(gcn_test PRIVATE ${GCN_SPIRV_CROSS_DIR}/libspirv-cross.a)
    endif()
    # Debugging disassembler for dumped GCN programs (uses the library's internal decoder).
    add_executable(gcn_dis tools/gcn_dis.cpp)
    target_link_libraries(gcn_dis PRIVATE gcn_recompiler)
    target_include_directories(gcn_dis PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${EXT}/fmt/include
                               ${EXT}/magic_enum/include ${EXT}/boost)
    target_compile_definitions(gcn_dis PRIVATE FMT_USE_NONTYPE_TEMPLATE_ARGS=0)
    # C-only smoke test proving the API is callable from C.
    add_executable(gcn_c_smoke tools/c_smoke.c)
    target_link_libraries(gcn_c_smoke PRIVATE gcn_recompiler)
    if (GCN_BUILD_MSL)
        target_link_libraries(gcn_c_smoke PRIVATE ${GCN_SPIRV_CROSS_DIR}/libspirv-cross.a)
    endif()
    set_target_properties(gcn_c_smoke PROPERTIES LINKER_LANGUAGE C)
    target_link_options(gcn_c_smoke PRIVATE -lc++)
endif()
