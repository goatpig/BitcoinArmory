..
    Copyright (c) 2015, Red Hat, Inc. and/or its affiliates
    Licensed under CC-BY-SA-3.0; see the license file

.. highlight:: c

.. index::
    double: Porting; PyTypeObject

=======================
Porting extension types
=======================

The extension type structure, PyTypeObject, has seen some changes in Python 3.
You might wish to refresh your memory with the Python documentation on this
(:ref:`Python 2 <py2:type-structs>`, :ref:`Python 3 <py3:type-structs>`);
here we concentrate only on the differences.


.. _tpflags:

Type Flags
==========

The most common incompatibility in type definition involves feature flags
like :data:`Py_TPFLAGS_HAVE_WEAKREFS` and :data:`Py_TPFLAGS_HAVE_ITER`
(see :ref:`Type flags reference <tpflags_ref>` for a full list).

These flags indicate capabilities that are always present in Python 3,
so the macros are only available in Python 2.
Most projects can simply define these to 0 in Python 3.

However, another use of the macros is feature checking, as in
:c:func:`PyType_HasFeature(cls, Py_TPFLAGS_HAVE_ITER) <py2:PyType_HasFeature>`.
Defining the flags to ``0`` would cause that test to fail under Python 3,
where it should instead always succeed!
So, in these cases, the check should be done as
``(IS_PY3 || PyType_HasFeature(cls, Py_TPFLAGS_HAVE_ITER))``.

If your project does not use ``PyType_HasFeature``, or bypasses the check under
Python 3 as above, you can include ``<py3c/tpflags.h>`` to define missing type
flags as 0.


PyTypeObject
============

The differences in PyTypeObject itself are fairly minor.
The ``tp_compare`` field became ``void *tp_reserved``, and is ignored.
If you use ``tp_richcompare``, this field is ignored in Python 2.
It is best set to NULL.

The change can case trouble if you use explicity types during definition
for type safety, as in::

        ...
        (destructor)Example_dealloc,            /* tp_dealloc */
        (printfunc)0,                           /* tp_print */
        (getattrfunc)0,                         /* tp_getattr */
        (setattrfunc)0,                         /* tp_setattr */
        (cmpfunc)0,                             /* tp_compare */
        ...

In this case, make an exception for tp_compare, and use just ``NULL``.

Python 3 also adds new fields at the end of PyTypeObject – but that should
not affect initialization.



.. index::
    double: Porting; PyNumberMethods

PyNumberMethods
===============

The ``PyNumberMethods`` structure, used to implement number-like behavior
and operators, was changed.
(Docs: :c:type:`py2 <py2:PyNumberMethods>`, :c:type:`py3 <py3:PyNumberMethods>`)

Specifically, several members were removed:

    * ``nb_divide`` (Python3 calls ``nb_floor_divide`` or ``nb_true_divide``)
    * ``nb_coerce``
    * ``nb_oct``
    * ``nb_hex``
    * ``nb_inplace_divide`` (see nb_divide)

one was renamed:

    * ``nb_nonzero`` became ``nb_bool``

and one was blanked:

    * ``unaryfunc nb_long`` became ``void *nb_reserved`` and must be NULL.

The mix of removal strategies on the CPython side makes the port somewhat
annoying.

As of yet, the py3c library does not provide helpers for porting
PyNumberMethods. More investigation is needed to be sure all projects' needs
are addressed.

What you need to do depends on your initialization style:

CPython style
.............

This style, used in CPython, works in both old C and C++::

    static PyNumberMethods long_as_number = {
        (binaryfunc)long_add,       /*nb_add*/
        (binaryfunc)long_sub,       /*nb_subtract*/
        (binaryfunc)long_mul,       /*nb_multiply*/
        (binaryfunc)long_div,       /*nb_divide*/
        long_mod,                   /*nb_remainder*/
        long_divmod,                /*nb_divmod*/
        long_pow,                   /*nb_power*/
        (unaryfunc)long_neg,        /*nb_negative*/
        ...

When using this, wrap the removed elements in ``#if !IS IS_PY3``.

If you use nb_long in Python 2, conditionally set it to NULL in Python 3.
Make sure ``nb_int`` is set.

C99 style
.........

If you don't support both C89 and C++ (!) compilers, you may
use the named member initialization feature of C99::

    static PyNumberMethods long_as_number = {
        .tp_add = long_add,
        .tp_div = long_div,
        ...

If this is the case, lump the non-NULL Python2-only members and ``nb_long``
together in a single ``#if !IS IS_PY3`` block.
You will need another ``#if``/``#else`` block to handle to handle both names
of ``nb_nonzero``, if using that.


.. index::
    double: Porting; PyBufferProcs

PyBufferProcs
=============

The buffer protocol changed significantly in Python 3.
Kindly read the :ref:`documentation <py3:bufferobjects>`, and implement
the new buffer protocol for Python 3.

If you find an easier way to port buffer-aware objects,
which other projects could benefit from,
remember that py3c welcomes contributions.
