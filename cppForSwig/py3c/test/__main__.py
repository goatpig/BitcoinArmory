# encoding: UTF-8

import unittest
import sys
import operator
import gc
import tempfile
import os
import shutil
import subprocess

import test_py3c

PY3 = sys.version_info >= (3, 0)
PY2 = not PY3
CAPSULE_THUNK = sys.version_info < (2, 7)

TEXT_STRING = 'test string ' + 'ẇíťħ ŮŢḞ∞ ☺'
TEST_DATA = 'here is some data'

if PY3:
    intern = sys.intern


class StringSubclass(str): pass
class IntSubclass(int): pass


class TestCase(unittest.TestCase):
    if sys.version_info < (2, 7):
        def assertIs(self, left, right):
            self.assertTrue(left is right)


class StringChecks(TestCase):
    def test_type(self):
        self.assertIs(test_py3c.str_type, str)

    def test_check(self):
        self.assertTrue(test_py3c.str_check("abc"))
        self.assertFalse(test_py3c.str_check(8))
        self.assertTrue(test_py3c.str_check(StringSubclass()))
        self.assertEqual(test_py3c.str_check(b"abc"), PY2)
        self.assertEqual(test_py3c.str_check(u"abc"), PY3)

    def test_check_exact(self):
        self.assertTrue(test_py3c.str_checkexact("abc"))
        self.assertFalse(test_py3c.str_checkexact(8))
        self.assertFalse(test_py3c.str_checkexact(StringSubclass()))
        self.assertEqual(test_py3c.str_check(b"abc"), PY2)
        self.assertEqual(test_py3c.str_check(u"abc"), PY3)

    def test_fromstring(self):
        self.assertEqual(test_py3c.str_fromstring(), TEXT_STRING)

    def test_fromstringandsize(self):
        self.assertEqual(test_py3c.str_fromstringandsize(11), "test string")
        self.assertEqual(test_py3c.str_fromstringandsize(36), TEXT_STRING)
        if PY2:
            self.assertEqual(test_py3c.str_fromstringandsize(13), TEXT_STRING[:13])
        else:
            self.assertRaises(UnicodeDecodeError, test_py3c.str_fromstringandsize, 13)

    def test_fromformat(self):
        self.assertEqual(test_py3c.str_fromformat(), "<abc:3>")
        self.assertEqual(test_py3c.str_fromformatv(), "<abc:3>")

    def test_asstring(self):
        self.assertTrue(test_py3c.str_asstring_check(TEXT_STRING))
        self.assertFalse(test_py3c.str_asstring_check("---"))
        self.assertRaises(TypeError, test_py3c.str_asstring_check, 8)

    def test_concat(self):
        self.assertEqual(test_py3c.str_concat(TEXT_STRING, "\0abc"), TEXT_STRING + "\0abc")
        self.assertRaises((TypeError, SystemError), test_py3c.str_concat, 8, "\0abc")

    def test_format(self):
        self.assertEqual(test_py3c.str_format("%s %d %.2f", ("a", 1, 3.5)), "a 1 3.50")
        self.assertRaises(TypeError, test_py3c.str_format, 8)
        self.assertRaises(TypeError, test_py3c.str_format, "%d", "-")

    def test_interninplace(self):
        self.assertEqual(test_py3c.str_interninplace("ab" + "cd"), intern("abcd"))

    def test_internfromstring(self):
        self.assertEqual(test_py3c.str_internfromstring(TEXT_STRING), intern(TEXT_STRING))

    def test_decode(self):
        self.assertEqual(test_py3c.str_decode(11, "strict"), "test string")
        # XXX
        #self.assertEqual(test_py3c.str_decode(36, "strict"), UTF8_STRING)
        #self.assertEqual(test_py3c.str_decode(13, "replace"), "test string \N{REPLACEMENT CHARACTER}")

    def test_asutf8string(self):
        if PY2:
            self.assertEqual(test_py3c.str_asutf8string(TEXT_STRING), TEXT_STRING)
        else:
            self.assertEqual(test_py3c.str_asutf8string(TEXT_STRING), TEXT_STRING.encode('utf-8'))

    def test_asutf8(self):
        self.assertTrue(test_py3c.str_asutf8_check(TEXT_STRING))
        self.assertFalse(test_py3c.str_asutf8_check("---"))
        self.assertRaises(TypeError, test_py3c.str_asutf8_check, 8)

    def test_asutf8andsize(self):
        self.assertTrue(test_py3c.str_asutf8andsize_check(TEXT_STRING))
        self.assertFalse(test_py3c.str_asutf8andsize_check("---"))
        self.assertRaises(TypeError, test_py3c.str_asutf8andsize_check, 8)


