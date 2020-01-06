set(LMDB_SOURCES
    lmdbpp.cpp
    mdb.c
    midl.c
)

set(LMDB_INCLUDE_DIRECTORIES
    PRIVATE lmdb/libraries/liblmdb
)

if(MSVC)
    list(APPEND LMDB_SOURCES
        ../../../leveldb_windows_port/win32_posix/dirent_win32.cpp
        ../../../leveldb_windows_port/win32_posix/mman.cpp
        ../../../leveldb_windows_port/win32_posix/pthread_win32port.cpp
        ../../../leveldb_windows_port/win32_posix/Win_TranslatePath.cpp
        ../../../leveldb_windows_port/win32_posix/win32_posix.cc
    )

    list(APPEND LMDB_INCLUDE_DIRECTORIES
        PUBLIC ../../../leveldb_windows_port/win32_posix
    )
endif()

add_library(lmdb
    ${LMDB_SOURCES}
)

target_include_directories(lmdb
    ${LMDB_INCLUDE_DIRECTORIES}
)

if(APPLE)
    target_compile_definitions(lmdb
        PRIVATE MDB_USE_POSIX_SEM=1
    )
endif()
