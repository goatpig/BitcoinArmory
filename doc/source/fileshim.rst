..
    Copyright (c) 2015, Red Hat, Inc. and/or its affiliates
    Licensed under CC-BY-SA-3.0; see the license file

.. highlight:: c

.. index::
    double: Porting; PyFile

The PyFile API
==============

In Python 3, the PyFile API was reduced to a few functions, and is now
meant for internal interpreter use.

Python files (and file-like objects) should be manipulated with the API defined
by the :py:mod:`py3:io` module.


But, in the real world, some C libraries only provide debugging output to
``FILE*``. For cases like this, py3c provides a quick-and-dirty replacement
for :c:func:`py2:PyFile_AsFile`:

.. c:function:: FILE* py3c_PyFile_AsFileWithMode(PyObject *py_file, const char *mode)

    Open a (file-backed) Python file object as ``FILE*``.

    :param py_file: The file object, which must have a working ``fileno()`` method
    :param mode: A mode appropriate for ``fdopen``, such as ``'r'`` or ``'w'``

    This function presents several caveats:

    * Only works on file-like objects backed by an actual file
    * All C-level writes should be done before additional
      Python-level writes are allowed (e.g. by running Python code).
    * Though the function tries to flush, due to different layers of buffering
      there is no guarantee that reads and writes will be ordered correctly.
