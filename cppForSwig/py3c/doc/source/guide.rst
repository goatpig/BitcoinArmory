..
    Copyright (c) 2015, Red Hat, Inc. and/or its affiliates
    Licensed under CC-BY-SA-3.0; see the license file

.. highlight:: c

=====================================
Porting guide for Python C Extensions
=====================================

This guide is written for authors of *C extensions* for Python, who want to
make their extension compatible with Python 3. It provides comprehensive,
step-by-step porting instructions.

Before you start adding Python 3 compatibility to your C extension,
consider your options:

* .. index:: CFFI

  If you are writing a wrapper for a C library, take a look at
  `CFFI <https://cffi.readthedocs.org>`_, a C Foreign Function Interface
  for Python. This lets you call C from Python 2.6+ and 3.3+, as well as PyPy.
  A C compiler is required for development, but not for installation.

* .. index:: Cython

  For more complex code, consider `Cython <http://cython.org/>`_,
  which compiles a Python-like language to C, has great support for
  interfacing with C libraries, and generates code that works on
  Python 2.6+ and 3.3+.

Using CFFI or Cython will make your code more maintainable in the long run,
at the cost of rewriting the entire extension.
If that's not an option, you will need to update the extension to use
Python 3 APIs. This is where py3c can help.

This is an *opinionated* guide to porting. It does not enumerate your options,
but rather provides one tried way of doing things.

This doesn't mean you can't do things your way – for example, you can
cherry-pick the macros you need and put them directly in your files.
However, dedicated headers for backwards compatibility will make them easier
to find when the time comes to remove them.

If you want more details, consult the
`"Migrating C extensions" chapter <http://python3porting.com/cextensions.html>`_
from Lennart Regebro's book "Porting to Python 3",
the `C porting guide <https://docs.python.org/3/howto/cporting.html>`_
from Python documentation, and the py3c headers for macros to use.

The py3c library lives `at Github <https://github.com/encukou/py3c>`_.
See the README for installation instructions.


.. toctree::
   :hidden:

   1. Modernization <guide-modernization>
   2. Porting <guide-porting>
   3. Cleanup <guide-cleanup>


.. rubric:: Overview

Porting a C extension to Python 3 involves three phases:

1. :doc:`Modernization <guide-modernization>`,
   where the code is migrated to the latest Python 2 features, and tests are
   added to prevent bugs from creeping in later.
   After this phase, the project will support Python 2.6+.
2. :doc:`Porting <guide-porting>`,
   where support for Python 3 is introduced, but Python 2 compatibility is kept.
   After this phase, the project will support Python 2.6+ and 3.3+.
3. :doc:`Cleanup <guide-cleanup>`,
   where support for Python 2 is removed, and you can start using
   Python 3-only features.
   After this phase, the project will support Python 3.3+.

The first two phases can be done simultaneously; I separate them here because
the porting might require involved discussions/decisions about longer-term
strategy, while modernization can be done immediately
(as soon as support for Python 2.5 is dropped).
But do not let the last two stages overlap,
unless the port is trivial enough to be done in a single patch.
This way you will have working code at all time.

Generally, *libraries*, on which other projects depend, will support
both Python 2 and 3 for a longer time, to allow dependent code to make
the switch. For libraries, the start of phase 3 might be delayed for many years.
On the other hand, *applications* can often switch at once,
dropping Python 2 support as soon as the porting is done.


Ready? The :doc:`guide-modernization` section is waiting!

