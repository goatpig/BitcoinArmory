..
    Copyright (c) 2015, Red Hat, Inc. and/or its affiliates
    Licensed under CC-BY-SA-3.0; see the license file


==============
py3c reference
==============

.. highlight:: c

Compatibility Layer
===================

::

    #include <py3c/compat.h>  // (included in <py3c.h>)


.. c:macro:: IS_PY3

    Defined as ``1`` when building for Python 3; ``0`` otherwise.


.. index:: PyStr
.. _PyStr:

PyStr
~~~~~

These functions are the intersection of PyString in Python 2,
and PyUnicode in Python 3, with a few helpers thrown it.

All follow the Python 3 API, except ``PyStr`` is substituted for ``PyUnicode``.

.. c:var:: PyStr_Type

    A :c:type:`PyTypeObject` instance representing a human-readable string.
    Exposed in Python as ``str``.

    | Python 2: :c:data:`PyString_Type <py2:PyString_Type>`
    | Python 3: :c:data:`(provided) <py3:PyUnicode_Type>`

.. c:function:: int PyStr_Check(PyObject *o)

    Check that *o* is an instance of :ref:`PyStr` or a subtype.

    | Python 2: :c:func:`PyString_Check <py2:PyString_Check>`
    | Python 3: :c:func:`PyUnicode_Check <py3:PyUnicode_Check>`

.. c:function:: int PyStr_CheckExact(PyObject *o)

    Check that *o* is an instance of :ref:`PyStr`, but not a subtype.

    | Python 2: :c:func:`PyString_CheckExact <py2:PyString_CheckExact>`
    | Python 3: :c:func:`PyUnicode_CheckExact <py3:PyUnicode_CheckExact>`

.. c:function:: PyObject* PyStr_FromString(const char *u)

    Create a :ref:`PyStr` from a UTF-8 encoded null-terminated character buffer.

    | Python 2: :c:func:`PyString_FromString <py2:PyString_FromString>`
    | Python 3: :c:func:`PyUnicode_FromString <py3:PyUnicode_FromString>`

.. c:function:: PyObject* PyStr_FromStringAndSize(const char *u, Py_ssize_t len)

    Create a :ref:`PyStr` from a UTF-8 encoded character buffer,
    and corresponding size in bytes.

    Note that human-readable strings should not contain null bytes;
    but if the size is known, this is more efficient
    than :c:func:`PyStr_FromString`.

    | Python 2: :c:func:`PyString_FromStringAndSize <py2:PyString_FromStringAndSize>`
    | Python 3: :c:func:`PyUnicode_FromStringAndSize <py3:PyUnicode_FromStringAndSize>`

.. c:function:: PyObject* PyStr_FromFormat(const char *format, ...)

    Create a :ref:`PyStr` from a C printf-style format string and arguments.

    Note that formatting directives that were added in Python 3
    (``%li``, ``%lli``, ``zi``, ``%A``, ``%U``, ``%V``, ``%S``, ``%R``)
    will not work in Python 2.

    | Python 2: :c:func:`PyString_FromFormat <py2:PyString_FromFormat>`
    | Python 3: :c:func:`PyUnicode_FromFormat <py3:PyUnicode_FromFormat>`

.. c:function:: PyObject* PyStr_FromFormatV(const char *format, va_list vargs)

    As :c:func:`PyStr_FromFormat`, but takes a ``va_list``.

    | Python 2: :c:func:`PyString_FromFormatV <py2:PyString_FromFormatV>`
    | Python 3: :c:func:`PyUnicode_FromFormatV <py3:PyUnicode_FromFormatV>`

.. c:function:: const char* PyStr_AsString(PyObject *s)

    Return a null-terminated representation of the contents of *s*.
    The buffer is owned by *s* and must bot be modified, deallocated,
    or used after *s* is deallocated.

    Uses the UTF-8 encoding on Python 3.

    If given an Unicode string on Python 2, uses Python's default encoding.

    | Python 2: :c:func:`PyString_AsString <py2:PyString_AsString>`
    | Python 3: :c:func:`PyUnicode_AsUTF8 <py3:PyUnicode_AsUTF8>` (!)

