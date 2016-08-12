#!/usr/bin/env python2

from armoryengine.VersionUtils import getVersionString
exec [x for x in file("armoryengine/ArmoryUtils.py").readlines() if x.startswith("BTCARMORY_VERSION")][0]
print(getVersionString(BTCARMORY_VERSION))
