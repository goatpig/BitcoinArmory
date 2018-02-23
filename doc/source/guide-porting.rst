..
    Copyright (c) 2015, Red Hat, Inc. and/or its affiliates
    Licensed under CC-BY-SA-3.0; see the license file

.. highlight:: c

.. index::
    single: Porting

Porting – Adding Support for Python 3
=====================================

After you :doc:`modernize <guide-modernization>` your C extension to use the
latest features available in Python 2, it is time to address the differences
between Python 2 and 3.

The recommended way to port is keeping single-source compatibility between
Python 2 and 3, until support Python 2 can be safely dropped.
For Python code, you can use libraries like `six <https://pypi.python.org/pypi/six/>`_
and `future <http://python-future.org/>`_, and, failing that,
``if sys.version_info >= (3, 0):`` blocks for conditional code.
For C, the py3c library provides common tools, and for special cases you can use
conditional compilation with ``#if IS_PY3``.

To start using py3c, ``#include <py3c.h>``, and instruct your compiler to
find the header.


.. index::
    double: Porting; Strings
    double: Porting; Bytes
    double: Porting; Unicode

The Bytes/Unicode split
~~~~~~~~~~~~~~~~~~~~~~~

The most painful change for extension authors is the bytes/unicode split:
unlike Python 2's ``str`` or C's ``char*``, Python 3 introduces a sharp divide
between *human-readable strings* and *binary data*.
You will need to decide, for each string value you use, which of these two
types you want.

Make the division as sharp as possible: mixing the types tends to lead to utter chaos.
Functions that take both Unicode strings and bytes (in a single Python version)
should be rare, and should generally be convenience functions in your interface;
not code deep in the internals.

However, you can use a concept of **native strings**: a type that corresponds
to the ``str`` type in Python: PyBytes on Python 2, and PyUnicode in Python 3.
This is the type that you will need to return from functions like ``__str__``
and ``__repr__.

Using the **native string** extensively is suitable for conservative projects:
it affects the semantics under Python 2 as little as possible, while not
requiring the resulting Python 3 API to feel contorted.

With py3c, functions for the native string type are PyStr_* (PyStr_FromString,
PyStr_Type, PyStr_Check, etc.). They correspond to
`PyString <https://docs.python.org/2/c-api/string.html>`_ on Python 2,
and `PyUnicode <https://docs.python.org/3/c-api/unicode.html>`_ on Python 3.
The supported API is the intersection of `PyString_* <https://docs.python.org/2/c-api/string.html>`_
and `PyUnicode_* <https://docs.python.org/3/c-api/unicode.html>`_,
except PyStr_Size (see below) and the deprecated PyUnicode_Encode;
additionally `PyStr_AsUTF8String <https://docs.python.org/3/c-api/unicode.html#c.PyUnicode_AsUTF8String>`_ is defined.

Keep in mind py3c expects that native strings are always encoded with ``utf-8``
under Python 2. If you use a different encoding, you will need to convert
between bytes and text manually.

For binary data, use PyBytes_* (PyBytes_FromString, PyBytes_Type, PyBytes_Check,
etc.). Python 3.x provides them under these names only; in Python 2.6+ they are
aliases of PyString_*. (For even older Pythons, py3c also provides these aliases.)
The supported API is the intersection of `PyString_* <https://docs.python.org/2/c-api/string.html>`_
and `PyBytes_* <https://docs.python.org/3/c-api/bytes.html>`_,

Porting mostly consists of replacing "``PyString_``" to either "``PyStr_``"
or "``PyBytes_``"; just see the caveat about size below.

To summarize the four different string type names:

============ ============= ============== ===================
String kind  py2           py3            Use
============ ============= ============== ===================
PyStr_*      PyString_*    PyUnicode_*    Human-readable text
PyBytes_*    PyString_*    ✔              Binary data
PyUnicode_*  ✔             ✔              Unicode strings
PyString_*   ✔             error          In unported code
============ ============= ============== ===================


.. index::
    double: Porting; String Size

String size
~~~~~~~~~~~

When dealing with Unicode strings, the concept of “size” is tricky,
since the number of characters doesn't necessarily correspond to the
number of bytes in the string's UTF-8 representation.

To prevent subtle errors, this library does *not* provide a
PyStr_Size function.

Instead, use PyStr_AsUTF8AndSize. This functions like Python 3's
`PyUnicode_AsUTF8AndSize <https://docs.python.org/3/c-api/unicode.html#c.PyUnicode_AsUTF8AndSize>`_,
except under Python 2, the string is not encoded (as it should already be in UTF-8),
the size pointer must not be NULL, and the size may be stored even if an error occurs.


.. index::
    double: Porting; Ints
    double: Porting; Long

Ints
~~~~

While string type is split in Python 3, the int is just the opposite:
``int`` and ``long`` were unified. ``PyInt_*`` is gone and only
``PyLong_*`` remains (and, to confuse things further, PyLong is named "int"
in Python code).
The py3c headers alias PyInt to PyLong, so if you're using them,
there's no need to change at this point.