class IntChecks(TestCase):
    def test_type(self):
        self.assertIs(test_py3c.int_type, int)

    def test_check(self):
        self.assertTrue(test_py3c.int_check(8))
        self.assertFalse(test_py3c.int_check("abc"))
        self.assertTrue(test_py3c.int_check(IntSubclass()))
        self.assertEqual(test_py3c.int_check(2**1000), PY3)

    def test_check_exact(self):
        self.assertTrue(test_py3c.int_checkexact(8))
        self.assertFalse(test_py3c.int_checkexact("abc"))
        self.assertFalse(test_py3c.int_checkexact(IntSubclass()))
        self.assertEqual(test_py3c.int_checkexact(2**1000), PY3)

    def test_from(self):
        self.assertEqual(test_py3c.int_fromstring(), 8)
        self.assertEqual(test_py3c.int_fromlong(), 8)
        self.assertEqual(test_py3c.int_fromssize_t(), 8)
        self.assertEqual(test_py3c.int_fromsize_t(), 8)

    def test_as(self):
        self.assertTrue(test_py3c.int_aslong_check(8))
        self.assertTrue(test_py3c.int_aslong_macro_check(8))
        self.assertTrue(test_py3c.int_asunsignedlonglongmask_check(8))
        self.assertTrue(test_py3c.int_asssize_t_check(8))


class FloatChecks(TestCase):
    def test_fromstring(self):
        self.assertEqual(test_py3c.float_fromstring('42'), 42.0)
        self.assertEqual(test_py3c.float_fromstring('-10.5'), -10.5)