.. c:function:: PyObject* PyStr_Concat(PyObject *left, PyObject *right)

    Concatenates two strings giving a new string.

    | Python 2: implemented in terms of :c:func:`PyString_Concat <py2:PyString_Concat>`
    | Python 3: :c:func:`PyUnicode_Concat <py3:PyUnicode_Concat>`

.. c:function:: PyObject* PyStr_Format(PyObject *format, PyObject *args)

    Format a string; analogous to the Python expression ``format % args``.
    The *args* must be a tuple or dict.

    | Python 2: :c:func:`PyString_Format <py2:PyString_Format>`
    | Python 3: :c:func:`PyUnicode_Format <py3:PyUnicode_Format>`

.. c:function:: void PyStr_InternInPlace(PyObject **string)

    Intern *string*, in place.

    | Python 2: :c:func:`PyString_InternInPlace <py2:PyString_InternInPlace>`
    | Python 3: :c:func:`PyUnicode_InternInPlace <py3:PyUnicode_InternInPlace>`

.. c:function:: PyObject* PyStr_InternFromString(const char *v)

    Create an interned string from a buffer.
    Combines :c:func:`PyStr_FromString` and :c:func:`PyStr_InternInPlace`.

    In Python 3, *v* must be UTF-8 encoded.

    | Python 2: :c:func:`PyString_InternFromString <py2:PyString_InternFromString>`
    | Python 3: :c:func:`PyUnicode_InternFromString <py3:PyUnicode_InternFromString>`

.. c:function:: PyObject* PyStr_Decode(const char *s, Py_ssize_t size, const char *encoding, const char *errors)

    Create a new string by decoding *size* bytes from *s*,
    using the specified *encoding*.

    | Python 2: :c:func:`PyString_Decode <py2:PyString_Decode>`
    | Python 3: :c:func:`PyUnicode_Decode <py3:PyUnicode_Decode>`

.. c:function:: char* PyStr_AsUTF8(PyObject *str)

    Encode a string using UTF-8 and return the result as a char*.
    Under Python 3, the result is UTF-8 encoded.

    | Python 2: :c:func:`PyString_AsString <py2:PyString_AsEncodedObject>`
    | Python 3: :c:func:`PyUnicode_AsUTF8 <py3:PyUnicode_AsUTF8>`

.. c:function:: PyObject* PyStr_AsUTF8String(PyObject *str)

    Encode a string using UTF-8 and return the result as PyBytes.

    In Python 2, (where PyStr is bytes in UTF-8 encoding already),
    this is a no-op.

    | Python 2: identity
    | Python 3: :c:func:`PyUnicode_AsUTF8String <py3:PyUnicode_AsUTF8String>`

.. c:function:: char *PyStr_AsUTF8AndSize(PyObject *str, Py_ssize_t *size)

    Return the UTF-8-encoded representation of the string, and set *size*
    to the number of bytes in this representation. The *size* may not be NULL.

    In Python 2, the string is assumed to be UTF8-encoded.

    On error, *size* may or may not be set.

    | Python 2: (\*size = :c:func:`PyString_Size(str) <py2:PyString_Size>`, :c:func:`PyString_AsString(str) <py2:PyString_AsString>`)
    | Python 3: :c:func:`PyUnicode_AsUTF8AndSize <py3:PyUnicode_AsUTF8AndSize>`


.. index:: PyBytes
.. _PyBytes:

PyBytes
~~~~~~~

These functions are the intersection of PyString in Python 2,
and PyBytes in Python 3.

All follow the Python 3 API.

.. c:var:: PyBytes_Type

    A :c:type:`PyTypeObject` instance representing a string of binary data.
    Exposed in Python 2 as ``str``, and in Python 3 as ``bytes``.

    | Python 2: :c:data:`PyString_Type <py2:PyString_Type>`
    | Python 3: :c:data:`(provided) <py3:PyBytes_Type>`

