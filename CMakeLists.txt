cmake_minimum_required(VERSION 2.8.12) # ubuntu 14 version

set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

set(VCPKG_DEPS protobuf openssl libwebsockets)

include(Set-Toolchain-vcpkg)

project(BitcoinArmory C CXX)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "release build with debug info" FORCE)
endif()

if(POLICY CMP0077)
    cmake_policy(SET CMP0077 NEW)
endif()

include(ArmorySupport)
include(PrettyCompilerColors)

use_cxx11()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# use ccache if available, and not already enabled on the command line
# but not with ninja and msys ccache on msys2
if(NOT (WIN32 AND (NOT $ENV{MSYSTEM} STREQUAL "") AND CMAKE_GENERATOR STREQUAL Ninja))
    if(NOT CMAKE_CXX_COMPILER_LAUNCHER AND NOT CMAKE_C_COMPILER_LAUNCHER)
        find_program(CCACHE_FOUND ccache)
        if(CCACHE_FOUND)
            set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ccache)
            set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK    ccache)
        endif(CCACHE_FOUND)
    endif()
endif()

# build for host CPU if desired (default yes)
option(WITH_HOST_CPU_FEATURES "support the CPU features of the build host, gcc only" ON)

if(WITH_HOST_CPU_FEATURES AND CMAKE_CXX_COMPILER_ID STREQUAL GNU)
    check_x86_cpu_features()

    add_compile_options(-march=native ${X86_CPU_FEATURES_COMPILER_FLAGS})
endif()

if(MSVC)
    add_compile_definitions(NOMINMAX _WINSOCKAPI_)
endif()

add_subdirectory(cppForSwig)