.. index::
    double: Porting; Argument parsing
    double: Porting; PyArg_Parse
    double: Porting; Py_BuildValue

Floats
~~~~~~

In Python 3, the function :c:func:`PyFloat_FromString <PyFloat_FromString>`
lost its second, ignored argument.

The py3c headers redefine the function to take one argument even in Python 2.
You will need to remove the excess argument from all calls.


Argument Parsing
~~~~~~~~~~~~~~~~

The format codes for argument-parsing functions of the PyArg_Parse family
have changed somewhat.

In Python 3, the ``s``, ``z``, ``es``, ``es#`` and ``U`` (plus the new ``C``)
codes accept only Unicode strings, while ``c`` and ``S`` only accept bytes.

Formats accepting Unicode strings usually encode to char* using UTF-8.
Specifically, these are ``s``, ``s*``, ``s#``, ``z``, ``z*``, ``z#``, and also
``es``, ``et``, ``es#``, and ``et#`` when the encoding argument is set to NULL.
In Python 2, the default encoding was used instead.

There is no variant of ``z`` for bytes, which means htere's no built-in way to
accept "bytes or NULL" as a ``char*``.
If you need this, write an ``O&`` converter.

Python 2 lacks an ``y`` code, which, in Python 3, works on byte objects.
The use cases needing ``bytes`` in Python 3 and ``str`` in Python 2 should be
rare; if needed, use ``#ifdef IS_PY3`` to select a compatible PyArg_Parse call.

..
    XXX: Write an O& converter for "z" and "y"
    XXX: Write/document handling pathnames safely and portably; see
        PyUnicode_FSConverter/PyUnicode_FSDecoder

Compare the `Python 2 <https://docs.python.org/2/c-api/arg.html>`_ and `Python 3 <https://docs.python.org/3/c-api/arg.html>`_
docs for full details.


.. index::
    double: Porting; Module Initialization


Defining Extension Types
~~~~~~~~~~~~~~~~~~~~~~~~

If your module defines extension types, i.e. variables of type ``PyTypeObject``
(and related structures like ``PyNumberMethods`` and ``PyBufferProcs``),
you might need to make changes to these definitions.
Please read the :doc:`Extension types <ext-types>` guide for details.

A common incompatibility comes from type flags, like
:data:`Py_TPFLAGS_HAVE_WEAKREFS` and :data:`Py_TPFLAGS_HAVE_ITER`,
which are removed in Python 3 (where the functionality is always present).
If you are only using these flags in type definitions,
(and *not* for example in :c:func:`PyType_HasFeature`),
you can include ``<py3c/tpflags.h>`` to define them to zero under Python 3.
For more information, read the :ref:`Type flags <tpflags>` section.


Module initialization
~~~~~~~~~~~~~~~~~~~~~

The module creation process was overhauled in Python 3.
py3c provides a compatibility wrapper so most of the Python 3 syntax can
be used.

PyModuleDef and PyModule_Create
-------------------------------

Module object creation with py3c is the same as in Python 3.

First, create a PyModuleDef structure::

    static struct PyModuleDef moduledef = {
        PyModuleDef_HEAD_INIT,  /* m_base */
        "spam",                 /* m_name */
        NULL,                   /* m_doc */
        -1,                     /* m_size */
        spam_methods            /* m_methods */
    };

Then, where a Python 2 module would have ::

    m = Py_InitModule3("spam", spam_methods, "Python wrapper ...");

use instead ::

    m = PyModule_Create(&moduledef);

For ``m_size``, use -1. (If you are sure the module supports multiple
subinterpreters, you can use 0, but this is tricky to achieve portably.)
Additional members of the PyModuleDef structure are not accepted under Python 2.

See `Python documentation <https://docs.python.org/3/c-api/module.html#initializing-c-modules_>`_
for details on PyModuleDef and PyModule_Create.

Module creation entrypoint
--------------------------

Instead of the ``void init<name>`` function in Python 2, or a Python3-style
``PyObject *PyInit_<name>`` function, use the MODULE_INIT_FUNC macro
to define an initialization function, and return the created module from it::

    MODULE_INIT_FUNC(name)
    {
        ...
        m = PyModule_Create(&moduledef);
        ...
        if (error) {
            return NULL;
        }
        ...
        return m;
    }


.. index::
    double: Porting; Comparisons

.. _comparison-porting:

Comparisons
~~~~~~~~~~~

Python 2.1 introduced *rich comparisons* for custom objects, allowing separate
behavior for the ``==``, ``!=``, ``<``, ``>``, ``<=``, ``>=`` operators,
rather than calling one ``__cmp__`` function and interpreting its result
according to the requested operation.
(See `PEP 207 <https://www.python.org/dev/peps/pep-0207/>`_ for details.)