.. c:function:: int PyBytes_Check(PyObject *o)

    Check that *o* is an instance of :ref:`PyBytes` or a subtype.

    | Python 2: :c:func:`PyString_Check <py2:PyString_Check>`
    | Python 3: :c:func:`(provided) <py3:PyBytes_Check>`

.. c:function:: int PyBytes_CheckExact(PyObject *o)

    Check that *o* is an instance of :ref:`PyBytes`, but not a subtype.

    | Python 2: :c:func:`PyString_CheckExact <py2:PyString_CheckExact>`
    | Python 3: :c:func:`(provided) <py3:PyBytes_CheckExact>`

.. c:function:: PyObject* PyBytes_FromString(const char *v)

    Create a :ref:`PyBytes` from a NULL-terminated C buffer.

    Note that binary data might contain null bytes;
    consider using :c:func:`PyBytes_FromStringAndSize` instead.

    | Python 2: :c:func:`PyString_FromString <py2:PyString_FromString>`
    | Python 3: :c:func:`(provided) <py3:PyBytes_FromString>`

.. c:function:: PyObject* PPyBytes_FromStringAndSize(const char *v, Py_ssize_t len)

    Create a :ref:`PyBytes` from a C buffer and size.

    | Python 2: :c:func:`PyString_FromStringAndSize <py2:PyString_FromStringAndSize>`
    | Python 3: :c:func:`(provided) <py3:PyBytes_FromStringAndSize>`

.. c:function:: PyObject* PyBytes_FromFormat(const char *format, ...)

    Create a :ref:`PyBytes` from a C printf-style format string and arguments.

    | Python 2: :c:func:`PyString_FromFormat <py2:PyString_FromFormat>`
    | Python 3: :c:func:`(provided) <py3:PyBytes_FromFormat>`

.. c:function:: PyObject* PyBytes_FromFormatV(const char *format, va_list args)

    As :c:func:`PyBytes_FromFormat`, but takes a ``va_list``.

    | Python 2: :c:func:`PyString_FromFormatV <py2:PyString_FromFormatV>`
    | Python 3: :c:func:`(provided) <py3:PyBytes_FromFormatV>`

.. c:function:: Py_ssize_t PyBytes_Size(PyObject *o)

    Return the number of bytes in a :ref:`PyBytes` object.

    | Python 2: :c:func:`PyString_Size <py2:PyString_Size>`
    | Python 3: :c:func:`(provided) <py3:PyBytes_Size>`

.. c:function:: Py_ssize_t PyBytes_GET_SIZE(PyObject *o)

    As :c:func:`PyBytes_Size` but without error checking.

    | Python 2: :c:func:`PyString_GET_SIZE <py2:PyString_GET_SIZE>`
    | Python 3: :c:func:`(provided) <py3:PyBytes_GET_SIZE>`

.. c:function:: char *PyBytes_AsString(PyObject *o)

    Return the buffer in a :ref:`PyBytes` object.
    The data must not be modifiet or deallocated, or used after a reference
    to *o* is no longer held.

    | Python 2: :c:func:`PyString_AsString <py2:PyString_AsString>`
    | Python 3: :c:func:`(provided) <py3:PyBytes_AsString>`

.. c:function:: char *PyBytes_AS_STRING(PyObject *o)

    As :c:func:`PyBytes_AsString` but without error checking.

    | Python 2: :c:func:`PyString_AS_STRING <py2:PyString_AS_STRING>`
    | Python 3: :c:func:`(provided) <py3:PyBytes_AS_STRING>`

.. c:function:: int PyBytes_AsStringAndSize(PyObject *obj, char **buffer, Py_ssize_t *length)

    Get the buffer and size stored in a :ref:`PyBytes` object.

    | Python 2: :c:func:`PyString_AsStringAndSize <py2:PyString_AsStringAndSize>`
    | Python 3: :c:func:`(provided) <py3:PyBytes_AsStringAndSize>`

