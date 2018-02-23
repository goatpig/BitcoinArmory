
============
py3c Changes
============


Version History
===============

v1.0 (2018-02-06)
-----------------

Additions:

* Add Py_UNREACHABLE from Python 3.7
* Add Py_RETURN_RICHCOMPARE from Python 3.7
* Add Py_UNUSED from Python 3.4

Deprecations:

* The macro PY3C_RICHCMP is deprecated in favor of Py_RETURN_RICHCOMPARE.
  There are no plans to remove the old macro from py3c.

Fixes:
* The PyStr_Concat function no longer causes GCC warnings about being unused.

Tests:
* The test suite now fails on unexpected compiler warnings.

Packaging:
* py3c is now on PyPI, thanks to @xoviat


v0.9 (2017-11-08)
-----------------

Tests:

* Adapt tests to Python 3.7 regarding const correctness (thanks to Marcel Plch)

Build:

* Fix includedir substitution on OS X (thanks to Branko Čibej)
  (This might cause make to fail if includedir, by default /usr/local/include,
  is not present; in that case it needs to be created.)


v0.8 (2017-02-01)
-----------------

Additions:

* Add backports for PyMem_Raw*

Compatibility:

* Restore compatibility with old Visual Studio versions
* Enable tests on Python 3.6


v0.7 (2016-07-05)
-----------------

Packaging:

* Fix file shim tests on big endian architectures (bug in testsuite only)


v0.6 (2016-05-19)
-----------------

Packaging:

* Fix file permissions when doing ``make instal``


v0.5 (2016-05-13)
-----------------

Packaging:

* Fix the pkgconfig file


v0.4 (2016-05-13)
-----------------

Fixes:

* Fix unterminated #if in tpflags.h (thanks to MURAOKA Yusuke)

Additions:

* Support for C++ (with initial help from MURAOKA Yusuke)
* Support PyFloat_FromString (thanks to Christoph Zwerschke)
* Test suite is much more comprehensive


v0.3 (2015-09-09)
-----------------

Breaking changes:

* Type flags moved to "tpflags.h", which is not included by default.
  See the warning in the file, or in documentation.

Other changes:

* Integrated capsulethunk.h
* Added PyFile_AsFile shim
* Discuss porting type definitions in the guide


v0.2 (2015-08-25)
-----------------

* First tagged public release


More Details
============

For all changes, see the `commit history on Github <https://github.com/encukou/py3c/commits/master>`_.
