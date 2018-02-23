#include <Python.h>
#include "structmember.h"
#include <py3c.h>
#include <py3c/capsulethunk.h>
#include <py3c/fileshim.h>
#include <py3c/tpflags.h>

#define UTF8_STRING "test string \xe1\xba\x87\xc3\xad\xc5\xa5\xc4\xa7 \xc5\xae\xc5\xa2\xe1\xb8\x9e\xe2\x88\x9e \xe2\x98\xba"
#define FORMAT_STRING "<%s:%d>"

static const char* test_ptr = "here is some data";
static const char* test_ptr2 = "here is some other data";
static int capsule_count = 0;

static PyObject *str_check(PyObject *mod, PyObject * o) {
	return PyBool_FromLong(PyStr_Check(o));
}

static PyObject *str_checkexact(PyObject *mod, PyObject * o) {
	return PyBool_FromLong(PyStr_CheckExact(o));
}

/* Note: this also tests that Py_UNUSED works */
static PyObject *str_fromstring(PyObject *mod, PyObject *Py_UNUSED(o)) {
	return PyStr_FromString(UTF8_STRING);
}

static PyObject *str_fromstringandsize(PyObject *mod, PyObject * o) {
	return PyStr_FromStringAndSize(UTF8_STRING, PyInt_AsLong(o));
}

static PyObject *str_fromformat(PyObject *mod) {
	return PyStr_FromFormat(FORMAT_STRING, "abc", 3);
}

static PyObject *_fromformatv(const char *fmt, ...) {
	va_list valist;
	va_start(valist, fmt);
	return PyStr_FromFormatV(fmt, valist);
}

static PyObject *str_fromformatv(PyObject *mod) {
	return _fromformatv(FORMAT_STRING, "abc", 3);
}

static PyObject *str_asstring_check(PyObject *mod, PyObject * o) {
	const char *str = PyStr_AsString(o);
	if (!str) return NULL;
	return PyBool_FromLong(!strcmp(str, UTF8_STRING));
}

static PyObject *str_concat(PyObject *mod, PyObject *args) {
	PyObject *left, *right;
	if (!PyArg_ParseTuple(args, "OO", &left, &right)) return  NULL;
	return PyStr_Concat(left, right);
}

static PyObject *str_format(PyObject *mod, PyObject *args) {
	PyObject *fmt, *items;
	if (!PyArg_ParseTuple(args, "OO", &fmt, &items)) return  NULL;
	return PyStr_Format(fmt, items);
}

static PyObject *str_interninplace(PyObject *mod, PyObject *o) {
	/* we have a borrowed reference; need to own it */
	Py_INCREF(o);
	PyStr_InternInPlace(&o);
	return o;
}

static PyObject *str_internfromstring(PyObject *mod) {
	return PyStr_InternFromString(UTF8_STRING);
}

static PyObject *str_decode(PyObject *mod, PyObject *args) {
	int size;
	const char *errors;
	if (!PyArg_ParseTuple(args, "Is", &size, &errors)) return  NULL;
	return PyStr_Decode(UTF8_STRING, size, "utf-8", errors);
}

static PyObject *str_asutf8string(PyObject *mod, PyObject *o) {
	return PyStr_AsUTF8String(o);
}

static PyObject *str_asutf8_check(PyObject *mod, PyObject * o) {
	const char *str = PyStr_AsUTF8(o);
	if (!str) return NULL;
	return PyBool_FromLong(!strcmp(str, UTF8_STRING));
}

static PyObject *str_asutf8andsize_check(PyObject *mod, PyObject * o) {
	Py_ssize_t size;
	const char *str = PyStr_AsUTF8AndSize(o, &size);
	if (!str) return NULL;
	return PyBool_FromLong(!strcmp(str, UTF8_STRING) && size == 36);
}

static PyObject *int_check(PyObject *mod, PyObject * o) {
	return PyBool_FromLong(PyInt_Check(o));
}

static PyObject *int_checkexact(PyObject *mod, PyObject * o) {
	return PyBool_FromLong(PyInt_CheckExact(o));
}

static PyObject *int_fromstring(PyObject *mod) {
	char eight[] = "8";
	return PyInt_FromString(eight, NULL, 10);
}

static PyObject *int_fromlong(PyObject *mod) {
	return PyInt_FromLong(8);
}

