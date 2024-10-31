################################################################################
#                                                                              #
# Copyright (C) 2021, goatpig.                                                 #
#  Distributed under the MIT license                                           #
#  See LICENSE-MIT or https://opensource.org/licenses/MIT                      #
#                                                                              #
################################################################################

"""
This is the CFFI setup file. Here all C methods and structs/typedefs that
need python access are first declared. The C code is then made available to
CFFI, which will invoke gcc to build the corresponding shared library. There
is also a feature to link to system and discoverable shared libs.

The CFFI model seems to favor shared libraries. It is preferable to only let
it build the narrow set of C functions that need exposed to Python and let it
link to the internal code dynamically.

The other option is to build the entire code as part of the CFFI shared lib,
which is longer and most likely innefficient if the C/C++ side needs access
to those definitions too.
"""

import cffi
import os
import optparse
import logging

parser = optparse.OptionParser(usage="%prog [options]\n")
parser.add_option("--libbtc_path", dest="libbtc_path", default="../../libbtc", type="str", help="path to libbtc folder")
parser.add_option("--build_prefix", dest="build_prefix", default="..", type="str", help="path to build folder")

CLI_OPTIONS = None
CLI_ARGS = None
(CLI_OPTIONS, CLI_ARGS) = parser.parse_args()

hkdf_path = "cppForSwig/hkdf"

#libbtc paths
libbtc_libpath = os.path.join(CLI_OPTIONS.libbtc_path, ".libs")
libbtc_includepath = os.path.join(CLI_OPTIONS.libbtc_path, "include")
libbtc_libfile = os.path.join(libbtc_libpath, "libbtc.a")

if not os.path.exists(libbtc_includepath):
    logging.error(f"could not find libbtc include path (looked for: \"{libbtc_includepath}\")")

if not os.path.exists(libbtc_libfile):
    logging.error(f"could not find libbtc binary path (looked for: \"{libbtc_libfile}\")")

#chachapoly path
chachapoly_name = "libchacha20poly1305.a"
chachapoly_libpath = os.path.join(CLI_OPTIONS.build_prefix, "cppForSwig/chacha20poly1305")

chachapoly_fullpath = os.path.join(chachapoly_libpath, chachapoly_name)
if not os.path.exists(chachapoly_fullpath):
    logging.warning(f"could not find chachapoly lib (looked for: \"{chachapoly_fullpath}\")")

    chachapoly_libpath = os.path.join(chachapoly_libpath, ".libs")
    chachapoly_fullpath = os.path.join(chachapoly_libpath, chachapoly_name)
    if not os.path.exists(chachapoly_fullpath):
        logging.error(f"could not find chachapoly lib (looked for: \"{chachapoly_fullpath}\")")
    else:
        logging.info(f"  found in \"{chachapoly_fullpath}\"")

#hkdf path
hkdf_name = "libhkdf.a"
hkdf_libpath = os.path.join(CLI_OPTIONS.build_prefix, "cppForSwig/hkdf")

hkdf_fullpath = os.path.join(hkdf_libpath, hkdf_name)
if not os.path.exists(hkdf_fullpath):
    logging.warning(f"could not find hkdf lib (looked for: \"{hkdf_fullpath}\")")

    hkdf_libpath = os.path.join(hkdf_libpath, ".libs")
    hkdf_fullpath = os.path.join(hkdf_libpath, hkdf_name)
    if not os.path.exists(hkdf_fullpath):
        logging.error(f"could not find hkdf lib (looked for: \"{hkdf_fullpath}\")")
    else:
        logging.info(f"  found in \"{hkdf_fullpath}\"")

"""
cffi.FFI.cdef() takes the C declarations of the functions to pythonize (
one declaration per line).

It takes structures and typedefs as well but does not make them directly
available to Python. Custom make_new like functions that return the desired
struct/type have to be added. It's preferable to add those to a dedicated
C header for cffi's purposes, or declare them inline in the setup file, so
as to avoid polluting the original C headers.

The typical approach is to read the relevant C header files, strip them of
all precompiler directives and feed them to cdef as is. I am not comfortable
with this wide net approach. It is more work but manual inline declaration
of the strict set of functions that Python needs to see is a lot cleaner,
and less opaque for reviewers.
"""

ffi = cffi.FFI()
with open('cffi_declarations.cffi') as f:
    data = ''
    for line in f:
        if line.startswith('#'):
            continue
        data += line

    ffi.cdef(data)

ffi.set_source(
    "c20p1305",
    r'''
    #include "cffi_cdecl.h"
    ''',

    #source file for the dedicated cffi code
    sources = ["cffi_cdef.c"],

    #include paths
    include_dirs = [
        CLI_OPTIONS.libbtc_path,
        libbtc_includepath
    ],

    #link time custom path for lib discovery
    library_dirs = [
        libbtc_libpath,
        chachapoly_libpath,
        hkdf_libpath,
        ],

    #dependencies
    libraries = ["hkdf", "btc", "chacha20poly1305"],
    runtime_library_dirs = [libbtc_libpath],
    extra_link_args = ["-static-libgcc", "-static-libstdc++"]
)

ffi.compile(verbose=True)
