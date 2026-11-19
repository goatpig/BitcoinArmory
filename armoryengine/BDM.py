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
from armoryengine.ArmoryUtils import CLI_OPTIONS, LOGINFO, LOGERROR, \
   LOGEXCEPT, BTC_HOME_DIR, ARMORY_DB_DIR, ARMORY_HOME_DIR, ARMORYDB_IP , \
   ARMORYDB_DEFAULT_IP

BDMPhase_DBHeaders = 1
BDMPhase_OrganizingChain = 2
BDMPhase_BlockHeaders = 3
BDMPhase_BlockData = 4
BDMPhase_Rescan = 5
BDMPhase_Balance = 6
BDMPhase_SearchHashes = 7
BDMPhase_ResolveHashes = 8
BDMPhase_Completed = 9

BDM_OFFLINE = 'Offline'
BDM_UNINITIALIZED = 'Uninitialized'
BDM_BLOCKCHAIN_READY = 'BlockChainReady'
BDM_SCANNING = 'Scanning'

# Make TheBDM act like it's a singleton. Always use the global singleton TheBDM
# instance that exists in this module regardless of the instance that passed as self
def ActLikeASingletonBDM(func):
   def inner(*args, **kwargs):
      if TheBDM and len(args) > 0:
         newArgs = (TheBDM,) + args[1:]
         return func(*newArgs, **kwargs)
      else:
         return func(*args, **kwargs)
   return inner

################################################################################
class BlockDataManager(object):
   def __init__(self, isOffline=False):
      super().__init__()

      #register callbacks
      self.armoryDBDir = ""
      self.bdv_ = None

      # Flags
      self.aboutToRescan = False
      self.errorOut      = 0

      self.currentActivity = 'None'
      self.walletsToRegister = []

      if isOffline == True: self.bdmState = BDM_OFFLINE
      else: self.bdmState = BDM_UNINITIALIZED

      self.btcdir = BTC_HOME_DIR
      self.armoryDBDir = ARMORY_DB_DIR
      self.datadir = ARMORY_HOME_DIR
      self.lastPctLoad = 0

      self.topBlockHeight = 0
      self.cppNotificationListenerList = []
      self.cppPromptListeners = []

      self.progressComplete=0
      self.secondsRemaining=0
      self.progressPhase=0
      self.progressNumeric=0

      self.remoteDB = False
      if ARMORYDB_IP != ARMORYDB_DEFAULT_IP:
         self.remoteDB = True

      self.exception = ""
      self.cookie = None

      self.witness = False

   #############################################################################
   @ActLikeASingletonBDM
   def getTopBlockHeight(self):
      return self.topBlockHeight

   #############################################################################
   @ActLikeASingletonBDM
   def registerCppNotification(self, listener):
      self.cppNotificationListenerList.append(listener)

   #############################################################################
   @ActLikeASingletonBDM
   def unregisterPrompt(self, id):
      TheBridge.bridgeSocket.clearPrompt(id)

   #############################################################################
   @ActLikeASingletonBDM
   def unregisterCppNotification(self, listener):
      if listener in self.cppNotificationListenerList:
         self.cppNotificationListenerList.remove(listener)

   #############################################################################
   @ActLikeASingletonBDM
   def setSatoshiDir(self, newBtcDir):
      if not os.path.exists(newBtcDir):
         LOGERROR('setSatoshiDir: directory does not exist: %s', newBtcDir)
         return

      self.btcdir = newBtcDir

   #############################################################################
   @ActLikeASingletonBDM
   def predictLoadTime(self):
      return (self.progressPhase, self.progressComplete, \
         self.secondsRemaining, self.progressNumeric)

   #############################################################################
   @TimeThisFunction
   @ActLikeASingletonBDM
   def createAddressBook(self, cppWlt):
      return cppWlt.createAddressBook()

   #############################################################################
   @ActLikeASingletonBDM
   def setState(self, state):
      self.bdmState = state

   #############################################################################
   @ActLikeASingletonBDM
   def getState(self):
      return self.bdmState

   #############################################################################
   def pushNotification(self, notifProto):
      which = notifProto.which()
      if which == 'ready':
         LOGINFO('BDM is ready!')
         TheBDM.topBlockHeight = notifProto.ready
         TheBDM.setState(BDM_BLOCKCHAIN_READY)

      elif which == 'newBlock':
         TheBDM.topBlockHeight = notifProto.newBlock

      elif which == 'disconnected':
         TheBDM.setState(BDM_OFFLINE)

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