static PyObject *int_fromssize_t(PyObject *mod) {
	return PyInt_FromSsize_t((Py_ssize_t)8);
}

static PyObject *int_fromsize_t(PyObject *mod) {
	return PyInt_FromSize_t((size_t)8);
}

static PyObject *int_aslong_check(PyObject *mod, PyObject * o) {
	return PyBool_FromLong(PyInt_AsLong(o) == 8);
}

static PyObject *int_aslong_macro_check(PyObject *mod, PyObject * o) {
	return PyBool_FromLong(PyInt_AS_LONG(o) == 8);
}

static PyObject *int_asunsignedlonglongmask_check(PyObject *mod, PyObject * o) {
	return PyBool_FromLong(PyInt_AsUnsignedLongLongMask(o) == 8);
}

static PyObject *int_asssize_t_check(PyObject *mod, PyObject * o) {
	return PyBool_FromLong(PyInt_AsSsize_t(o) == 8);
}


static PyObject *float_fromstring(PyObject *mod, PyObject *o) {
	return PyFloat_FromString(o);
}


static PyObject *return_notimplemented(PyObject *mod) {
	Py_RETURN_NOTIMPLEMENTED;
}


static PyObject *capsule_get_count(PyObject *mod) {
	return PyInt_FromLong(capsule_count);
}

static void capsule_destructor(PyObject *capsule) {
	const char *name = PyCapsule_GetName(capsule);
	(void) name;
	if (!PyErr_Occurred()) {
		capsule_count--;
	} else {
		/* Better way to signal failure? */
		capsule_count += 999;
	}
}

static void capsule_alternate_destructor(PyObject *capsule) {
	const char *name = PyCapsule_GetName(capsule);
	(void) name;
	if (!PyErr_Occurred()) {
		capsule_count -= 2;
	} else {
		/* Better way to signal failure? */
		capsule_count += 999;
	}
}

static PyObject *capsule_new(PyObject *mod) {
	capsule_count++;
	return PyCapsule_New((void*)test_ptr, "test_capsule", capsule_destructor);
}

static PyObject *capsule_new_nullname(PyObject *mod) {
	capsule_count++;
	return PyCapsule_New((void*)test_ptr, NULL, capsule_destructor);
}

static PyObject *capsule_getpointer_check(PyObject *mod, PyObject *o) {
	void * ptr = PyCapsule_GetPointer(o, "test_capsule");
	if (ptr == NULL && PyErr_Occurred()) return NULL;
	return PyBool_FromLong(ptr == test_ptr);
}

static PyObject *capsule_getpointer_check_other(PyObject *mod, PyObject *o) {
	void * ptr = PyCapsule_GetPointer(o, "test_capsule");
	if (ptr == NULL && PyErr_Occurred()) return NULL;
	return PyBool_FromLong(ptr == test_ptr2);
}

static PyObject *capsule_getpointer_nullname_check(PyObject *mod, PyObject *o) {
	void * ptr = PyCapsule_GetPointer(o, NULL);
	if (ptr == NULL && PyErr_Occurred()) return NULL;
	return PyBool_FromLong(ptr == test_ptr);
}

static PyObject *capsule_getdestructor_check(PyObject *mod, PyObject *o) {
	void * ptr = (void*) PyCapsule_GetDestructor(o);
	if (ptr == NULL && PyErr_Occurred()) return NULL;
	return PyBool_FromLong(ptr == capsule_destructor);
}

static PyObject *capsule_getcontext(PyObject *mod, PyObject *o) {
	void * ptr = (void*) PyCapsule_GetContext(o);
	if (ptr == NULL && PyErr_Occurred()) return NULL;
	if (ptr == NULL) Py_RETURN_NONE;
	return PyStr_FromString((char*) ptr);
}

static PyObject *capsule_setcontext(PyObject *mod, PyObject *args) {
	PyObject *o;
	int ret;
	const char *ptr;
	if (!PyArg_ParseTuple(args, "Os", &o, &ptr)) return  NULL;
	ret = PyCapsule_SetContext(o, (void*)ptr);
	if (ret) return NULL;
	Py_RETURN_NONE;
}

static PyObject *capsule_getname(PyObject *mod, PyObject *o) {
	const char* ptr = PyCapsule_GetName(o);
	if (ptr == NULL && PyErr_Occurred()) return NULL;
	if (ptr == NULL) Py_RETURN_NONE;
	return PyStr_FromString(ptr);
}

