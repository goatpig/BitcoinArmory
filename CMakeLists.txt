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
    SET(CMAKE_FIND_LIBRARY_PREFIXES "")
    SET(CMAKE_FIND_LIBRARY_SUFFIXES .a .so .dll)
    SET(CMAKE_LIBRARY_HINTS /usr/lib /usr/include /ucrt64/lib /ucrt64/include)
ENDIF ()

## lmdb ##
FIND_LIBRARY (LMDB_LIB liblmdb HINTS ${CMAKE_LIBRARY_HINTS})
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

FIND_LIBRARY (LIBWEBSOCKETS_LIB libwebsockets.dll HINTS ${CMAKE_LIBRARY_HINTS})
IF (NOT LIBWEBSOCKETS_LIB)
    MESSAGE(FATAL_ERROR "Could not find WS lib")
ENDIF ()
SET(LIBWEBSOCKETS_INCLUDE_DIRS ${WITH_LWS}/include)

## capnproto ##
FIND_LIBRARY (CAPNPROTO_LIB libcapnp.dll HINTS ${CMAKE_LIBRARY_HINTS})
IF (NOT CAPNPROTO_LIB)
    MESSAGE(FATAL_ERROR "Could not find capnproto lib")
ENDIF ()

## gtest ##
IF (ENABLE_TESTS)
    FIND_LIBRARY(GTEST_LIB libgtest_main.dll HINTS ${CMAKE_LIBRARY_HINTS})
    IF (NOT GTEST_LIB)
        MESSAGE(FATAL_ERROR "Could not find gtest lib")
    ENDIF ()
    MESSAGE ("gtest lib: ${GTEST_LIB}")
ENDIF (ENABLE_TESTS)

## winsock ##
FIND_LIBRARY (WINSOCK_LIB ws2_32)
IF (NOT WINSOCK_LIB)
    MESSAGE (FATAL_ERROR "Could not find Winsock2 lib")
ENDIF ()

PROJECT(BitcoinArmory C CXX)

IF (NOT CMAKE_BUILD_TYPE)
    SET(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Release with debug info version" FORCE)
ENDIF ()

include(GCCToolchain)
include(CompilerColorDiagnostics)
include(CompilerWarnings)

IF (CMAKE_SYSTEM_NAME STREQUAL "MSYS")
    SET(MINGW 1)
ENDIF ()

SET(CMAKE_POSITION_INDEPENDENT_CODE ON)
IF (MSVC OR MINGW)
    add_definitions(-DNOMINMAX -D_WINSOCKAPI_)
ENDIF ()

# this is the main project
add_subdirectory(cppForSwig)
