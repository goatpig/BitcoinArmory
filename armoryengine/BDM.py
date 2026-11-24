################################################################################
#                                                                              #
# Copyright (C) 2011-2015, Armory Technologies, Inc.                           #
# Distributed under the GNU Affero General Public License (AGPL v3)            #
# See LICENSE or http://www.gnu.org/licenses/agpl.html                         #
#                                                                              #
# Copyright (C) 2016-2026, goatpig                                             #
#  Distributed under the MIT license                                           #
#  See LICENSE-MIT or https://opensource.org/licenses/MIT                      #
#                                                                              #
################################################################################
import os.path

#TODO: import * is too aggressive
#from armoryengine.ArmoryUtils import *
from armoryengine.Timer import TimeThisFunction
from armoryengine.CppBridge import TheBridge
from armoryengine.ArmoryUtils import CLI_OPTIONS, LOGINFO, ARMORY_HOME_DIR

BDM_OFFLINE = 'Offline'
BDM_UNINITIALIZED = 'Uninitialized'
BDM_BLOCKCHAIN_READY = 'BlockChainReady'
BDM_SCANNING = 'Scanning'

################################################################################
class BlockDataManager(object):
   def __init__(self, isOffline=False):
      super().__init__()
      self.bdmState = BDM_OFFLINE if isOffline else BDM_UNINITIALIZED
      self.topBlockHeight = 0
      self.cppNotificationListenerList = []

   #############################################################################
   def registerCppNotification(self, listener):
      self.cppNotificationListenerList.append(listener)

   def unregisterPrompt(self, id):
      TheBridge.bridgeSocket.clearPrompt(id)

   def unregisterCppNotification(self, listener):
      if listener in self.cppNotificationListenerList:
         self.cppNotificationListenerList.remove(listener)

   #############################################################################
   def getState(self):
      return self.bdmState

   def getTopBlockHeight(self):
      return self.topBlockHeight

   def pushNotification(self, notifProto):
      which = notifProto.which()
      if which == 'ready' and self.bdmState != BDM_OFFLINE:
         LOGINFO('BDM is ready!')
         TheBDM.topBlockHeight = notifProto.ready
         self.bdmState = BDM_BLOCKCHAIN_READY

      elif which == 'scanProgress':
         if self.bdmState != BDM_BLOCKCHAIN_READY and \
            not notifProto.scanProgress.ids:
            self.bdmState = BDM_SCANNING

      elif which == 'newBlock':
         TheBDM.topBlockHeight = notifProto.newBlock

      elif which == 'disconnected':
         self.bdmState = BDM_UNINITIALIZED

      for listener in self.cppNotificationListenerList:
         listener(notifProto)

   #############################################################################
   def startBridge(self, stringArgs, readyCallback):
      TheBridge.bridgeSocket.setNotificationHandler(self.pushNotification)
      TheBridge.start(stringArgs, readyCallback)

################################################################################
# Make TheBDM reference the asyncrhonous BlockDataManager wrapper if we are
# running
TheBDM = None
if CLI_OPTIONS.offline:
   LOGINFO('Armory loaded in offline-mode.  Will not attempt to load ')
   LOGINFO('blockchain without explicit command to do so.')
   TheBDM = BlockDataManager(isOffline=True)
else:
   LOGINFO('Using the asynchronous/multi-threaded BlockDataManager.')
   LOGINFO('Blockchain operations will happen in the background.  ')
   LOGINFO('Devs: check TheBDM.getState() before asking for data.')
   LOGINFO('Registering addresses during rescans will queue them for ')
   LOGINFO('inclusion after the current scan is completed.')
   TheBDM = BlockDataManager(isOffline=False)

   cppLogFile = os.path.join(ARMORY_HOME_DIR, 'armorycpplog.txt')
   cpplf = cppLogFile
