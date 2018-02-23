..
    Copyright (c) 2015, Red Hat, Inc. and/or its affiliates
    Licensed under CC-BY-SA-3.0; see the license file


~~~~~~~~~~~~~~~~~~~
The py3c Cheatsheet
~~~~~~~~~~~~~~~~~~~


Strings
~~~~~~~

* PyStr_* – for human-readable strings

* PyBytes_* – for binary data

* PyUnicode_* – when you used ``unicode`` in Python 2

* PyString_* – when you don't care about Python 3 yet

Use PyStr_AsUTF8AndSize to get a char* and its length.


Ints
~~~~

Use whatever you used in Python 2. For py3-only code, use ``PyLong``.


Floats
~~~~~~

Don't pass the useless second argument to :c:func:`PyFloat_FromString` as
you needed to do in Python 2.


Comparisons
~~~~~~~~~~~

Use `rich comparisons <https://www.python.org/dev/peps/pep-0207/>`_::

    static PyObject* mytype_richcmp(PyObject *obj1, PyObject *obj2, int op)
    {
        if (mytype_Check(obj2)) {
            PY_RETURN_RICHCOMPARE(get_data(obj1), get_data(obj2), op);
        }
        Py_RETURN_NOTIMPLEMENTED;
    }

::

    .tp_richcompare = mytype_richcmp


Objects & Types
~~~~~~~~~~~~~~~

==============================  ================================
Instead of                      use
==============================  ================================
obj->ob_type                    Py_TYPE(obj)
obj->ob_refcnt                  Py_REFCNT(obj)
obj->ob_size                    Py_SIZE(obj)
PyVarObject_HEAD_INIT(NULL, 0)  PyObject_HEAD_INIT(NULL), 0
==============================  ================================


Module initialization
~~~~~~~~~~~~~~~~~~~~~

::

    static struct PyModuleDef moduledef = {
        PyModuleDef_HEAD_INIT,
        .m_name = "spam",
        .m_doc = PyDoc_STR("Python wrapper for the spam submodule."),
        .m_size = -1,
        .m_methods = spam_methods,
    };

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


CObject
~~~~~~~

Use the `PyCapsule <https://docs.python.org/3/c-api/capsule.html#capsules>`_ API.
If you need to support 2.6, see the chapter :doc:`capsulethunk`.
