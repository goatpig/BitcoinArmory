#!/usr/bin/env python3
"""Build Armory as a Mac OS X Application."""
import os
from os import path
import sys
import hashlib
import shutil
import glob
import time
import datetime
import optparse
import tarfile
import subprocess
import zipfile

from subprocess import Popen, PIPE
from tempfile import mkstemp

# Set some constants up front
minOSXVer     = '10.8'

LOGFILE       = 'build-app.log.txt'
LOGPATH       = path.abspath( path.join(os.getcwd(), LOGFILE))
ARMORYDIR     = '..'
OBJCDIR       = path.join(os.getcwd(), 'objc_armory')
WORKDIR       = path.join(os.getcwd(), 'workspace')
APPDIR        = path.join(WORKDIR, 'Armory.app') # actually make it local
PREFIXBASEDIR = path.join(APPDIR, 'Contents/MacOS/py')
MAKEFLAGS     = '-j4'
CONFIGFLAGS   = 'PYTHON_VERSION=3 --with-macosx-version-min=%s' % minOSXVer

# If no arguments specified, then do the minimal amount of work necessary
# Assume that only one flag is specified.  These should be
parser = optparse.OptionParser(usage="%prog [options]\n")
parser.add_option('--cleanupapp',   dest='cleanupapp',  default=False, action='store_true', help='Delete Python files in the compiled application')
(CLIOPTS, CLIARGS) = parser.parse_args()

########################################################
# Write the string to both console and log file
def logprint(s):
   print(s)
   with open(LOGFILE,'a') as f:
      f.write(s if s.endswith('\n') else s+'\n')

########################################################
# Now actually start the download&build process

# Make sure all the dirs exist
def main():

   if path.exists(LOGFILE):
      os.remove(LOGFILE)

   delete_prev_data(CLIOPTS)

   makedir(WORKDIR)

   if not os.path.isdir(APPDIR):
      make_empty_app()
      make_resources()

   compile_armory()
# TODO: Re-enable Objective-C compilation.
#   compile_objc_library()
   cleanup_app()
   # Force Finder to update the Icon
   execAndWait("touch " + APPDIR)
   make_targz()

################################################################################
def getRightNowStr():
   dateFmt = '%Y-%b-%d %I:%M%p'
   dtobj = datetime.datetime.fromtimestamp(time.time())
   dtstr = '' + dtobj.strftime(dateFmt)
   return dtstr[:-2] + dtstr[-2:].lower()

################################################################################
def execAndWait(syscmd, cwd=None):
   try:
      syscmd += ' 2>&1 | tee -a %s' % LOGPATH
      logprint('*'*80)
      logprint(getRightNowStr())
      logprint('Executing: "%s"' % syscmd)
      logprint('Executing from: "%s"' % (os.getcwd() if cwd is None else cwd))
      proc = Popen(syscmd, shell=True, cwd=cwd)
      while proc.poll() == None:
         time.sleep(0.25)
      logprint('Finished executing: "%s"' % syscmd)
      logprint('Finished executing from: "%s"' % (os.getcwd() if cwd is None else cwd))
      logprint('*'*80)
   except Exception as e:
      logprint('\n' + '-'*80)
      logprint('ERROR: %s' % str(e))
      logprint('-'*80 + '\n')

################################################################################
def makedir(dirname):
   if not path.isdir(dirname):
      logprint( "Creating directory: %s" % dirname)
      os.mkdir(dirname)

################################################################################
def removetree(pathname):
   if path.exists(pathname):
      logprint('Removing directory tree: %s' % pathname)
      shutil.rmtree(pathname)

################################################################################
def copyfile(src, dst):
   logprint('Copying file:  "%s" --> "%s"' % (src,dst))
   shutil.copy(src,dst)

########################################################
def make_empty_app():
   'Make the empty .app bundle structure'
   makedir(APPDIR)
   makedir(path.join(APPDIR,'Contents'))
   makedir(path.join(APPDIR,'Contents/MacOS'))
   makedir(path.join(APPDIR,'Contents/MacOS/py'))
   makedir(path.join(APPDIR,'Contents/Frameworks'))
   makedir(path.join(APPDIR,'Contents/Resources'))
   makedir(path.join(APPDIR,'Contents/Dependencies'))

