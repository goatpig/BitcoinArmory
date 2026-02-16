################################################################################
#                                                                              #
# Copyright (C) 2016-2025, goatpig                                             #
#  Distributed under the MIT license                                           #
#  See LICENSE-MIT or https://opensource.org/licenses/MIT                      #
#                                                                              #
################################################################################
import enum

from armoryengine.ArmoryUtils import LOGINFO, LOGWARN, LOGERROR
from armoryengine.PyBtcWallet import PyBtcWallet
from armoryengine.CppBridge import TheBridge

################################################################################
class WalletTypes(enum.Enum):
   Plain       = 0
   Crypt       = 1
   WatchOnly   = 2
   Offline     = 3

####
class WalletFilter(enum.Enum):
   MINE     = 0
   OFFLINE  = 1
   OTHERS   = 2
   ALL      = 3
   SINGLE   = 4

################################################################################
def determineWalletType(wlt):
   if wlt.watchingOnly:
      if wlt.getSetting('IsMine'):
         return WalletTypes.Offline
      else:
         return WalletTypes.WatchOnly
   elif wlt.useEncryption:
      return WalletTypes.Crypt
   else:
      return WalletTypes.Plain

################################################################################
## This class tracks and manages the wallets loaded by the application
class WalletMap(object):
   def __init__(self):

      #pybtcwallet objects keyed by dbId
      self._walletMap = {}

      #dbId keyed by wallet id
      self._wltIdToDbId = {}

      #index tracking
      self._dbIdList = []

   def add(self, wallet: PyBtcWallet):
      if wallet.dbId in self._walletMap:
         LOGWARN(f"trying to add a wallet we already have ({wallet.dbId})")
         for i in range(len(self._dbIdList)):
            if self._dbIdList[i]['id'] == wallet.dbId:
               del self._dbIdList[i]
               break

      self._walletMap[wallet.dbId] = wallet
      if wallet.walletId not in self._wltIdToDbId:
         self._wltIdToDbId[wallet.walletId] = {}

      if wallet.accountId in self._wltIdToDbId[wallet.walletId]:
         oldWallet = self._wltIdToDbId[wallet.walletId][wallet.accountId]
         oldDbId = oldWallet.dbId
         for i in range(len(self._dbIdList)):
            if self._dbIdList[i]['id'] == oldDbId:
               del self._dbIdList[i]
               break

      self._wltIdToDbId[wallet.walletId][wallet.accountId] = wallet
      self._dbIdList.append({'id': wallet.dbId, 'visible': True})

   def unloadWallet(self, wltId: str, accId: str=None):
      dbIds = []

      #gather all affected dbIds, cleanup the map as we go
      if wltId not in self._wltIdToDbId:
         return

      if accId:
         if not accId in self._wltIdToDbId[wltId]:
            return

         dbIds.append(self._wltIdToDbId[wltId][accId].dbId)
         del self._wltIdToDbId[wltId][accId]
         if not self._wltIdToDbId[wltId]:
            del self._wltIdToDbId[wltId]
      else:
         for accountId in self._wltIdToDbId[wltId]:
            dbIds.append(self._wltIdToDbId[wltId][accountId].dbId)
         del self._wltIdToDbId[wltId]

      if not dbIds:
         return

      #drop all affected wallets
      #NOTE: we do not need to unregister them, just ignore the dbIds
      for dbId in dbIds:
         del self._walletMap[dbId]
         for i in range(len(self._dbIdList)):
            if self._dbIdList[i]['id'] == dbId:
               del self._dbIdList[i]
               break

   def setupFromProto(self, protoPacket):
      LOGINFO('Loading wallets...')
      if not protoPacket.success:
         LOGERROR(f"failed to load wallets with error: {protoPacket.error}")
         raise Exception("failed to load wallets")

      for wltProto in protoPacket.walletManager.loadWallets:
         wltLoad = PyBtcWallet(proto=wltProto)
         self.add(wltLoad)

      LOGINFO('Number of wallets read in: %d', len(self._walletMap))
      for _, wlt in self._walletMap.items():
         dispStr  = ('   Wallet (%s):' % wlt.walletId).ljust(25)
         dispStr +=  '"'+wlt.labelName.ljust(32)+'"   '
         dispStr +=  '(Encrypted)' if wlt.useEncryption else '(No Encryption)'
         LOGINFO(dispStr)

      # Create one wallet per lockbox to make sure we can query individual
      # lockbox histories easily.
      '''
      if self._parent.usermode==USERMODE.Expert:
         LOGINFO('Loading Multisig Lockboxes')
         self.loadLockboxesFromFile(MULTISIG_FILE)
      '''

   ## getters ##
   def get(self, wId: str, accId: str=None):
      if not accId:
         #only one id was passed, use it as the dbId
         dbId = wId
      else:
         #two ids, check the wltId to dbId map
         if wId not in self._wltIdToDbId:
            raise Exception(f"no wallet for id: {wId}")

         wltMap = self._wltIdToDbId[wId]
         if accId not in wltMap:
            raise Exception(f"no account for id {accId} in wallet {wId}")
         dbId = self._wltIdToDbId[wId][accId].dbId

      if dbId not in self._walletMap:
         raise Exception(f"missing wallet for dbId {dbId}")
      return self._walletMap[dbId]

   def getByIndex(self, index):
      if index >= len(self._dbIdList):
         raise Exception(f"[getByIndex] index {index} is too large")
      dbId = self._dbIdList[index]['id']
      return self._walletMap[dbId]

   def count(self):
      return len(self._walletMap)

   def empty(self):
      return self.count() == 0

   def getWalletIdList(self, watchingOnly: bool=False):
      result = []
      for entry in self._dbIdList:
         wlt = self._walletMap[entry['id']]
         if watchingOnly and not wlt.watchingOnly:
            continue
         result.append(entry['id'])
      return result

   def getWltForScrAddr(self, scrAddr):
      for _, iterWlt in self._walletMap.items():
         if iterWlt.hasAddrHash(scrAddr):
            return iterWlt
      return None

   def hasWallet(self, wltId: str):
      return wltId in self._wltIdToDbId

   ## visibility ##
   def isVisible(self, index):
      if index >= len(self._dbIdList):
         raise Exception(f"index {index} is too large")
      return self._dbIdList[index]['visible']

   def updateVisibilityFilter(self,
      mode: WalletFilter=WalletFilter.SINGLE, index=None):
      for i in range(len(self._dbIdList)):
         dbId = self._dbIdList[i]['id']
         wlt = self._walletMap[dbId]
         wtype = determineWalletType(wlt)

         doShow = False
         if mode == WalletFilter.MINE:
            doShow = wtype in [
               WalletTypes.Offline,
               WalletTypes.Crypt,
               WalletTypes.Plain
            ]
         elif mode == WalletFilter.OFFLINE:
            doShow = wtype == WalletTypes.Offline
         elif mode == WalletFilter.OTHERS:
            doShow = wtype == WalletTypes.WatchOnly
         elif mode == WalletFilter.ALL:
            doShow = True
         elif mode == WalletFilter.SINGLE:
            doShow = i == index

         self._dbIdList[i]['visible'] = doShow
         wlt.setSetting('LedgerShow', doShow)

   def getVisibilityFilter(self):
      result = []
      for entry in self._dbIdList:
         if entry["visible"]:
            result.append(entry['id'])
      return result

   ## balances ##
   def updateBalanceAndCount(self):
      for _, wlt in self._walletMap.items():
         wlt.updateBalancesAndCount()
         wlt.getAddrDataFromDB()

   def getBalances(self):
      total = 0
      spend = 0
      unconf = 0
      for entry in self._dbIdList:
         if not entry['visible']:
            continue
         wlt = self._walletMap[entry['id']]
         if not wlt.isEnabled:
            continue

         total += wlt.getBalance('Total')
         spend += wlt.getBalance('Spendable')
         unconf += wlt.getBalance('Unconfirmed')
      return total, spend, unconf

   def detectHighestUsedIndex(self):
      for _, wlt in self._walletMap.items():
         wlt.detectHighestUsedIndex()

