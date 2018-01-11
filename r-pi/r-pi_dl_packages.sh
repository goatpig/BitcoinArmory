#! /bin/bash
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

packages="libqtcore4 libqt4-dev python-qt4 libconfig-file-perl libqt4-designer 
	  libqt4-scripttools libqt4-help libqt4-test libqtassistantclient4 libqtwebkit4 libqt4-declarative libqt4-script
	  libqt4-xmlpatterns libqt4-dev-bin libqt4-qt3support libqt4-sql qt4-linguist-tools
          qt4-qmake
          python-psutil python-pyasn1
	  python-pkg-resources
	  "

arch="i386"

for i
do
	arg=$i
done

if [[ "$arg" == -* ]]; then
	arch=${i:1} 
fi

if  [ ! -d "$arch" ]; then 
	mkdir $arch
fi

cd $arch

for pkg in $packages; 
do
	apt-get -o APT::Architecture=$arch download $pkg
done

cd ..

exit
