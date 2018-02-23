..
    Copyright (c) 2015, Red Hat, Inc. and/or its affiliates
    Copyright (c) 2001-2015 Python Software Foundation; All Rights Reserved
    Licensed under CC-BY-SA-3.0; see the license file

.. highlight:: c

PyCapsule API for Python 2.6
============================

The ``capsulethunk.h`` header implements the PyCapsule API
(with some limitations) in terms of PYCObject.
It is only necessary for compatibility with Python 2.6 (or 3.0).

.. note::
    The ``capsulethunk.h`` header and this documentation was written by
    Larry Hastings for the Python documentation. [#issue13053]_
    It is now maintained as part of the py3c project. [#issue24937]_

.. index::
    single: capsulethunk

CObject replaced with Capsule
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The :c:type:`Capsule <py3:PyCapsule>` object was introduced in Python 3.1 and
2.7 to replace :c:type:`CObject <py2:PyCObject>`.  CObjects were useful,
but the :c:type:`CObject <py2:PyCObject>` API was problematic: it didn't permit
distinguishing between valid CObjects, which allowed mismatched CObjects to
crash the interpreter, and some of its APIs relied on undefined behavior in C.
(For further reading on the rationale behind Capsules, please see
`CPython issue 5630 <https://bugs.python.org/issue5630>`_.)

If you're currently using CObjects, and you want to migrate to Python 3,
you'll need to switch to Capsules.
See the :ref:`PyCObject section <pycapsule-porting>` in the porting guide
for instructions.

:c:type:`CObject <py2:PyCObject>` was deprecated in 3.1 and 2.7 and completely
removed in Python 3.2.  So, if you need to support versions of
Python earlier than 2.7, or Python 3.0,
you'll have to support both CObjects and Capsules.

The following example header file :file:`capsulethunk.h` may
solve the problem for you.  Simply write your code against the
:c:type:`Capsule <py3:PyCapsule>` API and include this header file after
:file:`Python.h`.  Your code will automatically use Capsules
in versions of Python with Capsules, and switch to CObjects
when Capsules are unavailable.

If you're using py3c, you will need to explicitly
``#include <py3c/capsulethunk.h>``. The file is not included from ``py3c.h``.

Since :c:type:`CObject <py2:PyCObject>` provides no place to store the
capsule's "name", the simulated :c:type:`Capsule <py3:PyCapsule>` objects
created by :file:`capsulethunk.h` behave slightly differently from real
Capsules.  Specifically:

  * The name parameter passed in to :c:func:`PyCapsule_New` is ignored.

  * The name parameter passed in to :c:func:`PyCapsule_IsValid` and
    :c:func:`PyCapsule_GetPointer` is ignored, and no error checking
    of the name is performed.

  * :c:func:`PyCapsule_GetName` always returns NULL.

  * :c:func:`PyCapsule_SetName` always raises an exception and
    returns failure.  (Since there's no way to store a name
    in a CObject, noisy failure of :c:func:`PyCapsule_SetName`
    was deemed preferable to silent failure here.)
    If this is inconvenient, feel free to modify your local copy
    as you see fit.

You can find :file:`capsulethunk.h` at
`include/py3c/capsulethunk.h <https://github.com/encukou/py3c/blob/master/include/py3c/capsulethunk.h>`_.
We also include it here for your convenience:

.. literalinclude:: ../../include/py3c/capsulethunk.h

---------

.. rubric:: Footnotes

.. [#issue13053] `CPython issue 13053 <http://bugs.python.org/issue13053>`_: Add Capsule migration documentation to "cporting"
.. [#issue24937] `CPython issue 24937 <http://bugs.python.org/issue24937>`_: Multiple problems in getters & setters in capsulethunk.h
