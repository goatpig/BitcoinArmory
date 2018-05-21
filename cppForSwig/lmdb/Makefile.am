lib_LTLIBRARIES = liblmdb.la

# Shared library. Portability required when linking with  libCppBlockUtils.
liblmdb_la_SOURCES = lmdb.h midl.h \
		    mdb.c midl.c \
		    lmdbpp.h lmdbpp.cpp
liblmdb_la_LDFLAGS = $(LDFLAGS) -shared

AM_CXXFLAGS = -fPIC -DNDEBUG
AM_CFLAGS = -fPIC -DNDEBUG
