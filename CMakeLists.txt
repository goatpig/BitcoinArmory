CMAKE_MINIMUM_REQUIRED(VERSION 3.5) # ubuntu 16 version
SET(CMAKE_CXX_STANDARD 17)

SET(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

OPTION(ENABLE_TESTS "enable building of tests" OFF)
SET(THIRD_PARTY_PATH ".." CACHE STRING "path 3rd party libs")

#### detect build env ####
PROJECT(BitcoinArmory C CXX)
IF (CMAKE_SYSTEM_NAME STREQUAL "MSYS")
    SET(MINGW 1)
ENDIF ()

#### find libraries ####

IF (MINGW)
    SET(CMAKE_FIND_LIBRARY_PREFIXES "")
    SET(CMAKE_FIND_LIBRARY_SUFFIXES .a .so .dll)
    SET(CMAKE_LIBRARY_HINTS /usr/lib /usr/include /ucrt64/lib /ucrt64/include)
ELSE ()
    SET(CMAKE_FIND_LIBRARY_PREFIXES "lib")
    SET(CMAKE_FIND_LIBRARY_SUFFIXES .a .so)
    SET(CMAKE_LIBRARY_HINTS "")
ENDIF ()

## lmdb ##
FIND_LIBRARY (LMDB_LIB liblmdb HINTS ${THIRD_PARTY_PATH}/lmdb/libraries/liblmdb)
IF (NOT LMDB_LIB)
    MESSAGE(FATAL_ERROR "Could not find LMDB lib")
ENDIF ()

## libbtc ##
IF (IS_ABSOLUTE ${THIRD_PARTY_PATH})
    SET(LIBBTC_PATH ${THIRD_PARTY_PATH}/libbtc)
ELSE ()
    SET(LIBBTC_PATH ${CMAKE_SOURCE_DIR}/${THIRD_PARTY_PATH}/libbtc)
ENDIF ()

FIND_LIBRARY (LIBBTC_LIB libbtc HINTS
    ${LIBBTC_PATH}/.libs
    ${LIBBTC_PATH}/lib
    ${LIBBTC_PATH}/build
)
IF (NOT LIBBTC_LIB)
    MESSAGE(FATAL_ERROR "Could not find libbtc")
ENDIF ()
SET(LIBBTC_INCLUDE_DIR ${LIBBTC_PATH})

## lws ##
IF (IS_ABSOLUTE ${WITH_LWS})
    # do nothing
ELSE ()
    SET(WITH_LWS ${CMAKE_SOURCE_DIR}/${WITH_LWS})
ENDIF ()

## libwebsockets ##
IF (IS_ABSOLUTE ${THIRD_PARTY_PATH})
    SET(LWS_PATH ${THIRD_PARTY_PATH}/libwebsockets)
ELSE ()
    SET(LWS_PATH ${CMAKE_SOURCE_DIR}/${THIRD_PARTY_PATH}/libwebsockets)
ENDIF ()

FIND_LIBRARY (LIBWEBSOCKETS_LIB libwebsockets_static HINTS
    ${LWS_PATH}/lib
    ${LWS_PATH}/build/lib)
IF (NOT LIBWEBSOCKETS_LIB)
    MESSAGE(FATAL_ERROR "Could not find WS lib")
ENDIF ()
SET(LIBWEBSOCKETS_INCLUDE_DIRS ${LWS_PATH}/include)

## capnproto ##
IF (IS_ABSOLUTE ${THIRD_PARTY_PATH})
    SET(CAPNP_PATH ${THIRD_PARTY_PATH}/capnproto)
ELSE ()
    SET(CAPNP_PATH ${CMAKE_SOURCE_DIR}/${THIRD_PARTY_PATH}/capnproto)
ENDIF ()

SET(CAPNP_SUFFIX c++/src)
SET(CAPNP_INCLUDE_DIRS ${CAPNP_PATH}/${CAPNP_SUFFIX})

FIND_LIBRARY (CAPNPROTO_LIB libcapnp HINTS ${CAPNP_PATH}/build/${CAPNP_SUFFIX}/capnp)
IF (NOT CAPNPROTO_LIB)
    MESSAGE (FATAL_ERROR "Failed to find capnp lib")
ENDIF ()

## capnp compiler ##
SET(CAPNP_EXECUTABLE ${CAPNP_PATH}/build/${CAPNP_SUFFIX}/capnp/capnp.exe)
IF (NOT EXISTS ${CAPNP_EXECUTABLE})
    MESSAGE (FATAL_ERROR "Could not find capnp binary at: ${CAPNP_EXECUTABLE}")
ENDIF ()

## kj (capnproto dependency) ##
FIND_LIBRARY (KJ_LIB libkj HINTS ${CAPNP_PATH}/build/${CAPNP_SUFFIX}/kj)
IF (NOT KJ_LIB)
    MESSAGE (FATAL_ERROR "Failed ot find kj lib")
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
    MESSAGE (FATAL_ERROR "Could not find system lib: ws2_32.dll")
ENDIF ()

IF (NOT CMAKE_BUILD_TYPE)
    SET(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Release with debug info version" FORCE)
ENDIF ()

include(GCCToolchain)
include(CompilerColorDiagnostics)
include(CompilerWarnings)

SET(CMAKE_POSITION_INDEPENDENT_CODE ON)
IF (MSVC OR MINGW)
    add_definitions(-DNOMINMAX -D_WINSOCKAPI_)
ENDIF ()

# this is the main project
add_subdirectory(cppForSwig)