################################################################################
## Wallet Data Classes
class WalletImportPreview(object):
   """Preview data for legacy wallets in the wallet list before loading.

   Contains extended metadata for wallets that could be migrated and loaded
   into the WalletMap. This is not a loaded wallet object."""

   def __init__(self, previewProto):
      self.walletId = previewProto.walletId
      self.label = previewProto.label
      self.description = previewProto.description
      self.watchingOnly = previewProto.watchingOnly
      self.encrypted = previewProto.encrypted
      self.timestamp = previewProto.timestamp
      self.highestUsedIndex = previewProto.highestUsedIndex
      self.addressCount = previewProto.addressCount
      self.seedVersion = previewProto.seedVersion
      self.kdfMem = previewProto.kdfMem
      self.previewType = previewProto.which()

   def which(self):
      return self.previewType

################################################################################
class WalletListEntry(object):
   """Wallet list entry representing a wallet that could be loaded.

   Contains metadata about wallets available for loading. These are not
   loaded wallet objects; WalletMap contains the actual loaded wallets."""

   def __init__(self, entry):
      self.walletId = entry.walletId
      self.filename = entry.path
      self.staged = entry.staged or False

      self.stateName = str(entry.state).lower()
      self.isLegacy = (self.stateName == 'legacy')
      self.isEncrypted = (self.stateName == 'encrypted')
      self.isReady = (self.stateName == 'ready')

      if self.walletId:
         self.displayName = self.walletId
      else:
         self.displayName = self.filename.rsplit('.', 1)[0]
      self.watchingOnly = entry.watchingOnly

      self.importPreview = None
      if entry.which() == 'legacy':
         self.importPreview = WalletImportPreview(entry.legacy)