.. c:function:: void PyBytes_Concat(PyObject **bytes, PyObject *newpart)

    Concatenate *newpart* to *bytes*, returning a new object in *bytes*,
    and discarding the old.

    | Python 2: :c:func:`PyString_Concat <py2:PyString_Concat>`
    | Python 3: :c:func:`(provided) <py3:PyBytes_Concat>`

.. c:function:: void PyBytes_ConcatAndDel(PyObject **bytes, PyObject *newpart)

    As :c:func:`PyBytes_AsString` but decreases reference count of *newpart*.

    | Python 2: :c:func:`PyString_ConcatAndDel <py2:PyString_ConcatAndDel>`
    | Python 3: :c:func:`(provided) <py3:PyBytes_ConcatAndDel>`

.. c:function:: int _PyBytes_Resize(PyObject **string, Py_ssize_t newsize)

    Used for efficiently build bytes objects; see the Python docs.

    | Python 2: :c:func:`_PyString_Resize <py2:_PyString_Resize>`
    | Python 3: :c:func:`(provided) <py3:_PyBytes_Resize>`


.. index:: PyInt
.. _PyInt:

PyInt
~~~~~

These functions allow extensions to make the diistinction between ints
and longs on Python 2.

All follow the Python 2 API.

.. c:var:: PyInt_Type

    A :c:type:`PyTypeObject` instance representing an integer
    that fits in a C long.

    | Python 2: :c:data:`(provided) <py2:PyInt_Type>`
    | Python 3: :c:data:`PyLong_Type <py3:PyLong_Type>`

.. c:function:: int PyInt_Check(PyObject *o)

    Check that *o* is an instance of :ref:`PyInt` or a subtype.

    | Python 2: :c:func:`(provided) <py2:PyInt_Check>`
    | Python 3: :c:func:`PyLong_Check <py3:PyLong_Check>`

.. c:function:: int PyInt_CheckExact(PyObject *o)

    Check that *o* is an instance of :ref:`PyInt`, but not a subtype.

    | Python 2: :c:func:`(provided) <py2:PyInt_CheckExact>`
    | Python 3: :c:func:`PyLong_CheckExact <py3:PyLong_CheckExact>`

.. c:function:: PyObject* PyInt_FromString(char *str, char **pend, int base)

    Convert a string to :ref:`PyInt`. See the Python docs.

    | Python 2: :c:func:`(provided) <py2:PyInt_FromString>`
    | Python 3: :c:func:`PyLong_FromString <py3:PyLong_FromString>`

.. c:function:: PyObject* PyInt_FromLong(long i)

    Convert a C long int to :ref:`PyInt`.

    | Python 2: :c:func:`(provided) <py2:PyInt_FromLong>`
    | Python 3: :c:func:`PyLong_FromLong <py3:PyLong_FromLong>`

.. c:function:: PyObject* PyInt_FromSsize_t(Py_ssize_t i)

    Convert a Py_ssize_t int to :ref:`PyInt`.

    | Python 2: :c:func:`(provided) <py2:PyInt_FromSsize_t>`
    | Python 3: :c:func:`PyLong_FromSsize_t <py3:PyLong_FromSsize_t>`

.. c:function:: PyObject* PyInt_FromSize_t(Py_size_t i)

    Convert a Py_size_t int to :ref:`PyInt`.

    | Python 2: :c:func:`(provided) <py2:PyInt_FromSize_t>`
    | Python 3: :c:func:`PyLong_FromSize_t <py3:PyLong_FromSize_t>`

.. c:function:: long PyInt_AsLong(PyObject *o)

    Convert a :ref:`PyInt` to a C long.

    | Python 2: :c:func:`(provided) <py2:PyInt_AsLong>`
    | Python 3: :c:func:`PyLong_AsLong <py3:PyLong_AsLong>`

.. c:function:: long PyInt_AS_LONG(PyObject *o)

    As :c:func:`PyInt_AsLong`, but with no error checking.

    | Python 2: :c:func:`(provided) <py2:PyInt_AS_LONG>`
    | Python 3: :c:func:`PyLong_AS_LONG <py3:PyLong_AS_LONG>`

