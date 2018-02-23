import sys
import os
from distutils.core import setup, Extension

USE_CPP = (os.environ.get('TEST_USE_CPP') == 'yes')

# The C/C++ source is the same, but GCC recognizes the language by extension,
# so use a symlink named 'test_py3c.cpp' for C++.
# (there's also a gcc -x switch, but it needs to go before the filename;
# I don't think setuptools allows that)
if USE_CPP:
    sources = ['test_py3c.cpp']
else:
    sources = ['test_py3c.c']

extra_compile_args = []
extra_compile_args.extend(['-Werror', '-Wall'])
if sys.version_info < (2, 7):
    # XXX: The PyCapsule (2.6) shim tests aren't yet warning-free in GCC
    extra_compile_args.extend(['-Wno-format'])

test_py3c_module = Extension(
    'test_py3c',
    sources=sources,
    include_dirs=['../include'],
    extra_compile_args=extra_compile_args,
)
test_py3c_module.extra_compile_args=extra_compile_args

setup_args = dict(
    name='test_py3c',
    version='0.0',
    description = '',
    ext_modules = [test_py3c_module]
)

if __name__ == '__main__':
    setup(**setup_args)
