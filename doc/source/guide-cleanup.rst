..
    Copyright (c) 2015, Red Hat, Inc. and/or its affiliates
    Licensed under CC-BY-SA-3.0; see the license file

.. highlight:: c

.. index::
    single: Cleanup

Cleanup – Dropping Support for Python 2
=======================================

When users of your C extension are not using Python 2 any more, or you need to
use one of Python 3's irresistible new features, you can convert the project to
use Python 3 only.
As mentioned earlier, it is usually not a good idea to do this until you
have full support for both Pythons.

With py3c, dropping Python 2 basically amounts to expanding all its compat
macros.
In other words, remove the ``py3c.h`` header, and fix the compile errors.

*   .. index::
        double: Cleanup; Strings
        double: Cleanup; Bytes
        double: Cleanup; Unicode

    Convert ``PyStr_*`` to ``PyUnicode_*``; ``PyInt_*`` to ``PyLong_*``.

*   .. index::
        double: Cleanup; Module Initialization

    Instead of ``MODULE_INIT_FUNC(<name>)``, write::

        PyMODINIT_FUNC PyInit_<name>(void);
        PyMODINIT_FUNC PyInit_<name>(void)

*   .. index::
        double: Cleanup; Types

    Remove ``Py_TPFLAGS_HAVE_WEAKREFS`` and ``Py_TPFLAGS_HAVE_ITER``
    (py3c defines them as 0).


*   .. index::
        double: Cleanup; Comparisons

    Replace ``return PY3C_RICHCMP`` by :c:func:`Py_RETURN_RICHCOMPARE`,
    as mentioned in :c:macro:`PY3C_RICHCMP` documentation.

*   Replace :c:func:`Py_RETURN_RICHCOMPARE` and :c:func:`Py_UNREACHABLE`
    by its expansion, unless you either
    support Python 3.7+ only or keep the ``py3c/comparison.h`` header.

*   Replace :c:func:`Py_RETURN_NOTIMPLEMENTED` by its expansion, unless you either
    support Python 3.3+ only or keep the ``py3c/comparison.h`` header.

*   Drop ``capsulethunk.h``, if you're using it.

*   Remove any code in ``#if !IS_PY3`` blocks, and the ifs around
    ``#if IS_PY3`` ones.

You will want to check the code as you're doing this.
For example, replacing PyLong can easily result in code like
``if (PyInt_Check(o) || PyInt_Check(o))``.

Enjoy your Python 3-compatible extension!
