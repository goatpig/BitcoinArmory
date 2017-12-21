#!/bin/bash
# This is the initial driver script executed by the armoryd application on OS X.
# Its role is to set up the environment before passing control to Python.
# NB: If any changes are made to this script, you'll probably need to make the
# same changes to the Armory script.

# Set environment variables so the Python executable finds its stuff.
# Note that `dirname $0` gives a relative path. We'd like the absolute path.
DIRNAME="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
ARMORYDIR="${DIRNAME}/py/usr/local/lib/armory"

# Misc. crap to keep around in case it's ever needed.
#OSXVER=`sw_vers -productVersion | awk '{ print substr( $0, 0, 4 ) }'`
#if [ $# == "0" ]; then # <-- If 0 CL args....

# Assume all args are meant for armoryd. Assuming otherwise, for shell scripts
# at least, is horribly painful.
exec "python" "${ARMORYDIR}/armoryd.py" "$@"
