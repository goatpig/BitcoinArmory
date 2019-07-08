################################################################################
#                                                                              #
# Copyright (C) 2019, goatpig                                                  #
# Distributed under the GNU Affero General Public License (AGPL v3)            #
# See LICENSE or http://www.gnu.org/licenses/agpl.html                         #
#                                                                              #
################################################################################

#setup cppyy
import cppyy
cppyy.add_include_path('cppForSwig/libbtc/include')
cppyy.add_include_path('cppForSwig/libbtc/src/secp256k1/include')
cppyy.add_include_path('cppForSwig/chacha20poly1305')
cppyy.add_include_path('cppForSwig/lmdb/libraries/liblmdb')
cppyy.include('cppForSwig/SwigClient.h')
cppyy.include('cppForSwig/BlockDataManagerConfig.h')
cppyy.include('cppForSwig/WalletManager.h')
cppyy.include('cppForSwig/nodeRPC.h')
cppyy.include('cppForSwig/EncryptionUtils.h')
cppyy.include('cppForSwig/BDM_mainthread.h')
cppyy.include('cppForSwig/NetworkConfig.h')
cppyy.include('cppForSwig/libbtc/include/btc/ecc.h')

#have to add lib path for cppyy to find libprotobuf.so
cppyy.gbl.gSystem.AddDynamicPath('/usr/lib/x86_64-linux-gnu/')

#libArmoryClient.so dynamically links to protobuf, have to preload it
cppyy.load_library('libprotobuf.so')
cppyy.load_library('libArmoryClient.so')
from cppyy.gbl import AsyncClient, std
import cppyy.gbl as ArmoryCpp
