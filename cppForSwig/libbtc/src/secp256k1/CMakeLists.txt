set(SECP256K1_VERSION 0.1) # I have no idea actually, this is not used

set(SECP256K1_SOURCES
    src/secp256k1.c
)

option(SECP256K1_ENABLE_ASM "enable asm for secp256k1" ON)

unset(USE_ASM_X86_64)
unset(USE_EXTERNAL_ASM)

if(SECP256K1_ENABLE_ASM)
    if(CMAKE_SYSTEM_PROCESSOR STREQUAL x86_64)
        set(USE_ASM_X86_64 ON)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES arm)
        set(USE_EXTERNAL_ASM ON)
        list(APPEND SECP256K1_SOURCES src/asm/field_10x26_arm.s)
    endif()
endif()

add_library(secp256k1
    ${SECP256K1_SOURCES}
)

target_compile_definitions(secp256k1
    PRIVATE SECP256K1_BUILD HAVE_CONFIG_H
)

# make the config file

include(CheckIncludeFile)

check_include_file("inttypes.h"  HAVE_INTTYPES_H)
check_include_file("memory.h"    HAVE_MEMORY_H)
check_include_file("stdint.h"    HAVE_STDINT_H)
check_include_file("stdlib.h"    HAVE_STDLIB_H)
check_include_file("strings.h"   HAVE_STRINGS_H)
check_include_file("string.h"    HAVE_STRING_H)
check_include_file("sys/stat.h"  HAVE_SYS_STAT_H)
check_include_file("sys/types.h" HAVE_SYS_TYPES_H)
check_include_file("unistd.h"    HAVE_UNISTD_H)

include(TestBigEndian)

test_big_endian(WORDS_BIGENDIAN)

include(CheckTypeSize)

check_type_size(__int128  HAVE___INT128)

check_type_size(uint128_t HAVE_UINT128_T)

string_option(SECP256K1_USE_LIBGMP "use libgmp for secp256k1" AUTO)

unset(LIBGMP_REQUIRED)
unset(HAVE_LIBGMP)

if(SECP256K1_USE_LIBGMP AND (NOT SECP256K1_USE_LIBGMP STREQUAL AUTO))
    set(LIBGMP_REQUIRED REQUIRED)
elseif(NOT SECP256K1_USE_LIBGMP)
    set(HAVE_LIBGMP NO)
endif()

unset(USE_NUM_NONE)
unset(USE_NUM_GMP)
unset(USE_FIELD_INV_BUILTIN)
unset(USE_FIELD_INV_NUM)
unset(USE_SCALAR_INV_BUILTIN)
unset(USE_SCALAR_INV_NUM)

if(NOT DEFINED HAVE_LIBGMP)
    find_library(GMP_LIBRARIES NAMES gmp ${LIBGMP_REQUIRED})

    if(GMP_LIBRARIES)
        set(HAVE_LIBGMP ON)

        target_link_libraries(secp256k1
            ${GMP_LIBRARIES}
        )

        set(USE_NUM_GMP        ON)
        set(USE_FIELD_INV_NUM  ON)
        set(USE_SCALAR_INV_NUM ON)
    else()
        set(HAVE_LIBGMP OFF)
    endif()
endif()

if(NOT HAVE_LIBGMP)
    set(USE_NUM_NONE           ON)
    set(USE_FIELD_INV_BUILTIN  ON)
    set(USE_SCALAR_INV_BUILTIN ON)
endif()

option(SECP256K1_MODULE_ECDH "enable ecdh module for secp256k1" OFF)

unset(ENABLE_MODULE_ECDH)

if(SECP256K1_MODULE_ECDH)
    set(ENABLE_MODULE_ECDH ON)
endif()

option(SECP256K1_MODULE_RECOVERY "enable recovery module for secp256k1" OFF)

unset(ENABLE_MODULE_RECOVERY)

if(SECP256K1_MODULE_RECOVERY)
    set(ENABLE_MODULE_RECOVERY ON)
endif()

option(SECP256K1_MODULE_SCHNORR "enable schnorr module for secp256k1" OFF)

unset(ENABLE_MODULE_SCHNORR)