.. c:function:: unsigned long PyInt_AsUnsignedLongLongMask(PyObject *o)

    Convert a Python object to int, and return its value as an unsigned long.

    | Python 2: :c:func:`(provided) <py2:PyInt_AsUnsignedLongLongMask>`
    | Python 3: :c:func:`PyLong_AsUnsignedLongLongMask <py3:PyLong_AsUnsignedLongLongMask>`

.. c:function:: Py_ssize_t PyInt_AsSsize_t(PyObject *o)

    Convert a Python object to int, and return its value as a Py_ssize_t.

    | Python 2: :c:func:`(provided) <py2:PyInt_AsSsize_t>`
    | Python 3: :c:func:`PyLong_AsSsize_t <py3:PyLong_AsSsize_t>`

PyFloat
~~~~~~~

.. c:function:: PyObject* PyFloat_FromString(PyObject *str)

    Create a :c:type:`PyFloatObject` object.  The signature
    follows the Python 3 API, even on Python 2.

    | Python 2: :c:func:`PyFloat_FromString(str, NULL) <py2:PyFloat_FromString>`
    | Python 3: :c:func:`PyFloat_FromString(str) <py3:PyFloat_FromString>`


Module Initialization
~~~~~~~~~~~~~~~~~~~~~

.. c:function:: MODULE_INIT_FUNC(<name>)

    Use this macro as the header for the module initialization function.

    Python 2::

        static PyObject *PyInit_<name>(void);
        void init<name>(void);
        void init<name>(void) { PyInit_<name>(); }
        static PyObject *PyInit_<name>(void)

    Python 3::

        PyMODINIT_FUNC PyInit_<name>(void);
        PyMODINIT_FUNC PyInit_<name>(void)

.. c:type:: PyModuleDef

    Python 2:

        .. c:member:: int m_base

            Always set this to :c:macro:`PyModuleDef_HEAD_INIT`

        .. c:member:: char *m_name

        .. c:member:: char *m_doc

        .. c:member:: Py_ssize_t m_size

            Set this to -1. (Or if your module supports :c:func:`subinterpreters <py3:Py_NewInterpreter>`, use 0)

        .. c:member:: PyMethodDef m_methods

    Python 3: :c:type:`(provided) <py3:PyModuleDef>`

.. c:macro:: PyModuleDef_HEAD_INIT

    | Python 2: ``0``
    | Python 3: :c:member:`(provided) <py3:PyModuleDef.m_base>`


.. c:function:: PyObject* PyModule_Create(PyModuleDef def)

    | Python 2: :c:func:`Py_InitModule3 <py2:Py_InitModule3>`\(def->m_name, def->m_methods, def->m_doc)
    | Python 3: :c:func:`(provided) <py3:PyModule_Create>`



Comparison Helpers
==================

::

    #include <py3c/comparison.h>  // (included in <py3c.h>)


.. c:macro:: Py_RETURN_NOTIMPLEMENTED

    Backported from :c:macro:`Python 3.4 <py3:Py_RETURN_NOTIMPLEMENTED>`
    for older versions.

.. c:macro:: Py_UNREACHABLE

    Use instead of `assert()` or `abort()` in unreachable code,
    such as handling undefined comparison operations.

    Backported from :c:macro:`Python 3.7 <py3:Py_UNREACHABLE>`
    for older versions.

.. c:function:: PyObject* PY_RETURN_RICHCOMPARE(val1, val2, int op)

    Compares two arguments orderable by C comparison operators (such as C
    ints or floats), and returns :c:data:`Py_True` or :c:data:`Py_False` based
    on the result, properly increasing the reference count.

    The third argument specifies the requested operation,
    as for a :c:member:`rich comparison function <py3:PyTypeObject.tp_richcompare>`.

    Backported from :c:macro:`Python 3.7 <py3:PY_RETURN_RICHCOMPARE>`
    for older versions.