class ComparisonHelperChecks(TestCase):
    def test_return_notimplemented(self):
        self.assertIs(test_py3c.return_notimplemented(), NotImplemented)

    def test_unreachable(self):
        proc = subprocess.Popen(
            [sys.executable, '-c',
             'import test_py3c; test_py3c.test_unreachable()'],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        stdout, stderr = proc.communicate()
        self.assertEqual(stdout, b'')
        self.assertEqual(stderr, b'')
        self.assertNotEqual(proc.returncode, 0)


class ComparisonChecksBase(object):
    def test_number(self):
        num = self.cls(3)
        self.assertEqual(num.value, 3)
        num.value = 5
        self.assertEqual(num.value, 5)

    def test_number_number_equality(self):
        three = self.cls(3)
        three2 = self.cls(3)
        five = self.cls(5)
        self.assertEqual(three, three)
        self.assertEqual(three, three2)
        self.assertNotEqual(three, five)

    def test_number_int_equality(self):
        three = self.cls(3)
        five = self.cls(5)
        self.assertEqual(three, 3)
        self.assertNotEqual(three, 5)

    def test_all_comparisons(self):
        three = self.cls(3)
        three2 = self.cls(3)
        five = self.cls(5)
        for op in (operator.eq, operator.ne, operator.gt, operator.lt,
                   operator.ge, operator.le):
            self.assertEqual(op(three, three), op(3, 3))
            self.assertEqual(op(three, three2), op(3, 3))
            self.assertEqual(op(three, five), op(3, 5))
            self.assertEqual(op(five, three), op(5, 3))

            self.assertEqual(op(three, 3), op(3, 3))
            self.assertEqual(op(3, three), op(3, 3))
            self.assertEqual(op(three, 5), op(3, 5))
            self.assertEqual(op(5, three), op(5, 3))



class OldComparisonChecks(TestCase, ComparisonChecksBase):
    cls = test_py3c.OldNumber


class NewComparisonChecks(TestCase, ComparisonChecksBase):
    cls = test_py3c.NewNumber


class CapsuleChecks(TestCase):
    def tearDown(self):
        # Ensure that the destructor was called for each capsule
        gc.collect()
        self.assertEqual(test_py3c.capsule_get_count(), 0)

    def test_get_pointer(self):
        capsule = test_py3c.capsule_new()
        self.assertTrue(test_py3c.capsule_getpointer_check(capsule))
        if not CAPSULE_THUNK:
            self.assertRaises(ValueError, test_py3c.capsule_getpointer_nullname_check, capsule)

    def test_get_pointer_nullname(self):
        capsule = test_py3c.capsule_new_nullname()
        self.assertTrue(test_py3c.capsule_getpointer_nullname_check(capsule))
        if not CAPSULE_THUNK:
            self.assertRaises(ValueError, test_py3c.capsule_getpointer_check, capsule)

    def test_get_destructor(self):
        capsule = test_py3c.capsule_new()
        self.assertTrue(test_py3c.capsule_getdestructor_check(capsule))

    def test_get_context(self):
        capsule = test_py3c.capsule_new()
        self.assertEqual(test_py3c.capsule_getcontext(capsule), None)

    def test_set_context(self):
        capsule = test_py3c.capsule_new()
        test_py3c.capsule_setcontext(capsule, TEST_DATA)
        self.assertEqual(test_py3c.capsule_getcontext(capsule), TEST_DATA)

    def test_get_name(self):
        capsule = test_py3c.capsule_new()
        if CAPSULE_THUNK:
            self.assertEqual(test_py3c.capsule_getname(capsule), None)
        else:
            self.assertEqual(test_py3c.capsule_getname(capsule), "test_capsule")

    def test_setname(self):
        capsule = test_py3c.capsule_new()
        if CAPSULE_THUNK:
            self.assertRaises(NotImplementedError, test_py3c.capsule_setname, capsule, "other name")
        else:
            test_py3c.capsule_setname(capsule, "other name")
            self.assertEqual(test_py3c.capsule_getname(capsule), "other name")

    def test_import(self):
        self.assertTrue(test_py3c.capsule_import_check('test_py3c.capsule'))

    def test_valid(self):
        capsule = test_py3c.capsule_new()
        self.assertTrue(test_py3c.capsule_valid(capsule))

    def test_invalid(self):
        capsule = test_py3c.capsule_new_nullname()
        self.assertEqual(test_py3c.capsule_valid(capsule), CAPSULE_THUNK)

    def test_setdestructor(self):
        capsule = test_py3c.capsule_new()
        test_py3c.capsule_setdestructor(capsule)
        del capsule
        gc.collect()
        self.assertEqual(test_py3c.capsule_get_count(), 0)

    def test_setpointer(self):
        capsule = test_py3c.capsule_new()
        self.assertTrue(test_py3c.capsule_getpointer_check(capsule))
        test_py3c.capsule_setpointer(capsule)
        self.assertTrue(test_py3c.capsule_getpointer_check_other(capsule))

    def test_type(self):
        capsule = test_py3c.capsule_new()
        self.assertTrue(test_py3c.capsule_type_check(capsule))

    def test_invalid_operations(self):
        capsule = "not a capsule"
        if CAPSULE_THUNK:
            error = TypeError
        else:
            error = ValueError
        self.assertRaises(error, test_py3c.capsule_getpointer_check, capsule)
        self.assertRaises(error, test_py3c.capsule_getdestructor_check, capsule)
        self.assertRaises(error, test_py3c.capsule_getcontext, capsule)
        self.assertFalse(test_py3c.capsule_valid(capsule))

        if CAPSULE_THUNK:
            self.assertEqual(test_py3c.capsule_getname(capsule), None)
        else:
            self.assertRaises(error, test_py3c.capsule_getname, capsule)

        self.assertRaises(error, test_py3c.capsule_setpointer, capsule)
        self.assertRaises(error, test_py3c.capsule_setdestructor, capsule)
        self.assertRaises(error, test_py3c.capsule_setcontext, capsule, "")

        if CAPSULE_THUNK:
            self.assertRaises(NotImplementedError, test_py3c.capsule_setname, capsule, "")
        else:
            self.assertRaises(error, test_py3c.capsule_setname, capsule, "")


class FileShimChecks(TestCase):
    def setUp(self):
        self.dirname = tempfile.mkdtemp()
        self.filename = os.path.join(self.dirname, 'f')

    def tearDown(self):
        shutil.rmtree(self.dirname)

    def test_bad_type(self):
        err = AttributeError, ValueError, SystemError
        self.assertRaises(err, test_py3c.file_asfilewithmode, 3, "hello")

    def test_read_write(self):
        with open(self.filename, 'w') as pyfile:
            pass
        with open(self.filename, 'w+') as pyfile:
            f = test_py3c.file_asfilewithmode(pyfile, 'w+')
            test_py3c.file_fwrite(f, "hello")
            test_py3c.file_fseek(f, 0)
            val = test_py3c.file_fread(f)
            self.assertEqual(val, "hello")

    def test_read(self):
        with open(self.filename, 'w') as pyfile:
            pyfile.write('hello')
        with open(self.filename, 'r') as pyfile:
            f = test_py3c.file_asfilewithmode(pyfile, "r")
            val = test_py3c.file_fread(f)
            self.assertEqual(val, "hello")

    def test_write(self):
        with open(self.filename, 'w') as pyfile:
            f = test_py3c.file_asfilewithmode(pyfile, "w")
            test_py3c.file_fwrite(f, "hello")
        with open(self.filename, 'r') as pyfile:
            self.assertEqual(pyfile.read(), 'hello')


class RawMallocChecks(TestCase):
    def test_raw_malloc(self):
        test_py3c.test_raw_malloc()

    def test_raw_malloc(self):
        test_py3c.test_raw_calloc()

    def test_raw_realloc(self):
        test_py3c.test_raw_realloc()

    def test_raw_realloc_null(self):
        test_py3c.test_raw_realloc_null()

    def test_raw_malloc_zerosize(self):
        test_py3c.test_raw_malloc_zerosize()

    def test_raw_calloc_zero(self):
        test_py3c.test_raw_malloc_zerosize()

    def test_raw_realloc_zero(self):
        test_py3c.test_raw_malloc_zerosize()


def main():
    try:
        unittest.main()
    except SystemExit as e:
        if e.code != 0:
            raise


if __name__ == '__main__':
    main()

    if hasattr(sys, 'gettotalrefcount'):
        # For debug builds of Python, verify we don't leak references
        RUNS = 10

        baseline = None
        gc.collect()
        refcounts = [None] * RUNS

        for i in range(RUNS):
            main()
            gc.collect()
            refcounts[i] = sys.gettotalrefcount()

        # refleak == total refcounts change with every test run
        if len(set(refcounts)) <= len(refcounts):
            print('Reference counts are stable')
        else:
            print(refcounts)
            print('Reference counts are not stable')
            exit('FAIL')
