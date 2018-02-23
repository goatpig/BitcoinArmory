import os
import glob

from setuptools import setup


def find_headers():
    path = os.path.join(os.path.dirname(__file__), 'include')
    for root, dirs, files in os.walk(path):
        for filename in files:
            if filename.endswith('.h'):
                yield os.path.join(root, filename)

with open('README.rst') as f:
    long_description = f.read()

setup(
    name='py3c',
    version='1.0',
    description='Python compatibility headers',
    long_description=long_description,
    author='Petr Viktorin',
    author_email='encukou@gmail.com',
    url='http://py3c.readthedocs.io/',
    headers=list(find_headers()),
    platforms='any')
