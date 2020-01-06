include(ArmorySupport)

use_cxx11()

include(CheckIncludeFileCXX)

set(REQUIRED_HEADERS
    arpa/inet.h fcntl.h limits.h netdb.h netinet/in.h stddef.h stdlib.h
    string.h sys/ioctl.h sys/socket.h sys/time.h unistd.h
)

foreach(header ${REQUIRED_HEADERS})
    check_include_file_cxx(${header} ${header}_exists)

    if(NOT ${header}_exists)
        message(FATAL_ERROR "required header ${header} not found")
    endif()
endforeach()

include(CheckTypeSize)

foreach(type size_t ssize_t ptrdiff_t)
    check_type_size(${type} ${type}_exists LANGUAGE CXX)

    if(NOT ${type}_exists)
        message(FATAL_ERROR "your C++ compiler is missing the required type ${type}")
    endif()
endforeach()

include(CheckFunctionExists)

set(save_libs ${CMAKE_REQUIRED_LIBRARIES})

if(NOT WIN32)
    set(CMAKE_REQUIRED_LIBRARIES ${CMAKE_REQUIRED_LIBRARIES} m) # math library
endif()

foreach(func gethostbyname gettimeofday memmove memset pow select setlocale socket)
    check_function_exists(${func} ${func}_exists)

    if(NOT ${func}_exists)
        message(FATAL_ERROR "your system is missing the required C library function ${func}")
    endif()
endforeach()

set(CMAKE_REQUIRED_LIBRARIES ${save_libs})

set(LIBCRYPTOPP_SOURCES
    3way.cpp
    gf2n.cpp
    pssr.cpp
    socketft.cpp
    adler32.cpp
    default.cpp
    gfpcrypt.cpp
    pubkey.cpp
    sosemanuk.cpp
    algebra.cpp
    des.cpp
    gost.cpp
    queue.cpp
    square.cpp
    algparam.cpp
    dessp.cpp
    gzip.cpp
    rabin.cpp
    squaretb.cpp
    arc4.cpp
    DetSign.cpp
    hex.cpp
    randpool.cpp
    strciphr.cpp
    asn.cpp
    dh2.cpp
    hmac.cpp
    rc2.cpp
    tea.cpp
    authenc.cpp
    dh.cpp
    hrtimer.cpp
    rc5.cpp
    base32.cpp
    dll.cpp
    ida.cpp
    rc6.cpp
    tftables.cpp
    base64.cpp
    idea.cpp
    rdtables.cpp
    tiger.cpp
    basecode.cpp
    dsa.cpp
    integer.cpp
    tigertab.cpp
    eax.cpp
    iterhash.cpp
    rijndael.cpp
    trdlocal.cpp
    ec2n.cpp
    luc.cpp
    ripemd.cpp
    ttmac.cpp
    bfinit.cpp
    eccrypto.cpp
    md2.cpp
    rng.cpp
    twofish.cpp
    blowfish.cpp
    ecp.cpp
    md4.cpp
    rsa.cpp
    blumshub.cpp
    elgamal.cpp
    md5.cpp
    rw.cpp
    camellia.cpp
    emsa2.cpp
    misc.cpp
    safer.cpp
    cast.cpp
    eprecomp.cpp
    modes.cpp
    salsa.cpp
    vmac.cpp
    casts.cpp
    esign.cpp
    mqueue.cpp
    seal.cpp
    wait.cpp
    cbcmac.cpp
    files.cpp
    mqv.cpp
    seed.cpp
    wake.cpp
    ccm.cpp
    filters.cpp
    nbtheory.cpp
    serpent.cpp
    whrlpool.cpp
    channels.cpp
    fips140.cpp
    network.cpp
    shacal2.cpp
    winpipes.cpp
    cmac.cpp
    oaep.cpp
    sha.cpp
    xtr.cpp
    cpu.cpp
    osrng.cpp
    sharkbox.cpp
    xtrcrypt.cpp
    crc.cpp
    gcm.cpp
    pch.cpp
    shark.cpp
    zdeflate.cpp
    cryptlib_bds.cpp
    gf2_32.cpp
    pkcspad.cpp
    simple.cpp
    zinflate.cpp
    cryptlib.cpp
    gf256.cpp
    polynomi.cpp
    skipjack.cpp
    zlib.cpp
)

unset(LIBCRYPTOPP_DEFINITIONS)
unset(LIBCRYPTOPP_COMPILE_OPTIONS)

option(WITH_HOST_CPU_FEATURES "support the CPU features of the build host" ON)

if(WITH_HOST_CPU_FEATURES AND CMAKE_CXX_COMPILER_ID STREQUAL GNU)
    list(APPEND LIBCRYPTOPP_COMPILE_OPTIONS -march=native)
endif()

# enable asm on x86, gcc only
if(CMAKE_SYSTEM_PROCESSOR MATCHES "i.86|x86_64" AND CMAKE_CXX_COMPILER_ID STREQUAL GNU)
    enable_language(ASM)

    # cmake needs to be patched to support this
    execute_process(
        COMMAND as --version
        COMMAND head -1
        COMMAND awk "{print $NF}"
        ERROR_QUIET
        OUTPUT_VARIABLE CMAKE_ASM_COMPILER_VERSION
    )

    if(CMAKE_ASM_COMPILER_VERSION VERSION_LESS 2.10)
        list(APPEND LIBCRYPTOPP_DEFINITIONS CRYPTOPP_DISABLE_ASM)
    elseif(CMAKE_ASM_COMPILER_VERSION VERSION_LESS 2.17)
        list(APPEND LIBCRYPTOPP_DEFINITIONS CRYPTOPP_DISABLE_SSE3)
    endif()

    if(WITH_HOST_CPU_FEATURES AND CMAKE_SYSTEM_NAME STREQUAL Linux)
        execute_process(COMMAND grep -o -m 1 aes    /proc/cpuinfo OUTPUT_VARIABLE HAVE_AES)
        execute_process(COMMAND grep -o -m 1 pclmul /proc/cpuinfo OUTPUT_VARIABLE HAVE_PCLMUL)

        if(HAVE_AES)
            list(APPEND LIBCRYPTOPP_COMPILE_OPTIONS -maes)
            list(APPEND LIBCRYPTOPP_DEFINITIONS     CRYPTOPP_ENABLE_AESNI)
        endif()

        if(HAVE_PCLMUL)
            list(APPEND LIBCRYPTOPP_COMPILE_OPTIONS -mpclmul)
        endif()
    endif()

    check_x86_cpu_features()

    list(APPEND LIBCRYPTOPP_COMPILE_OPTIONS ${X86_CPU_FEATURES_COMPILER_FLAGS})
else()
    list(APPEND LIBCRYPTOPP_DEFINITIONS CRYPTOPP_DISABLE_ASM)
endif()

add_library(cryptopp
    ${LIBCRYPTOPP_SOURCES}
)

if(LIBCRYPTOPP_DEFINITIONS)
    target_compile_definitions(cryptopp
        PRIVATE ${LIBCRYPTOPP_DEFINITIONS}
    )
endif()

if(LIBCRYPTOPP_COMPILE_OPTIONS)
    target_compile_options(cryptopp
        PRIVATE ${LIBCRYPTOPP_COMPILE_OPTIONS}
    )
endif()

if(NOT WIN32)
    target_link_libraries(cryptopp
        m # math
    )
endif()