static PyObject *capsule_valid(PyObject *mod, PyObject *o) {
	return PyBool_FromLong(PyCapsule_IsValid(o, "test_capsule"));
}

static PyObject *capsule_setdestructor(PyObject *mod, PyObject *o) {
	int ret = PyCapsule_SetDestructor(o, capsule_alternate_destructor);
	if (ret) return NULL;
	capsule_count ++;
	Py_RETURN_NONE;
}

static PyObject *capsule_setname(PyObject *mod, PyObject *args) {
	PyObject *o;
	int ret;
	const char *ptr;
	if (!PyArg_ParseTuple(args, "Os", &o, &ptr)) return  NULL;
	ret = PyCapsule_SetName(o, ptr);
	if (ret) return NULL;
	Py_RETURN_NONE;
}

static PyObject *capsule_setpointer(PyObject *mod, PyObject *o) {
	int ret = PyCapsule_SetPointer(o, (void*)test_ptr2);
	if (ret) return NULL;
	Py_RETURN_NONE;
}

static PyObject *capsule_import_check(PyObject *mod, PyObject *args) {
	const char *name;
	void *ptr;
	if (!PyArg_ParseTuple(args, "s", &name)) return  NULL;
	ptr = PyCapsule_Import(name, 0);
	if (ptr == NULL && PyErr_Occurred()) return NULL;
	return PyBool_FromLong(ptr == test_ptr2);
}

static PyObject *capsule_type_check(PyObject *mod, PyObject *o) {
	return PyBool_FromLong(PyObject_TypeCheck(o, &PyCapsule_Type));
}


static PyObject *file_asfilewithmode(PyObject *mod, PyObject *args) {
	PyObject *o;
	const char *mode;
	FILE *f;
	if (!PyArg_ParseTuple(args, "Os", &o, &mode)) return NULL;
	f = py3c_PyFile_AsFileWithMode(o, mode);
	if (!f) return NULL;
	return PyCapsule_New(f, "test_py3c._file", NULL);
}

static PyObject *file_fread(PyObject *mod, PyObject *o) {
	char data[200];
	size_t size;
	FILE *f = (FILE*) PyCapsule_GetPointer(o, "test_py3c._file");
	if (!f) return NULL;
	size = fread(data, 1, 200, f);
	if (size > 199) {
		return PyErr_Format(PyExc_AssertionError,
			"size read is too big (%zu)", size);
	}
	return PyStr_FromStringAndSize(data, size);
}

static PyObject *file_fwrite(PyObject *mod, PyObject *args) {
	PyObject *o;
	Py_buffer pybuf;
	size_t size_written;
	FILE *f;
	if (!PyArg_ParseTuple(args, "Os*", &o, &pybuf)) return NULL;
	f = (FILE*) PyCapsule_GetPointer(o, "test_py3c._file");
	if (!f) {
		PyBuffer_Release(&pybuf);
		return NULL;
	}
	size_written = fwrite(pybuf.buf, 1, pybuf.len, f);
	if (size_written != (size_t)pybuf.len) {
		PyBuffer_Release(&pybuf);
		return PyErr_Format(PyExc_AssertionError,
			"size written (%zu) != size of string (%zd)",
			size_written, pybuf.len);
	}
	fflush(f);
	PyBuffer_Release(&pybuf);
	Py_RETURN_NONE;
}

static PyObject *file_fseek(PyObject *mod, PyObject *args) {
	PyObject *o;
	size_t pos = 0;
	FILE *f;
	int ret;
	if (!PyArg_ParseTuple(args, "Oi", &o, &pos)) return NULL;
	f = (FILE*) PyCapsule_GetPointer(o, "test_py3c._file");
	if (!f) return NULL;
	ret = fseek(f, pos, SEEK_SET);
	if (ret < 0) return PyErr_SetFromErrno(PyExc_OSError);
	Py_RETURN_NONE;
}

static PyObject *test_raw_malloc(PyObject *mod, PyObject *args) {
	char *buf = (char*)PyMem_RawMalloc(1);
	buf[0] = 'a';
	PyMem_RawFree(buf);
	Py_RETURN_NONE;
}

static PyObject *test_raw_calloc(PyObject *mod, PyObject *args) {
	char *buf = (char*)PyMem_RawCalloc(1, 1);
	buf[0] = 'a';
	PyMem_RawFree(buf);
	Py_RETURN_NONE;
}