In Python 3, the original ``__cmp__``-based object comparison is removed,
so all code needs to switch to rich comparisons. Instead of a ::

    static int cmp(PyObject *obj1, PyObject *obj2)

function in the ``tp_compare`` slot, there is now a ::

    static PyObject* richcmp(PyObject *obj1, PyObject *obj2, int op)

in the ``tp_richcompare`` slot. The ``op`` argument specifies the comparison
operation: ``Py_EQ`` (==), ``Py_GT`` (>), ``Py_LE`` (<=), etc.

Additionally, Python 3 brings a semantic change. Previously, objects of
disparate types were ordered according to type, where the ordering of types
was undefined (but consistent across, at least, a single invocation of Python).
In Python 3, objects of different types are unorderable.
It is usually possible to write a comparison function that works for both
versions by returning NotImplemented to explicitly fall back to default
behavior.

To help writing rich comparisons, Python 3.7+ provides a convenience macro,
``Py_RETURN_RICHCOMPARE``, which returns the right
``PyObject *`` result based on two values orderable by C's comparison operators.
With py3c, the macro is available for older versions as well.
A typical rich comparison function will look something like this::

    static PyObject* mytype_richcmp(PyObject *obj1, PyObject *obj2, int op)
    {
        if (mytype_Check(obj2)) {
            Py_RETURN_RICHCOMPARE(get_data(obj1), get_data(obj2), op);
        }
        Py_RETURN_NOTIMPLEMENTED;
    }

where ``get_data`` returns an orderable C value (e.g. a pointer or int), and
mytype_Check checks if ``get_data`` is of the correct type
(usually via PyObject_TypeCheck). Note that the first argument, obj1,
is guaranteed to be of the type the function is defined for.

If a "cmp"-style function is provided by the C library, compare its result to 0,
e.g. ::

    Py_RETURN_RICHCOMPARE(mytype_cmp(obj1, obj2), 0, op)

The :c:macro:`py3:Py_RETURN_RICHCOMPARE` and
:c:macro:`py3:Py_RETURN_NOTIMPLEMENTED` macros are provided in Python 3.3+
and 3.7+, respectively;
py3c makes them available to older versions as well.

If you need more complicated comparison, use the :c:macro:`py3:Py_UNREACHABLE`
macro for unknown operation types (``op``).
The macro is was added in Python 3.7+, and py3c backports it.

.. note::

    The ``tp_richcompare`` slot is inherited in subclasses together with
    ``tp_hash`` and (in Python 2) ``tp_compare``: iff
    the subclass doesn't define any of them, all are inherited.

    This means that if a class is modernized, its subclasses don't have to be,
    *unless* the subclass manipulates compare/hash slots after
    class creation (e.g. after the :c:func:`PyType_Ready <py3:PyType_Ready>`
    call).

.. note::

    For backwards compatibility with previous versions of itself,
    py3c provides the :c:macro:`PY3C_RICHCMP` macro,
    an early draft of what became ``Py_RETURN_RICHCOMPARE``.


The File API
~~~~~~~~~~~~

The :c:type:`PyFile <py2:PyFileObject>` API was severely reduced
:c:func:`in Python 3 <py3:PyFile_FromFd>`.
The new version is specifically intended for internal error reporting
in Python.

Native Python file objects are officially no longer backed by ``FILE*``.

Use the Python API from the :py:mod:`py3:io` module instead of handling files
in C. The Python API supports all kinds of file-like objects, not just
built-in files – though, admittedly, it's cumbersome to use from plain C.

If you really need to access an API that deals with ``FILE*`` only
(e.g. for debugging), see py3c's limited :doc:`file API shim <fileshim>`.


Other changes
~~~~~~~~~~~~~

If you find a case where py3c doesn't help, use ``#if IS_PY3`` to include
code for only one or the other Python version.
And if your think others might have the same problem,
consider contributing a macro and docs to py3c!


.. index:: Building, ABI tags

Building
~~~~~~~~

When building your extension, note that Python 3.2 introduced ABI version tags
(`PEP 3149 <https://www.python.org/dev/peps/pep-3149/>`_), which can be added
to shared library filenames to ensure that the library is loaded with the
correct Python version. For example, instead of ``foo.so``, the shared library
for the extension module ``foo`` might be named ``foo.cpython-33m.so``.

Your buildsystem might generate these for you already, but if you need to
modify it, you can get the tags from ``systonfig``::

    >>> import sysconfig
    >>> sysconfig.get_config_var('EXT_SUFFIX')
    '.cpython-34m.so'
    >>> sysconfig.get_config_var('SOABI')
    'cpython-34m'

This is completely optional; the old filenames without ABI tags are
still valid.


Done!
~~~~~

Do your tests now pass under both Python 2 and 3?
(And do you have enough tests?)
Then you're done porting!

Once you decide to drop compatibility with Python 2,
you can move to the :doc:`Cleanup <guide-cleanup>` section.