########################################################
def compile_armory():
   logprint('Compiling and installing Armory')

   armoryAppScript = path.join(APPDIR, 'Contents/MacOS/Armory')
   armorydAppScript = path.join(APPDIR, 'Contents/MacOS/armoryd')
   armoryDB = path.join(APPDIR, 'Contents/MacOS/ArmoryDB')
   currentDir = os.getcwd()
   os.chdir("..")
   execAndWait('python3 update_version.py')
   os.chdir(currentDir)
   execAndWait('./autogen.sh', cwd='..')
   execAndWait('./configure %s' % CONFIGFLAGS, cwd='..')
   execAndWait('make clean', cwd='..')
   execAndWait('make DESTDIR="%s" install %s' % (PREFIXBASEDIR, MAKEFLAGS), cwd='..')
   copyfile('Armory-script.sh', armoryAppScript)
   copyfile('armoryd-script.sh', armorydAppScript)
   execAndWait('chmod +x "%s"' % armoryAppScript)
   execAndWait('chmod +x "%s"' % armorydAppScript)
   execAndWait('chmod +x "%s"' % armoryDB) # ArmoryDB copied over by Makefile

########################################################
def compile_objc_library():
   logprint('Compiling and installing the Armory Objective-C shared library')

   # Execute SIP to create the Python/Obj-C++ glue code, use qmake to create the
   # Makefile, and make the shared library. Be sure to keep the SIP flags in
   # sync with generate_sip_module_code() from PyQt's configure-ng.py.
# TODO: Find what can replace the PyQt files, and find sip and qmake.
#   sipFlags = '-w -x VendorID -t WS_MACX -t Qt_4_8_7 -x Py_v3 -B Qt_5_0_0 -o ' \
#              '-P -g -c . -I ../workspace/unpackandbuild/PyQt4_gpl_mac-%s/sip' % pyQtVer
#   execAndWait('sip %s ./ArmoryMac.sip' % (sipVer, sipFlags), cwd=OBJCDIR)
#   execAndWait('qmake ArmoryMac.pro' % qtVer, cwd=OBJCDIR)

   # For some reason, qmake mangles LFLAGS when LFLAGS is built. The exact cause
   # is unknown but probably has to do with a conf file included in
   # mkspecs/unsupported/macx-clang-libc++/qmake.conf. Patch the output for now.
#   execAndWait('patch -p0 < %s' % path.join(os.getcwd(), 'qmake_LFLAGS.patch'), cwd=OBJCDIR)
   execAndWait('make %s' % MAKEFLAGS, cwd=OBJCDIR)

########################################################
def make_resources():
   "Populate the Resources folder."
   cont = path.join(APPDIR, 'Contents')
   copyfile('Info.plist', cont)

   icnsArm = '../img/armory_icon_fullres.icns'
   icnsRes  = path.join(cont,  'Resources/Icon.icns')
   copyfile(icnsArm, icnsRes)

########################################################
def cleanup_app():
   "Try to remove as much unnecessary junk as possible."
   print "Removing .pyo and .pyc files."
   remove_python_files(PREFIXBASEDIR)

########################################################
def make_targz():
   ver = getVersionStr()
   execAndWait('tar -zcf ../armory_%s_osx.tar.gz Armory.app' % ver, cwd=WORKDIR)

########################################################
def getVersionStr():
   with open('../armoryengine/ArmoryUtils.py') as f:
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
            return vstr

########################################################
def show_app_size():
   # Macs are very peculiar about getting the sizes of things from the terminal.
   # Some commands (e.g., "find") are also really slow when piped around. The
   # following command is fast *and* accurate. Don't touch without good reasons!
   "Show the size of the app."
   execAndWait('ls -lR %s | grep -v \'^d\' | awk \'{total += $5} END ' \
               '{print \"Total size of Armory:\", total, \"bytes\"}\'' % APPBASE)
   sys.stdout.flush()

########################################################
def remove_python_files(top, removePy=True):
   """Remove .pyo and .pyc files."""
   n_pyo = 0
   n_pyc = 0

   for (dirname, dirs, files) in os.walk(top):
      for f in files:
         prename, ext = path.splitext(f)
         if ext == '.pyo':
            removefile(path.join(dirname, f))
            n_pyo += 1
         elif ext == '.pyc':
            removefile(path.join(dirname, f))
            n_pyc += 1

   logprint("Removed %i .pyo and %i .pyc files." % (n_pyo, n_pyc))

########################################################
def delete_prev_data(opts):
   # Always remove previously-built application files
   removetree(APPDIR)

########################################################
if __name__ == "__main__":
   main()