static PyObject *test_raw_realloc(PyObject *mod, PyObject *args) {
	char *buf = (char*)PyMem_RawMalloc(1);
	buf[0] = 'a';
	buf = (char*)PyMem_RawRealloc(buf, 2);
	if (buf[0] != 'a') {
		return PyErr_Format(PyExc_AssertionError,
			"memory not preserved in PyMem_RawRealloc");
    }
	PyMem_RawFree(buf);
	Py_RETURN_NONE;
}

static PyObject *test_raw_realloc_null(PyObject *mod, PyObject *args) {
	char *buf = (char*)PyMem_RawRealloc(NULL, 2);
	buf[0] = 'a';
	PyMem_RawFree(buf);
	Py_RETURN_NONE;
}

static PyObject *test_raw_malloc_zerosize(PyObject *mod, PyObject *args) {
	char *buf = (char*)PyMem_RawMalloc(0);
	if (buf == NULL) {
		return PyErr_Format(PyExc_AssertionError,
			"PyMem_RawMalloc(0) returned NULL");
    }
	PyMem_RawFree(buf);
	Py_RETURN_NONE;
}

static PyObject *test_raw_calloc_zerosize(PyObject *mod, PyObject *args) {
	char *buf = (char*)PyMem_RawCalloc(0, 0);
	if (buf == NULL) {
		return PyErr_Format(PyExc_AssertionError,
			"PyMem_RawCalloc(0) returned NULL");
    }
	PyMem_RawFree(buf);
	Py_RETURN_NONE;
}

static PyObject *test_raw_realloc_zerosize(PyObject *mod, PyObject *args) {
	char *buf = (char*)PyMem_RawMalloc(1);
	buf = (char*)PyMem_RawRealloc(buf, 0);
	if (buf == NULL) {
		return PyErr_Format(PyExc_AssertionError,
			"PyMem_RawRealloc(0) returned NULL");
    }
	PyMem_RawFree(buf);
	Py_RETURN_NONE;
}

static PyObject *test_unreachable(PyObject *mod) {
    Py_UNREACHABLE();
}

#define FUNC(style, name) { #name, (PyCFunction)name, style, NULL }

static PyMethodDef methods[] = {
	FUNC(METH_O, str_check),
	FUNC(METH_O, str_checkexact),
	FUNC(METH_VARARGS, str_fromstring),
	FUNC(METH_O, str_fromstringandsize),
	FUNC(METH_NOARGS, str_fromformat),
	FUNC(METH_NOARGS, str_fromformatv),
	FUNC(METH_O, str_asstring_check),
	FUNC(METH_VARARGS, str_concat),
	FUNC(METH_VARARGS, str_format),
	FUNC(METH_O, str_interninplace),
	FUNC(METH_O, str_internfromstring),
	FUNC(METH_VARARGS, str_decode),
	FUNC(METH_O, str_asutf8string),
	FUNC(METH_O, str_asutf8_check),
	FUNC(METH_O, str_asutf8andsize_check),

	FUNC(METH_O, int_check),
	FUNC(METH_O, int_checkexact),
	FUNC(METH_NOARGS, int_fromstring),
	FUNC(METH_NOARGS, int_fromlong),
	FUNC(METH_NOARGS, int_fromssize_t),
	FUNC(METH_NOARGS, int_fromsize_t),
	FUNC(METH_O, int_aslong_check),
	FUNC(METH_O, int_aslong_macro_check),
	FUNC(METH_O, int_asunsignedlonglongmask_check),
	FUNC(METH_O, int_asssize_t_check),

    FUNC(METH_O, float_fromstring),

	FUNC(METH_NOARGS, return_notimplemented),
	FUNC(METH_NOARGS, test_unreachable),

	FUNC(METH_NOARGS, capsule_get_count),
	FUNC(METH_NOARGS, capsule_new),
	FUNC(METH_NOARGS, capsule_new_nullname),
	FUNC(METH_O, capsule_getpointer_check),
	FUNC(METH_O, capsule_getpointer_check_other),
	FUNC(METH_O, capsule_getpointer_nullname_check),
	FUNC(METH_O, capsule_getdestructor_check),
	FUNC(METH_O, capsule_getcontext),
	FUNC(METH_VARARGS, capsule_setcontext),
	FUNC(METH_O, capsule_getname),
	FUNC(METH_O, capsule_valid),
	FUNC(METH_O, capsule_setdestructor),
	FUNC(METH_VARARGS, capsule_setname),
	FUNC(METH_O, capsule_setpointer),
	FUNC(METH_VARARGS, capsule_import_check),
	FUNC(METH_O, capsule_type_check),

	FUNC(METH_VARARGS, file_asfilewithmode),
	FUNC(METH_O, file_fread),
	FUNC(METH_VARARGS, file_fwrite),
	FUNC(METH_VARARGS, file_fseek),

	FUNC(METH_VARARGS, test_raw_malloc),
	FUNC(METH_VARARGS, test_raw_calloc),
	FUNC(METH_VARARGS, test_raw_realloc),
	FUNC(METH_VARARGS, test_raw_realloc_null),
	FUNC(METH_VARARGS, test_raw_malloc_zerosize),
	FUNC(METH_VARARGS, test_raw_calloc_zerosize),
	FUNC(METH_VARARGS, test_raw_realloc_zerosize),

	{ NULL }
};