.. c:function:: PyObject* PY3C_RICHCMP(val1, val2, int op)

    .. deprecated:: 1.0
        Use :c:func:`PY_RETURN_RICHCOMPARE` instead

    A helper for rich comparisons that py3c provided before such a helper was,
    with slightly changed name and semantics,
    `included in Python itself <https://bugs.python.org/issue23699>`_.

    There are no plans to remove :c:macro:`PY3C_RICHCMP` from py3c,
    but before *your project* gets rid of py3c, all calls need to switch
    to what's provided by CPython.

    To switch, instead of::

        return PY3C_RICHCMP(a, b, op)

    write::

        PY_RETURN_RICHCOMPARE(a, b, op)


Types
=====

::

    #include <py3c/tpflags.h>  /* (*NOT* included in <py3c.h>) */

.. _tpflags_ref:

Removed type flags are defined as ``0`` in Python 3, which is
*only* useful in type definitions.

In particular, these macros are *not* suitable for :c:func:`PyType_HasFeature`
in Python 3.

.. c:macro::  Py_TPFLAGS_HAVE_GETCHARBUFFER
.. c:macro::  Py_TPFLAGS_HAVE_SEQUENCE_IN
.. c:macro::  Py_TPFLAGS_HAVE_INPLACEOPS
.. c:macro::  Py_TPFLAGS_CHECKTYPES
.. c:macro::  Py_TPFLAGS_HAVE_RICHCOMPARE
.. c:macro::  Py_TPFLAGS_HAVE_WEAKREFS
.. c:macro::  Py_TPFLAGS_HAVE_ITER
.. c:macro::  Py_TPFLAGS_HAVE_CLASS
.. c:macro::  Py_TPFLAGS_HAVE_INDEX
.. c:macro:: Py_TPFLAGS_HAVE_NEWBUFFER

    | Python 2: (provided), e.g. :data:`Py_TPFLAGS_HAVE_WEAKREFS <py2:Py_TPFLAGS_HAVE_WEAKREFS>`
    | Python 3: ``0``


.. _capsulethunk:

Capsules
========

::

    #include <py3c/capsulethunk.h>  // (*NOT* included in <py3c.h>)

This file provides a PyCapsule API compatibility layer for Python 2.6.

Capsules are simulated in terms of PyCObject. The :doc:`capsulethunk` chapter
lists the limitations of this solution.

.. c:macro:: PyCapsule_Type

    | Python 2.6: :c:type:`PyCObject_Type <py2:PyCObject>`
    | 2.7 and 3.x: :c:type:`(provided) <py3:PyCapsule>`

.. c:macro:: PyCapsule_CheckExact(PyObject *p)

    | Python 2.6: :c:func:`PyCObject_Check <py2:PyCObject_Check>`
    | 2.7 and 3.x: :c:func:`(provided) <py3:PyCapsule_CheckExact>`

.. c:macro:: PyCapsule_IsValid(PyObject *capsule, const char *name)

    | Python 2.6: :c:func:`PyCObject_Check(capsule) <py2:PyCObject_Check>`
    | 2.7 and 3.x: :c:func:`(provided) <py3:PyCapsule_IsValid>`

.. c:macro:: PyCapsule_New(void *pointer, const char *name, PyCapsule_Destructor destructor)

    | Python 2.6: :c:func:`PyCObject_FromVoidPtr(pointer, destructor) <py2:PyCObject_FromVoidPtr>`
    | 2.7 and 3.x: :c:func:`(provided) <py3:PyCapsule_New>`

.. c:macro:: PyCapsule_GetPointer(PyObject *capsule, const char *name)

    | Python 2.6: :c:func:`PyCObject_AsVoidPtr(capsule) <py2:PyCObject_AsVoidPtr>` – name is not checked!
    | 2.7 and 3.x: :c:func:`(provided) <py3:PyCapsule_GetPointer>`

