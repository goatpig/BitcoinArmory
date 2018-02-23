..
    Copyright (c) 2015, Red Hat, Inc. and/or its affiliates
    Licensed under CC-BY-SA-3.0; see the license file

.. highlight:: c

.. index::
    single: Modernization

Modernization
=============

Before porting a C extension to Python 3, you'll need to make sure that
you're not using features deprecated even in Python 2.
Also, many of Python 3's improvements have been backported to Python 2.6,
and using them will make the porting process easier.

For all changes you do, be sure add tests to ensure you do not break anything.


.. index::
    double: Modernization; PyObject structure
    double: Modernization; Objects

PyObject Structure Members
~~~~~~~~~~~~~~~~~~~~~~~~~~

To conform to C's strict aliasing rules, PyObject_HEAD, which provides
members such as ``ob_type`` and ``ob_refcnt``, is a separate struct in
Python 3.
Access to these members is provided by macros, which have been ported to
Python 2.6:

==============  ==============
Instead of      use
==============  ==============
obj->ob_type    Py_TYPE(obj)
obj->ob_refcnt  Py_REFCNT(obj)
obj->ob_size    Py_SIZE(obj)
==============  ==============

And for initialization of type objects, the sequence ::

    PyObject_HEAD_INIT(NULL)
    0, /* ob_size */

must be replaced with ::

    PyVarObject_HEAD_INIT(NULL, 0)


.. index::
    double: Modernization; Constants

Adding module-level constants
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Often, module initialization uses code like this::

    PyModule_AddObject(m, "RDWR", PyInt_FromLong(O_RDWR));
    PyModule_AddObject(m, "__version__", PyString_FromString("6.28"));

Python 2.6 introduced convenience functions, which are shorter to write::

    PyModule_AddIntConstant(m, "RDWR", O_RDWR)
    PyModule_AddStringConstant(m, "__version__", "6.28")

These will use native int and str types in both Python versions.


.. index::
    double: Modernization; Classes

New-Style Classes
~~~~~~~~~~~~~~~~~

The `old-style classes <https://docs.python.org/2/c-api/class.html>`_
(``PyClass_*`` and ``PyInstance_*``) will be removed in Python 3.
Instead, use `type objects <https://docs.python.org/2/c-api/type.html#typeobjects>`_,
which have been available since Python 2.2.


.. index::
    double: Modernization; PyCObject
    double: Modernization; PyCapsule

.. _pycapsule-porting:

PyCObject to PyCapsule
~~~~~~~~~~~~~~~~~~~~~~

The `PyCObject API <https://docs.python.org/3.1/c-api/cobject.html>`_ has been
removed in Python 3.3.
You should instead use its replacement, `PyCapsule <https://docs.python.org/3/c-api/capsule.html#capsules>`_,
which is available in Python 2.7 and 3.1+.
For the rationale behind Capsules, see `CPython issue 5630 <https://bugs.python.org/issue5630>`_.

If you need to support Python 2.6, you can use ``capsulethunk.h``, which
implements the PyCapsule API (with some limitations) in terms of PyCObject.
For instructions, see the chapter :doc:`capsulethunk`.

The port to PyCapsule API should be straightforward:

* Instead of
  :c:func:`PyCObject_FromVoidPtr(obj, destr) <py2:PyCObject_FromVoidPtr>`, use
  :c:func:`PyCapsule_New(obj, name, destr) <py3:PyCapsule_New>`.
  If the capsule will be available as a module attribute, use
  ``"<modulename>.<attrname>"`` for *name*.
  Otherwise, use your best judgment, but try making the name unique.
* Instead of :c:func:`PyCObject_FromVoidPtrAndDesc(obj, desc, destr) <py2:PyCObject_FromVoidPtrAndDesc>`,
  use :c:func:`py3:PyCapsule_New` as above; then call
  :c:func:`PyCapsule_SetContext(obj, desc) <py3:PyCapsule_SetContext>`.
* Instead of :c:func:`PyCObject_AsVoidPtr(obj) <py2:PyCObject_AsVoidPtr>`,
  use :c:func:`PyCapsule_GetPointer(obj, name) <py3:PyCapsule_GetPointer>`.
  You will need to provide a capsule name, which is checked at runtime
  as a form of type safety.
* Instead of :c:func:`py2:PyCObject_GetDesc`,
  use :c:func:`py3:PyCapsule_GetContext`.
* Instead of :c:func:`py2:PyCObject_SetVoidPtr`,
  use :c:func:`py3:PyCapsule_SetPointer`.
* Change all CObject destructors to :c:type:`PyCapsule destructors <py3:PyCapsule_Destructor>`,
  which take the PyCapsule object as their only argument.


.. index::
    double: Modernization; Comparisons

Comparisons
~~~~~~~~~~~

.. note::

    In previous versions, this chapter talked about rich comparison,
    but those changes are better left to :ref:`porting <comparison-porting>`.


Done!
~~~~~

When your project is sufficiently modernized, and the tests still pass under
Python 2, you're ready to start the actual :doc:`Porting <guide-porting>`.