/* C++ apparently doesn't allow forward declarations for data
 * http://stackoverflow.com/a/936589/99057
 */
#ifdef __cplusplus
#define FORWARD_TYPE extern
#else
#define FORWARD_TYPE static
#endif
FORWARD_TYPE PyTypeObject test_py3c_OldNumberType;
FORWARD_TYPE PyTypeObject test_py3c_NewNumberType;

typedef struct {
	PyObject_HEAD
	int number;
} test_py3c_Number;

static char value_name[] = "value";
static char value_doc[] = "the value as an int";

static PyMemberDef Number_members[] = {
	{value_name, T_INT, offsetof(test_py3c_Number, number), 0, value_doc},
	{NULL}
};

static int Number_init(test_py3c_Number *self, PyObject *args, PyObject *kwds)
{
	static char num[] = "num";
	static char *kwlist[] = {num, NULL};

	if (! PyArg_ParseTupleAndKeywords(args, kwds, "i", kwlist,
	                                  &self->number))
		return -1;

	return 0;
}

static PyObject* Number_py3c_richcmp(PyObject *obj1, PyObject *obj2, int op)
{
	test_py3c_Number *self = (test_py3c_Number *)obj1;
	if (PyObject_TypeCheck(obj2, &test_py3c_OldNumberType)) {
		test_py3c_Number *other = (test_py3c_Number *)obj2;
		return PY3C_RICHCMP(self->number, other->number, op);
	}
	if (PyInt_Check(obj2)) {
		return PY3C_RICHCMP(self->number, PyInt_AsLong(obj2), op);
	}
	Py_RETURN_NOTIMPLEMENTED;
}

static PyObject* Number_py_richcmp(PyObject *obj1, PyObject *obj2, int op)
{
	test_py3c_Number *self = (test_py3c_Number *)obj1;
	if (PyObject_TypeCheck(obj2, &test_py3c_NewNumberType)) {
		test_py3c_Number *other = (test_py3c_Number *)obj2;
		Py_RETURN_RICHCOMPARE(self->number, other->number, op);
	}
	if (PyInt_Check(obj2)) {
		Py_RETURN_RICHCOMPARE(self->number, PyInt_AsLong(obj2), op);
	}
	Py_RETURN_NOTIMPLEMENTED;
}

#ifndef __cplusplus
static
#endif
PyTypeObject test_py3c_OldNumberType = {
	PyVarObject_HEAD_INIT(NULL, 0)
	"test_py3c.OldNumber",     /*tp_name*/
	sizeof(test_py3c_Number),  /*tp_basicsize*/
	0,                         /*tp_itemsize*/
	0,                         /*tp_dealloc*/
	0,                         /*tp_print*/
	0,                         /*tp_getattr*/
	0,                         /*tp_setattr*/
	0,                         /*tp_compare*/
	0,                         /*tp_repr*/
	0,                         /*tp_as_number*/
	0,                         /*tp_as_sequence*/
	0,                         /*tp_as_mapping*/
	0,                         /*tp_hash */
	0,                         /*tp_call*/
	0,                         /*tp_str*/
	0,                         /*tp_getattro*/
	0,                         /*tp_setattro*/
	0,                         /*tp_as_buffer*/
	Py_TPFLAGS_DEFAULT,        /*tp_flags*/
	"A mutable object that stores an int. Uses PY3C_RICHCMP for comparisons.",
	0,                         /* tp_traverse */
	0,                         /* tp_clear */
	Number_py3c_richcmp,       /* tp_richcompare */
	0,                         /* tp_weaklistoffset */
	0,                         /* tp_iter */
	0,                         /* tp_iternext */
	0,                         /* tp_methods */
	Number_members,            /* tp_members */
	0,                         /* tp_getset */
	0,                         /* tp_base */
	0,                         /* tp_dict */
	0,                         /* tp_descr_get */
	0,                         /* tp_descr_set */
	0,                         /* tp_dictoffset */
	(initproc)Number_init,     /* tp_init */
	0,                         /* tp_alloc */
	PyType_GenericNew,         /* tp_new */
};

