CMAKE_MINIMUM_REQUIRED(VERSION 3.5) # ubuntu 16 version
SET(CMAKE_CXX_STANDARD 17)

SET(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

OPTION(ENABLE_TESTS "enable building of tests" OFF)
SET(WITH_LIBBTC "../libbtc" CACHE STRING "path to libbtc")
SET(WITH_LWS "../libwebsockets" CACHE STRING "path to libwebsockets")

IF (WIN32)
    INCLUDE(Set-Toolchain-vcpkg)
    SET(VCPKG_DEPS capnproto libwebsockets lmdb mpir)
ENDIF ()

IF (ENABLE_TESTS)
    LIST(APPEND VCPKG_DEPS gtest)
ENDIF ()

#### find libraries ####

IF (WIN32 OR MSVC)
    SET(CMAKE_FIND_LIBRARY_SUFFIXES .lib)
ELSEIF (UNIX)
    SET(CMAKE_FIND_LIBRARY_SUFFIXES .a .so)
    SET(CMAKE_FIND_LIBRARY_PREFIXES lib)
    SET(CMAKE_LIBRARY_HINTS /usr/lib /usr/include)
ENDIF ()

## gmp ##
FIND_PATH (GMP_INCLUDE_DIR gmp.h HINTS ${CMAKE_LIBRARY_HINTS})

## lmdb ##
FIND_LIBRARY (LMDB_LIB lmdb HINTS ${CMAKE_LIBRARY_HINTS})
IF (NOT LMDB_LIB)
    MESSAGE(FATAL_ERROR "Could not find LMDB lib")
ENDIF ()

## libbtc ##
IF (IS_ABSOLUTE ${WITH_LIBBTC})
# do nothing
ELSE ()
    SET(WITH_LIBBTC ${CMAKE_SOURCE_DIR}/${WITH_LIBBTC})
ENDIF ()

FIND_LIBRARY (LIBBTC_LIB libbtc btc HINTS ${WITH_LIBBTC}/.libs ${WITH_LIBBTC}/lib ${WITH_LIBBTC}/build
    ${WITH_LIBBTC}/build/Debug ${WITH_LIBBTC}/build/Release
)
IF (NOT LIBBTC_LIB)
    MESSAGE(FATAL_ERROR "Could not find libbtc")
ENDIF ()
SET(LIBBTC_INCLUDE_DIR ${WITH_LIBBTC})

## lws ##
IF (IS_ABSOLUTE ${WITH_LWS})
    # do nothing
ELSE ()
    SET(WITH_LWS ${CMAKE_SOURCE_DIR}/${WITH_LWS})
ENDIF ()

FIND_LIBRARY (LIBWEBSOCKETS_LIB websockets HINTS ${WITH_LWS}/lib ${WITH_LWS}/build)
IF (NOT LIBWEBSOCKETS_LIB)
    MESSAGE(FATAL_ERROR "Could not find WS lib")
ENDIF ()
SET(LIBWEBSOCKETS_INCLUDE_DIRS ${WITH_LWS}/include)

## capnproto ##
FIND_LIBRARY (CAPNPROTO_LIB capnp HINTS ${CMAKE_LIBRARY_HINTS})
IF (NOT CAPNPROTO_LIB)
    MESSAGE(FATAL_ERROR "Could not find capnproto lib")
ENDIF ()

## gtest ##
IF (ENABLE_TESTS)
    FIND_LIBRARY(GTEST_LIB gtest HINTS ${CMAKE_LIBRARY_HINTS})
    IF (NOT GTEST_LIB)
        MESSAGE(FATAL_ERROR "Could not find gtest lib")
    ENDIF ()
ENDIF (ENABLE_TESTS)

PROJECT(BitcoinArmory C CXX)

IF (NOT CMAKE_BUILD_TYPE)
    SET(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Release with debug info version" FORCE)
ENDIF ()

include(ArmorySupport)
include(GCCToolchain)
include(LLVMToolchain)
include(LTO)
include(CompilerColorDiagnostics)
include(CompilerWarnings)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    add_definitions(-DNOMINMAX -D_WINSOCKAPI_)
endif()

# this is the main project
add_subdirectory(cppForSwig)
