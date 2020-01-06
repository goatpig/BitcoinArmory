# this is the right way to set options for sub-projects
set(SECP256K1_MODULE_RECOVERY ON CACHE BOOL "enable sec256k1 recovery module" FORCE)

add_subdirectory(src/secp256k1)

set(LIBBTC_SOURCES
    src/aes256_cbc.c
    src/base58.c
    src/bip32.c
    src/block.c
    src/buffer.c
    src/chainparams.c
    src/commontools.c
    src/cstr.c
    src/ctaes.c
    src/ecc_key.c
    src/ecc_libsecp256k1.c
    src/memory.c
    src/random.c
    src/ripemd160.c
    src/script.c
    src/segwit_addr.c
    src/serialize.c
    src/sha2.c
    src/tx.c
    src/utils.c
    src/vector.c
)

include(CheckIncludeFile)

check_include_file("dlfcn.h"     HAVE_DLFCN_H)
check_include_file("inttypes.h"  HAVE_INTTYPES_H)
check_include_file("memory.h"    HAVE_MEMORY_H)
check_include_file("stdbool.h"   HAVE_STDBOOL_H)
check_include_file("stdint.h"    HAVE_STDINT_H)
check_include_file("stdlib.h"    HAVE_STDLIB_H)
check_include_file("strings.h"   HAVE_STRINGS_H)
check_include_file("string.h"    HAVE_STRING_H)
check_include_file("sys/stat.h"  HAVE_SYS_STAT_H)
check_include_file("sys/types.h" HAVE_SYS_TYPES_H)
check_include_file("unistd.h"    HAVE_UNISTD_H)

option(LIBBTC_WITH_WALLET "with libbtc wallet/database functions" ON)

set(WITH_WALLET ${LIBBTC_WITH_WALLET})

if(LIBBTC_WITH_WALLET)
    list(APPEND LIBBTC_SOURCES
        src/logdb/logdb_core.c
        src/logdb/logdb_memdb_llist.c
        src/logdb/logdb_memdb_rbtree.c
        src/logdb/logdb_rec.c
        src/logdb/red_black_tree.c
        src/wallet.c
    )
endif()

option(LIBBTC_WITH_NET "with libbtc networking" ON)

set(WITH_NET ${LIBBTC_WITH_NET})

set(LIBBTC_LINK_LIBRARIES
    secp256k1
)

# these have to be PUBLIC for the tools targets
set(LIBBTC_INCLUDE_DIRECTORIES
    PUBLIC  include
    PUBLIC  src/logdb/include
    PUBLIC  ${PROJECT_BINARY_DIR} # for config.h
)

if(LIBBTC_WITH_NET)
    find_package(PkgConfig REQUIRED)

    pkg_check_modules(LIBEVENT          REQUIRED libevent)
    pkg_check_modules(LIBEVENT_PTHREADS REQUIRED libevent_pthreads)

    list(APPEND LIBBTC_LINK_LIBRARIES
        ${LIBEVENT_LIBRARIES}
        ${LIBEVENT_PTHREADS_LIBRARIES}
    )

    list(APPEND LIBBTC_INCLUDE_DIRECTORIES
        PRIVATE ${LIBEVENT_INCLUDE_DIRS}
        PRIVATE ${LIBEVENT_PTHREADS_INCLUDE_DIRS}
    )

    list(APPEND LIBBTC_SOURCES
        src/headersdb_file.c
        src/net.c
        src/netspv.c
        src/protocol.c
    )
endif()

option(LIBBTC_WITH_TESTS "with libbtc tests" ON)

option(LIBBTC_WITH_TOOLS "with libbtc tool cli applications" ON)

include(CheckCXXSourceCompiles)

check_cxx_source_compiles("
    void myfunc()
    {
        __builtin_expect(0,0);
    }
" HAVE_BUILTIN_EXPECT)

string_option(LIBBTC_RANDOM_DEVICE "device to use for random numbers, generally /dev/urandom on linux" /dev/urandom)

set(RANDOM_DEVICE ${LIBBTC_RANDOM_DEVICE})

if(RANDOM_DEVICE STREQUAL /dev/urandom OR RANDOM_DEVICE STREQUAL /dev/random)
    set(FILE_RANDOM TRUE)
endif()

set(PACKAGE_NAME    libbtc)

set(LIBBTC_VERSION  0.1)

set(PACKAGE_VERSION ${LIBBTC_VERSION})

configure_file(
    src/libbtc-config.h.cmakein
    ${PROJECT_BINARY_DIR}/libbtc-config.h
)

add_library(btc
    ${LIBBTC_SOURCES}
)

target_include_directories(btc
    ${LIBBTC_INCLUDE_DIRECTORIES}
)

target_link_libraries(btc
    ${LIBBTC_LINK_LIBRARIES}
)

target_compile_definitions(btc
    PUBLIC HAVE_CONFIG_H
)

if(LIBBTC_WITH_TOOLS)
    add_executable(bitcointool
        src/tools/bitcointool.c
    )

    set_target_properties(bitcointool PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

    target_link_libraries(bitcointool btc)

    if(LIBBTC_WITH_NET)
        add_executable(bitcoin-send-tx
            src/tools/bitcoin-send-tx.c
        )

        set_target_properties(bitcoin-send-tx PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

        target_link_libraries(bitcoin-send-tx btc)

        add_executable(bitcoin-spv
            src/tools/bitcoin-spv.c
        )

        set_target_properties(bitcoin-spv PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

        target_link_libraries(bitcoin-spv btc)
    endif()
endif()

# we don't need this but upstream might
if(FALSE)
    configure_file(
        libbtc.pc.cmakein
        ${CMAKE_CURRENT_BINARY_DIR}/libbtc.pc @ONLY
    )

    include(GNUInstallDirs)

    install(
        FILES ${CMAKE_CURRENT_BINARY_DIR}/libbtc.pc
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
    )
endif()

if(LIBBTC_WITH_TESTS)
    set(TESTS_SOURCES
        test/aes_tests.c
        test/base58check_tests.c
        test/bip32_tests.c
        test/block_tests.c
        test/buffer_tests.c
        test/cstr_tests.c
        test/ecc_tests.c
        test/eckey_tests.c
        test/hash_tests.c
        test/memory_tests.c
        test/random_tests.c
        test/serialize_tests.c
        test/sha2_tests.c
        test/utest.h
        test/unittester.c
        test/tx_tests.c
        test/utils_tests.c
        test/vector_tests.c
    )

    if(LIBBTC_WITH_WALLET)
        list(APPEND TESTS_SOURCES
            test/wallet_tests.c
            src/logdb/test/logdb_tests.c
            src/logdb/test/tests_red_black_tree.c
        )
    endif()

    if(LIBBTC_WITH_NET)
        list(APPEND TESTS_SOURCES
            test/net_tests.c
            test/netspv_tests.c
            test/protocol_tests.c
        )
    endif()

    if(LIBBTC_WITH_TOOLS)
        list(APPEND TESTS_SOURCES
            test/tool_tests.c
        )
    endif()

    add_executable(libbtc-tests
        ${TESTS_SOURCES}
    )

    target_link_libraries(libbtc-tests
        btc
    )

    set_target_properties(libbtc-tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
endif()
