################################################################################
##                                                                            ##
##  Copyright (C) 2011-2015, Armory Technologies, Inc.                        ##
##  Distributed under the GNU Affero General Public License (AGPL v3)         ##
##  See LICENSE-ATI or https://www.gnu.org/licenses/agpl.html                 ##
##                                                                            ##
##  Copyright (C) 2016-2018, goatpig                                          ##
##  Distributed under the MIT license                                         ##
##  See LICENSE-MIT or https://opensource.org/licenses/MIT                    ##
##                                                                            ##
################################################################################
#! /bin/bash

distro=`uname -v | grep -oi 'debian'`

if [ ${distro,,} = "debian" ]; then
echo ${distro,,}
exit 1
else
distro=`uname -v | grep -oi 'ubuntu'`
if [ ${distro,,} = "debian" ]; then
echo "debian"
exit 1
fi 
fi

echo "other"
exit 0