if(SECP256K1_MODULE_SCHNORR)
    set(ENABLE_MODULE_SCHNORR ON)
endif()

option(SECP256K1_ECMULT_STATIC_PRECOMPUTATION "use a statically generated ecmult table for secp256k1" OFF)

unset(USE_ECMULT_STATIC_PRECOMPUTATION)

if(SECP256K1_ECMULT_STATIC_PRECOMPUTATION)
    set(USE_ECMULT_STATIC_PRECOMPUTATION ON)
endif()

option(SECP256K1_ENDOMORPHISM "use endomorphism optimization for secp256k1" OFF)

unset(USE_ENDOMORPHISM)

if(SECP256K1_ENDOMORPHISM)
    set(USE_ENDOMORPHISM ON)
endif()

string_option(SECP256K1_WITH_FIELD  "use '32bit', '64bit' or 'AUTO' field with secp256k1"  AUTO)

string_option(SECP256K1_WITH_SCALAR "use '32bit', '64bit' or 'AUTO' scalar with secp256k1" AUTO)

if(SECP256K1_WITH_FIELD STREQUAL AUTO)
    set(SECP256K1_WITH_FIELD 32bit)

    # 64 bit
    if(CMAKE_C_SIZEOF_DATA_PTR EQUAL 8 AND (HAVE_LIBGMP OR HAVE_UINT128_T))
        set(SECP256K1_WITH_FIELD 64bit)
    endif()
endif()

if(SECP256K1_WITH_FIELD STREQUAL 64bit AND (NOT (HAVE_LIBGMP OR HAVE_UINT128_T)))
    message(FATAL_ERROR "64bit field support requires your compiler to have the 'uint128_t' type when not using libgmp")
endif()

if(SECP256K1_WITH_SCALAR STREQUAL AUTO)
    set(SECP256K1_WITH_SCALAR 32bit)

    # 64 bit
    if(CMAKE_C_SIZEOF_DATA_PTR EQUAL 8 AND (HAVE_LIBGMP OR HAVE_UINT128_T))
        set(SECP256K1_WITH_SCALAR 64bit)
    endif()
endif()

if(SECP256K1_WITH_SCALAR STREQUAL 64bit AND (NOT (HAVE_LIBGMP OR HAVE_UINT128_T)))
    message(FATAL_ERROR "64bit scalar support requires your compiler to have the 'uint128_t' type when not using libgmp")
endif()

unset(USE_FIELD_5X52)
unset(USE_FIELD_10X26)

if(SECP256K1_WITH_FIELD STREQUAL 64bit)
    set(USE_FIELD_5X52 ON)
elseif(SECP256K1_WITH_FIELD STREQUAL 32bit)
    set(USE_FIELD_10X26 ON)
else()
    message(FATAL_ERROR "invalid value for SECP256K1_WITH_FIELD, must be '64bit', '32bit' or 'AUTO'")
endif()

unset(USE_SCALAR_4X64)
unset(USE_SCALAR_8X32)

if(SECP256K1_WITH_SCALAR STREQUAL 64bit)
    set(USE_SCALAR_4X64 ON)
elseif(SECP256K1_WITH_SCALAR STREQUAL 32bit)
    set(USE_SCALAR_8X32 ON)
else()
    message(FATAL_ERROR "invalid value for SECP256K1_WITH_SCALAR, must be '64bit', '32bit' or 'AUTO'")
endif()

configure_file(
    src/libsecp256k1-config.h.cmakein
    ${PROJECT_BINARY_DIR}/libsecp256k1-config.h
)

# we don't need this but upstream might
if(FALSE)
    configure_file(
        libsecp256k1.pc.cmakein
        ${CMAKE_CURRENT_BINARY_DIR}/libsecp256k1.pc @ONLY
    )

    include(GNUInstallDirs)

    install(
        FILES ${CMAKE_CURRENT_BINARY_DIR}/libsecp256k1.pc
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
    )
endif()

target_include_directories(secp256k1
    PUBLIC  include
    PRIVATE .
    PRIVATE ../../include
    PRIVATE ../../src
    PRIVATE ${PROJECT_BINARY_DIR}
)
