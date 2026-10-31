# CMake build for the SMVS library and the smvsrecon command-line tool.
#
# Dependencies:
#   vcpkg (vcpkg.json): libjpeg-turbo, libpng, libtiff, mimalloc, gtest (tests)
#   FetchContent:       MVE (util, math and mve libraries)

cmake_minimum_required(VERSION 3.21)
project(SMVS LANGUAGES C CXX)

option(SMVS_BUILD_TESTS "Build the unit tests (requires GTest)" OFF)
option(SMVS_UNSAFE_MATH "Use -funsafe-math-optimizations" OFF)
option(SMVS_NATIVE_ARCH "Optimize for the build machine (-march=native)" OFF)
option(SMVS_USE_HIGHWAY "Use Google Highway (runtime-dispatched SIMD)" ON)
option(SMVS_USE_OPENMP  "Use OpenMP if available" ON)
option(SMVS_USE_MIMALLOC "Use mimalloc for C++ allocations (operator new/delete)" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Executables (and the OpenMP runtime on Windows) in one directory.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
# Debug information is only needed for profiling: configure with
# -DCMAKE_CXX_FLAGS_RELEASE="-O3 -g".
set(CMAKE_CXX_FLAGS_RELEASE "-O3" CACHE STRING "")

find_package(JPEG REQUIRED)
find_package(PNG REQUIRED)
find_package(TIFF REQUIRED)
find_package(Threads REQUIRED)
if(SMVS_USE_HIGHWAY)
    find_package(hwy CONFIG REQUIRED)
endif()
if(SMVS_BUILD_TESTS)
    find_package(GTest CONFIG REQUIRED)
endif()
if(SMVS_USE_MIMALLOC)
    find_package(mimalloc CONFIG REQUIRED)
    if(TARGET mimalloc-static)
        set(SMVS_MIMALLOC_TARGET mimalloc-static)
    else()
        set(SMVS_MIMALLOC_TARGET mimalloc)
    endif()
endif()
if(SMVS_USE_OPENMP)
    if(WIN32)
        # Prefer LLVM's own libomp.lib/libomp.dll (redistributable) over the one
        # shipped with Visual Studio, which imports libomp140.x86_64.dll.
        get_filename_component(llvm_bin_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
        if(EXISTS "${llvm_bin_dir}/../lib/libomp.lib")
            get_filename_component(llvm_omp_lib "${llvm_bin_dir}/../lib/libomp.lib" ABSOLUTE)
            set(OpenMP_libomp_LIBRARY "${llvm_omp_lib}" CACHE FILEPATH "" FORCE)
        endif()
    endif()
    find_package(OpenMP COMPONENTS CXX)
endif()

# ---- MVE ---------------------------------------------------------------------

include(FetchContent)

# MVE provides the util, math and mve libraries. Use
# -DFETCHCONTENT_SOURCE_DIR_MVE=<path> to build against a local checkout.
set(MVE_BUILD_APPS     OFF CACHE BOOL "" FORCE)
set(MVE_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(MVE_USE_HIGHWAY    OFF CACHE BOOL "" FORCE)
set(MVE_USE_MIMALLOC   OFF CACHE BOOL "" FORCE)
set(MVE_UNSAFE_MATH    ${SMVS_UNSAFE_MATH} CACHE BOOL "" FORCE)
set(MVE_NATIVE_ARCH    ${SMVS_NATIVE_ARCH} CACHE BOOL "" FORCE)
set(MVE_USE_OPENMP     ${SMVS_USE_OPENMP}  CACHE BOOL "" FORCE)
FetchContent_Declare(mve
    GIT_REPOSITORY https://github.com/rhiestan/mve.git
    GIT_TAG        e60c25f5703bdb803e192c9e4422f7b5e094ba32)
FetchContent_MakeAvailable(mve)

# ---- Flags shared by all targets -------------------------------------------

add_library(smvs_options INTERFACE)
target_include_directories(smvs_options INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/lib)
target_compile_options(smvs_options INTERFACE -Wall -Wextra -Wundef -pedantic -Wno-sign-compare)
if(SMVS_UNSAFE_MATH)
    target_compile_options(smvs_options INTERFACE -funsafe-math-optimizations)
endif()
if(SMVS_NATIVE_ARCH)
    target_compile_options(smvs_options INTERFACE -march=native)
endif()
if(WIN32)
    target_compile_definitions(smvs_options INTERFACE
        _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN _USE_MATH_DEFINES
        _SILENCE_CXX17_ITERATOR_BASE_CLASS_DEPRECATION_WARNING)
endif()
target_link_libraries(smvs_options INTERFACE
    mve mve_util mve_math Threads::Threads
    JPEG::JPEG PNG::PNG TIFF::TIFF)
if(OpenMP_CXX_FOUND)
    target_link_libraries(smvs_options INTERFACE OpenMP::OpenMP_CXX)
endif()

# Replaces global operator new/delete with mimalloc in an executable.
function(smvs_use_mimalloc target)
    if(SMVS_USE_MIMALLOC)
        target_sources(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/cmake/mimalloc_override.cc)
        target_link_libraries(${target} PRIVATE ${SMVS_MIMALLOC_TARGET})
    endif()
endfunction()

# ---- Targets -----------------------------------------------------------------

file(GLOB smvs_sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/lib/[^_]*.cc)
add_library(smvs STATIC ${smvs_sources})
target_link_libraries(smvs PUBLIC smvs_options)
if(SMVS_USE_HIGHWAY)
    target_link_libraries(smvs PUBLIC hwy::hwy)
    target_compile_definitions(smvs PUBLIC SMVS_USE_HIGHWAY=1)
endif()

add_executable(smvsrecon app/smvsrecon.cc)
target_link_libraries(smvsrecon PRIVATE smvs)
smvs_use_mimalloc(smvsrecon)

add_executable(smvs_simplify tools/simplify.cc)
set_target_properties(smvs_simplify PROPERTIES OUTPUT_NAME simplify)
target_link_libraries(smvs_simplify PRIVATE smvs)
smvs_use_mimalloc(smvs_simplify)

install(TARGETS smvsrecon smvs_simplify RUNTIME DESTINATION bin)

if(SMVS_BUILD_TESTS)
    enable_testing()
    file(GLOB smvs_test_sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/gtest_*.cc)
    add_executable(smvs_tests ${smvs_test_sources})
    target_link_libraries(smvs_tests PRIVATE smvs GTest::gtest GTest::gtest_main)
    add_test(NAME smvs_tests COMMAND smvs_tests)
endif()

# On Windows the LLVM OpenMP runtime is a DLL that has to ship with the tools.
if(WIN32 AND OpenMP_CXX_FOUND)
    get_filename_component(compiler_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    find_file(SMVS_LIBOMP_DLL libomp.dll HINTS "${compiler_dir}" NO_DEFAULT_PATH)
    if(SMVS_LIBOMP_DLL)
        add_custom_target(smvs_copy_libomp ALL
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SMVS_LIBOMP_DLL}" "${CMAKE_BINARY_DIR}")
        add_dependencies(smvsrecon smvs_copy_libomp)
        install(FILES "${SMVS_LIBOMP_DLL}" DESTINATION bin)
    else()
        message(WARNING "libomp.dll not found next to the compiler; copy it manually")
    endif()
endif()
