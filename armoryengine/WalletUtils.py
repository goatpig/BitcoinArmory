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
def determineWalletType(wlt, parent):
   if wlt.watchingOnly:
      if wlt.getSetting(parent.tr('IsMine')):
         return [WalletTypes.Offline, parent.tr('Offline')]
      else:
         return [WalletTypes.WatchOnly, parent.tr('Watching-Only')]
   elif wlt.useEncryption:
      return [WalletTypes.Crypt, parent.tr('Encrypted')]
   else:
      return [WalletTypes.Plain, parent.tr('No Encryption')]

################################################################################
## This class tracks and manages the wallets loaded by the application
class WalletMap(object):
   def __init__(self, parent):
      self._parent = parent

      #pybtcwallet objects keyed by dbId
      self._walletMap = {}

      #dbId keyed by wallet id
      self._wltIdToDbId = {}

      #index tracking
      self._dbIdList = []

   @property
   def parent(self):
      return self._parent

   def add(self, wallet: PyBtcWallet):
      if wallet.dbId in self._walletMap:
         LOGWARN(f"trying to add a wallet we already have ({wallet.dbId})")
         return

      #track by dbId
      self._walletMap[wallet.dbId] = wallet

      #check wlt/accId to dbId relationship
      if wallet.walletId not in self._wltIdToDbId:
         self._wltIdToDbId[wallet.walletId] = {}
      if wallet.accountId in self._wltIdToDbId:
         #we're replacing an existing wallet, find the old dbId
         oldWallet = self._wltIdToDbId[wallet.walletId][wallet.accountId]
         oldDbId = oldWallet.dbId

         #replace in idToId map
         self._wltIdToDbId[wallet.walletId][wallet.accountId] = wallet

         #replace in linear list
         for entry in self._dbIdList:
            if entry['id'] == oldDbId:
               entry['id'] = wallet.dbId
               break
      else:
         #new wallet, track wlt/accId relationship to dbId
         self._wltIdToDbId[wallet.walletId][wallet.accountId] = wallet

         #also append to linear dbId list
         self._dbIdList.append({
            'id' : wallet.dbId,
            'visible' : True
         })

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
         for i in range(0, len(self._dbIdList)):
            if self._dbIdList[i]['id'] == dbId:
               del self._dbIdList[i]
               break

   def setupFromProto(self, proto):
      LOGINFO('Loading wallets...')
      if not proto.success:
         LOGERROR(f"failed to load wallets wit error: {proto.error}")
         raise Exception("failed to load wallets")

      for wltProto in proto.service.loadWallets:
         wltLoad = PyBtcWallet(proto=wltProto)
         dbId = wltLoad.dbId

         wltLoaded = True
         if dbId in self._walletMap:
            LOGWARN('***WARNING: Duplicate wallet detected, %s', wallet.walletId)
            wo1 = self._walletMap[dbId].watchingOnly
            wo2 = wltLoad.watchingOnly
            fpath = wltLoad.walletPath
            if wo1 and not wo2:
               prevWltPath = self._walletMap[dbId].walletPath
               self._walletMap[dbId] = wltLoad
               LOGWARN('First wallet is more useful than the second one...')
               LOGWARN('     Wallet 1 (loaded):  %s', fpath)
               LOGWARN('     Wallet 2 (skipped): %s', prevWltPath)
            else:
               wltLoaded = False
               LOGWARN('Second wallet is more useful than the first one...')
               LOGWARN('     Wallet 1 (skipped): %s', fpath)
               LOGWARN('     Wallet 2 (loaded):  %s', self._walletMap[dbId].walletPath)
         else:
            # Update the maps/dictionaries
            self.add(wltLoad)

            # Maintain some linear lists of wallet info
            wtype = determineWalletType(wltLoad, self._parent)[0]
            notWatch = (not wtype == WalletTypes.WatchOnly)

         if wltLoaded is False:
            continue

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
         dbId = self._wltIdToDbId[wId][accId]

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

   def getWalletType(self, wlt):
      if wlt.watchingOnly:
         if wlt.getSetting('IsMine'):
            return WalletTypes.Offline
         else:
            return WalletTypes.WatchOnly
      elif wlt.useEncryption:
         return WalletTypes.Crypt
      else:
         return WalletTypes.Plain

   def getWalletIdList(self, watchingOnly: bool=False):
      result = []
      for entry in self._dbIdList:
         wlt = self._walletMap[entry['id']]
         if watchingOnly and not wlt.watchingOnly:
            continue
         result.append(entry['id'])
      return result

   def getWltForScrAddr(self, scrAddr):
      for iterID,iterWlt in self._walletMap.items():
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
      for i in range(0, len(self._dbIdList)):
         dbId = self._dbIdList[i]['id']
         wlt = self._walletMap[dbId]
         wtype = self.getWalletType(wlt)

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
      total=0
      spend=0
      unconf=0
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