################################################################################
## WalletList Class - Handles all wallet data fetching and preparation
class WalletList(object):
   """
   Handles wallet data fetching and formatting for UI rendering.
   NO GUI code - pure data processing only.
   """

   def __init__(self):
      self.wallets = []

   def update(self):
      """Fetch and process wallet list, updating internal state."""
      self.wallets = []
      try:
         rawWalletData = TheBridge.wltManager.listWallets()
      except Exception as e:
         LOGERROR(f"Failed to fetch wallet list: {e}")
         raise e

      bestByWalletId = {}
      for entry in rawWalletData:
         walletEntry = WalletListEntry(entry)
         if walletEntry.isEncrypted:
            self.wallets.append(walletEntry)
            continue

         if not walletEntry.walletId:
            raise Exception(f"Wallet entry missing walletId: {walletEntry.filename}")
         if walletEntry.walletId not in bestByWalletId:
            bestByWalletId[walletEntry.walletId] = walletEntry
         else:
            existingEntry = bestByWalletId[walletEntry.walletId]
            if replaceEntry(existingEntry, walletEntry):
               bestByWalletId[walletEntry.walletId] = walletEntry

      for walletEntry in bestByWalletId.values():
         self.wallets.append(walletEntry)

   def getFilteredList(self):
      """Return the filtered wallet list for UI rendering."""
      self.update()
      return self.wallets

def replaceEntry(existingEntry, newEntry):
   """
   Check if newEntry should replace existingEntry.
   Priority: ready (full) > ready (watch-only) > legacy
   """
   if newEntry.isReady:
      if not existingEntry.isReady or not newEntry.watchingOnly:
         return True
   return False

################################################################################
## Centralized wallet listing functions to eliminate duplicate bridge calls
def loadWalletsForMainApp():
   """Load and return populated WalletMap for main application."""
   walletManager = WalletMap()
   protoPacket = TheBridge.wltManager.loadWallets()
   walletManager.setupFromProto(protoPacket)
   return walletManager