.. c:macro:: PyCapsule_SetPointer(PyObject *capsule, void *pointer)

    | Python 2.6: uses CPython internals; effect similar to :c:func:`py2:PyCObject_SetVoidPtr`
    | 2.7 and 3.x: :c:func:`(provided) <py3:PyCapsule_SetPointer>`

.. c:macro:: PyCapsule_GetDestructor(PyObject *capsule)

    | Python 2.6: uses CPython internals to get the a CObject's destructor
    | 2.7 and 3.x: :c:func:`(provided) <py3:PyCapsule_GetDestructor>`

.. c:macro:: PyCapsule_SetDestructor(PyObject *capsule, PyCapsule_Destructor destructor)

    | Python 2.6: uses CPython internals to replace a CObject's destructor
    | 2.7 and 3.x: :c:func:`(provided) <py3:PyCapsule_SetDestructor>`

.. c:macro:: PyCapsule_GetName(PyObject *capsule)

    | Python 2.6: NULL
    | 2.7 and 3.x: :c:func:`(provided) <py3:PyCapsule_GetName>`

.. c:macro:: PyCapsule_SetName(PyObject *capsule)

    | Python 2.6: Always raises :py:class:`NotImplementedError`
    | 2.7 and 3.x: :c:func:`(provided) <py3:PyCapsule_SetName>`

.. c:macro:: PyCapsule_GetContext(PyObject *capsule)

    | Python 2.6: uses CPython internals to get the CObject "desc" field
    | 2.7 and 3.x: :c:func:`(provided) <py3:PyCapsule_GetContext>`

.. c:macro:: PyCapsule_SetContext(PyObject *capsule, PyCapsule_Destructor destructor)

    | Python 2.6: uses CPython internals to replace CObject "desc" field
    | 2.7 and 3.x: :c:func:`(provided) <py3:PyCapsule_SetContext>`

.. c:macro:: PyCapsule_Import(const char *name, int no_block)

    | Python 2.6: backported
    | 2.7 and 3.x: :c:func:`(provided) <py3:PyCapsule_Import>`


.. _fileshim:

Files
=====

::

    #include <py3c/fileshim.h>  // (*NOT* included in <py3c.h>)

.. c:function:: py3c_PyFile_AsFileWithMode(PyObject *py_file, const char *mode)

    Quick-and-dirty substitute for the removed  :c:func:`py2:PyFile_AsFile`.
    Read the :doc:`file shim <fileshim>` chapter before using.


Shims for New Conveniences in Python 3.4+
=========================================

::

    #include <py3c/py3shims.h>  // (included in <py3c.h>)

Raw Memory Allocation
~~~~~~~~~~~~~~~~~~~~~

Python 3.4 adds several functions to manage “raw” memory in a way that the
Python allocator is aware of it.
These are safe replacements for stdlib's ``malloc``, ``realloc`` and ``free``.
For convenience, ``py3c`` provides backports of these to earlier Python
versions.

The backports provided here simply use the C standard library functions,
except they return unique pointers when zero bytes are requested.

.. c:function:: PyMem_RawMalloc(size_t n)

    Backport of :c:func:`py3:PyMem_RawMalloc`. Replacement for ``malloc``.

.. c:function:: PyMem_RawRealloc(void* ptr, size_t n)

    Backport of :c:func:`py3:PyMem_RawRealloc`. Replacement for ``realloc``.

.. c:function:: PyMem_RawFree(void* ptr)

    Backport of :c:func:`py3:PyMem_Free`. Replacement for ``free``.

.. c:function:: PyMem_RawCalloc(size_t n, size_t s)

    Backport of :c:func:`py3:PyMem_Calloc` from Python 3.5+.
    Replacement for ``calloc``.


Unused Arguments
~~~~~~~~~~~~~~~~

.. c:macro:: Py_UNUSED(arg)

    Use this for unused arguments in a function definition to silence compiler
    warnings, e.g. ``PyObject* func(PyObject *Py_UNUSED(ignored))``.

    Backport of :c:macro:`py3:Py_UNUSED` from Python 3.4+.