#ifndef __cplusplus
static
#endif
PyTypeObject test_py3c_NewNumberType = {
	PyVarObject_HEAD_INIT(NULL, 0)
	"test_py3c.NewNumber",     /*tp_name*/
	sizeof(test_py3c_Number),  /*tp_basicsize*/
	0,                         /*tp_itemsize*/
	0,                         /*tp_dealloc*/
	0,                         /*tp_print*/
	0,                         /*tp_getattr*/
	0,                         /*tp_setattr*/
	0,                         /*tp_compare*/
	0,                         /*tp_repr*/
	0,                         /*tp_as_number*/
	0,                         /*tp_as_sequence*/
	0,                         /*tp_as_mapping*/
	0,                         /*tp_hash */
	0,                         /*tp_call*/
	0,                         /*tp_str*/
	0,                         /*tp_getattro*/
	0,                         /*tp_setattro*/
	0,                         /*tp_as_buffer*/
	Py_TPFLAGS_DEFAULT,        /*tp_flags*/
	"A mutable object that stores an int. Uses PY_RETURN_RICHCOMPARE for comparisons.",
	0,                         /* tp_traverse */
	0,                         /* tp_clear */
	Number_py_richcmp,         /* tp_richcompare */
	0,                         /* tp_weaklistoffset */
	0,                         /* tp_iter */
	0,                         /* tp_iternext */
	0,                         /* tp_methods */
	Number_members,            /* tp_members */
	0,                         /* tp_getset */
	0,                         /* tp_base */
	0,                         /* tp_dict */
	0,                         /* tp_descr_get */
	0,                         /* tp_descr_set */
	0,                         /* tp_dictoffset */
	(initproc)Number_init,     /* tp_init */
	0,                         /* tp_alloc */
	PyType_GenericNew,         /* tp_new */
};


static struct PyModuleDef moduledef = {
	PyModuleDef_HEAD_INIT,
	"test_py3c",                               /* m_name */
	PyDoc_STR("Testing module for py3c."),     /* m_doc */
	-1,                                        /* m_size */
	methods,                                   /* m_methods */
};

#ifdef __cplusplus
extern "C" {
#endif

MODULE_INIT_FUNC(test_py3c)
{
	PyObject *capsule = NULL;
	PyObject *m = NULL;

	if (PyType_Ready(&test_py3c_OldNumberType) < 0) goto error;
	if (PyType_Ready(&test_py3c_NewNumberType) < 0) goto error;

	capsule = PyCapsule_New((void*)test_ptr2, "test_py3c.capsule", NULL);
	if (capsule == NULL) goto error;

	m = PyModule_Create(&moduledef);

	if (PyModule_AddObject(m, "str_type", (PyObject *)&PyStr_Type)) goto error;
	Py_INCREF(&PyStr_Type);

	if (PyModule_AddObject(m, "int_type", (PyObject *)&PyInt_Type)) goto error;
	Py_INCREF(&PyInt_Type);

	Py_INCREF(&test_py3c_OldNumberType);
	if (PyModule_AddObject(m, "OldNumber", (PyObject *)&test_py3c_OldNumberType)) goto error;

	Py_INCREF(&test_py3c_NewNumberType);
	if (PyModule_AddObject(m, "NewNumber", (PyObject *)&test_py3c_NewNumberType)) goto error;

	if (PyModule_AddObject(m, "capsule", capsule)) goto error;

	return m;

error:
	Py_XDECREF(capsule);
	return NULL;
}

#ifdef __cplusplus
}
#endif
