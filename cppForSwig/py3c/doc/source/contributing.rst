..
    Copyright (c) 2015, Red Hat, Inc. and/or its affiliates
    Licensed under CC-BY-SA-3.0; see the license file


====================
Contributing to py3c
====================

.. highlight:: c

If you would like to contribute to py3c, be it code, documentation,
suggestions, or anything else, please file an issue or send a pull request
at the project's `Github page <http://github.com/encukou/py3c>`_.

If you are not familiar with Github, or prefer not to use it,
you can e-mail contributions to ``encukou`` at ``gmail`` dot ``com``.


Testing
=======

Automatic testing is set up at Travis CI:

    .. image:: https://travis-ci.org/encukou/py3c.svg?branch=master
       :alt: Build status
       :target: https://travis-ci.org/encukou/py3c

To test the code locally, you can run (using GNU make)::

    $ make test

This will test py3c against ``python2`` and ``python3``. To test under a
different interpreter, run for example::

    $ make test-python35


Packaging
=========

To install system-wide (for example, if you are a distro packager),
use ``make install``.
There are no configure/compile steps for this header-only library.

The install target honors GNU standard `environment variables`__ to specify
installation directories.

__ https://www.gnu.org/prep/standards/html_node/Directory-Variables.html


Building the Docs
=================

To build the docs, you need `Sphinx <http://sphinx-doc.org/>`_.
If it's not in your system's package manager, it can be installed with::

    $ pip install --user sphinx

To build the HTML documentation, do::

    $ make doc

For more docs options, run `make` in the `doc` directory.
