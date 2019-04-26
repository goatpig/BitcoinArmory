#! /usr/bin/python

import os
import shutil
import platform
import time
import argparse
from subprocess import Popen, PIPE

def execAndWait(cli_str):
   print '*** Executing:', cli_str[:60], '...'
   process = Popen(cli_str, shell=True)
   while process.poll() == None:
      time.sleep(0.5)
   print '*** Finished executing'
   

def dir(path='.'):
   allpaths = os.listdir(path)
   fileList = filter(lambda a: os.path.isfile(a), allpaths)
   dirList  = filter(lambda a: os.path.isdir(a), allpaths)
   return [fileList, dirList]

def cd(path):
   os.chdir(path)

def pwd():
   return os.getcwd()

# Set up parsing of input flags
parser = argparse.ArgumentParser(description='Build debian packages for Armory.')
parser.add_argument('-c', '--cross', metavar='arch', choices=['armhf'],
                    help='Specify architecture to cross compile for. Use "armhf" for RPi.')
parser.add_argument('-t', '--toolchain-path', dest='toolchain', 
                    help='Cross compiler directory, use in combination with --cross.')
parser.add_argument('-p', '--extra-python-includes', dest='extrapython',
                    help='Path to python include directory containing armhf version of pyconfig.h. Do not add "-I" prefix to path. Use in combination with --cross.')
parser.add_argument('-j', '--jobs', type=int, default=8,
                   help='Number of processing jobs to pass into dpkg & make')
args = parser.parse_args()

if pwd().split('/')[-1]=='dpkgfiles':
   cd('..')

if not os.path.exists('./armoryengine/ArmoryUtils.py') or \
   not os.path.exists('./ArmoryQt.py'):
   print '***ERROR: Must run this script from the root Armory directory!'
   exit(1)

jobParam = ''
if args.jobs >= 1:
   jobParam = '-j' + str(args.jobs)

if args.cross:
   if args.toolchain and args.extrapython and os.path.exists(args.toolchain) and os.path.exists(args.extrapython):
      args.toolchain = os.path.abspath(args.toolchain)
      args.extrapython = os.path.abspath(args.extrapython)
   else:
      exit('Cross compiler toolchain location and/or python include location are not valid directories. These are required for cross compiling.')

# Must get current Armory version from armoryengine.py
# I desperately need a better way to store/read/increment version numbers
vstr = ''
with open('armoryengine/ArmoryUtils.py') as f:
   for line in f.readlines():
      if line.startswith('BTCARMORY_VERSION'):
         vstr = line[line.index('(')+1:line.index(')')]
         vquad = tuple([int(v) for v in vstr.replace(' ','').split(',')])
         print vquad, len(vquad)
         vstr = '%d.%02d' % vquad[:2]
         if (vquad[2] > 0 or vquad[3] > 0):
            vstr += '.%d' % vquad[2]
         if vquad[3] > 0:
            vstr += '.%d' % vquad[3]
         break


pkgdir = 'armory-%s' % (vstr,)
pkgdir_ = 'armory_%s' % (vstr,)

if not vstr:
   print '***ERROR: Could not deduce version from ArmoryUtils.py. '
   print '          There is no good reason for this to happen.  Ever! :('
   exit(1)

# Copy the correct control file (for 32/64-bit OS or armhf crosscompile)
if args.cross == 'armhf':
    shutil.copy('dpkgfiles/controlarmhf', 'dpkgfiles/control')
else:    
    osBits = platform.architecture()[0][:2]
    shutil.copy('dpkgfiles/control%s' % (osBits), 'dpkgfiles/control')

dpkgfiles = ['control', 'copyright', 'postinst', 'postrm', 'rules']


# Start pseudo-bash-script
origDir = pwd().split('/')[-1]
execAndWait('python update_version.py')
execAndWait('make clean')
cd('..')
execAndWait('rm -rf %s' % pkgdir)
execAndWait('rm -f %s*' % pkgdir)
execAndWait('rm -f %s*' % pkgdir_)
shutil.copytree(origDir, pkgdir)
execAndWait('tar -zcf %s.tar.gz %s' % (pkgdir, pkgdir))
cd(pkgdir)
execAndWait('export DEBFULLNAME="goatpig"; dh_make -s -e moothecowlord@gmail.com -f ../%s.tar.gz' % pkgdir)
for f in dpkgfiles:
   shutil.copy('dpkgfiles/%s' % f, 'debian/%s' % f)

# Finally, all the magic happens here
if args.cross:
   print 'Attempting armhf cross compile for Raspberry Pi'
   # We need these build options to stop dpkg/configure testing for options that gcc <4.9 (e.g. raspbian toolchain) doesn't support, and then failing
   # Requires hardening-wrapper package to work too.
   deb_build_options = 'hardening=-stackprotectorstrong reproducible=-timeless'
   execAndWait('export PATH="$PATH:%s" DEB_BUILD_OPTIONS="%s" CROSS_COMPILING="armhf" EXTRA_PYTHON="%s"; dpkg-buildpackage -t arm-linux-gnueabihf -d -rfakeroot -uc -us %s' % (args.toolchain, deb_build_options, args.extrapython, jobParam))
else:
   print 'Attempting normal build for debian'
   execAndWait('dpkg-buildpackage -rfakeroot -uc -us %s' % jobParam)
