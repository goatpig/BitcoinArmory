..
    Copyright (c) 2015, Red Hat, Inc. and/or its affiliates
    Licensed under CC-BY-SA-3.0; see the license file


===================
Definitions in py3c
===================

This table summarizes the various macros py3c defines,
or mentions in the Porting Guide.

============================ ============= ==============
Macro                        py2           py3
============================ ============= ==============
IS_PY3                       → 0           → 1
PyStr_*                      → PyString_*  → PyUnicode_*
PyBytes_*                    → PyString_*  ✔
PyUnicode_*                  ✔             ✔
PyString_*                   ✔             *error*
PyStr_AsUTF8AndSize          see below     ✔
PyInt_*                      ✔             → PyLong_*
PyLong_*                     ✔             ✔
PyFloat_FromString           see below     ✔
PyModuleDef                  see below     ✔
PyModuleDef_HEAD_INIT        → 0           ✔
PyModule_Create              see below     ✔
MODULE_INIT_FUNC             see below     see below
Rich comparisons             ✔             ✔
Py_RETURN_RICHCOMPARE        =             =
Py_RETURN_NOTIMPLEMENTED     =             =
Py_UNREACHABLE               =             =
PY3C_RICHCMP                 see below     see below
Py_TYPE                      ✔             ✔
Py_REFCNT                    ✔             ✔
Py_SIZE                      ✔             ✔
PyVarObject_HEAD_INIT        ✔             ✔
PyCapsule_*                  see below     ✔
Py_TPFLAGS_*                 ✔             see below
PyMem_Raw*                   =             =
Py_UNUSED                    =             =
============================ ============= ==============

Legend:

    | ✔ – provided by Python
    | → – defined in py3c as a simple alias for
    | = – provided by at least Python 3.7; py3c backports it to Python versions that don't define it

The following non-trivial macros are defined:

    :c:func:`PyStr_AsUTF8AndSize`
        Python 2: defined in terms of PyString_Size and PyString_AsString. Differences from Python 3:

          * no encoding (string is assumed to be UTF-8-encoded)
          * size pointer must not be NULL
          * size may be stored even if an error occurs

    :c:func:`PyFloat_FromSting`
        | Python 2: Only takes one argument, as in Python 3.

    :c:type:`PyModuleDef`
        | Python 2: contains ``m_name``, ``m_doc``, ``m_size``, ``m_methods`` fields from Python 3, and ``m_base`` to accomodate PyModuleDef_HEAD_INIT.

    :c:func:`PyModule_Create`
        | Python 2: calls Py_InitModule3; semantics same as in Python 3

    :c:func:`MODULE_INIT_FUNC <MODULE_INIT_FUNC>`\ (<mod>)
        | Python 3: declares PyInit_<mod> and provides function header for it
        | Python 2: declares & defines init<mod>; declares a static PyInit_<mod> and provides function header for it

    :c:func:`PY3C_RICHCMP`
        | Convenience macro for comparisons, same in both versions.
        | Deprecated; use :c:macro:`Py_RETURN_RICHCOMPARE` instead.

    :ref:`PyCapsule_* <capsulethunk>`
        | Capsules are included in Python 2.7 and 3.1+.
        | For 2.6, see the chapter :doc:`capsulethunk`.

    :ref:`Py_TPFLAGS_* <tpflags>`
        Type flags that were removed in Python 3 are defined to ``0`` in
        ``<py3c/tpflags.h>``.
        Read the documentation before including the file.
