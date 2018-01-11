#! /usr/bin/python
##############################################################################
#                                                                            #
# Copyright (C) 2011-2015, Armory Technologies, Inc.                         #
# Distributed under the GNU Affero General Public License (AGPL v3)          #
# See LICENSE or https://www.gnu.org/licenses/agpl.html                      #
#                                                                            #
# Copyright (C) 2016-2018, goatpig                                           #
# Distributed under the MIT license                                          #
# See LICENSE-MIT or https://opensource.org/licenses/MIT                     #
#                                                                            #
##############################################################################
from distutils.core import setup
import py2exe


opts = {"py2exe":{
    "dll_excludes":["MSWSOCK.dll", "IPHLPAPI.dll", "MSWSOCK.dll", "WINNSI.dll", "WTSAPI32.dll", "NSI.dll", "POWRPROF.dll", "PSAPI.dll", "CRYPT32.dll"],
    "packages":["email"]
    }}

setup( options = opts, windows = [
                        {
                            "script": "../../ArmoryQt.py",
                            "icon_resources": [(1, "../../img/armory256x256.ico"),
                                               (1, "../../img/armory64x64.ico"),
                                               (1, "../../img/armory48x48.ico")]
                        }
                ]
    )


