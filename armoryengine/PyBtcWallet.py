################################################################################
#                                                                              #
# Copyright (C) 2011-2015, Armory Technologies, Inc.                           #
# Distributed under the GNU Affero General Public License (AGPL v3)            #
# See LICENSE or http://www.gnu.org/licenses/agpl.html                         #
#                                                                              #
# Copyright (C) 2016-2025, goatpig                                             #
#  Distributed under the MIT license                                           #
#  See LICENSE-MIT or https://opensource.org/licenses/MIT                      #
#                                                                              #
################################################################################
from __future__ import (absolute_import, division,
   print_function, unicode_literals)
import os.path
import shutil

from armoryengine.ArmoryUtils import UINT32_MAX, emptyFunc, \
   PYBTCWALLET_VERSION, USE_TESTNET, USE_REGTEST, CLI_OPTIONS, \
   LOGINFO, LOGEXCEPT, LOGWARN, LOGERROR, HMAC256
from armoryengine.BinaryPacker import *
from armoryengine.BinaryUnpacker import *
from armoryengine.Timer import Timer, TimeThisFunction
from armoryengine.Decorators import singleEntrantMethod
from armoryengine.CppBridge import TheBridge, BridgeWalletWrapper
from armoryengine.PyBtcAddress import PyBtcAddress
from armoryengine.AddressUtils import Balances, addrStr_to_hash160, \
   scrAddr_to_addrStr, AddressEntryType_Default, binary_to_base58
from armoryengine.Settings import TheSettings

BLOCKCHAIN_READONLY   = 0
BLOCKCHAIN_READWRITE  = 1
BLOCKCHAIN_DONOTUSE   = 2

WLT_UPDATE_ADD = 0
WLT_UPDATE_MODIFY = 1

WLT_DATATYPE_KEYDATA     = 0
WLT_DATATYPE_ADDRCOMMENT = 1
WLT_DATATYPE_TXCOMMENT   = 2
WLT_DATATYPE_OPEVAL      = 3
WLT_DATATYPE_DELETED     = 4

DEFAULT_COMPUTE_TIME_TARGET = 2
DEFAULT_MAXMEM_LIMIT        = 128*1024*1024

PYROOTPKCCVER = 1 # Current version of root pub key/chain code backup format
PYROOTPKCCVERMASK = 0x7F
PYROOTPKCCSIGNMASK = 0x80

def computeSettingsId(wltId, accId):
   #armory's py implementation of hmac mangles the 2nd half but
   #we don't care for the purpose of concatenating ids
   hmacStr = HMAC256(wltId.encode('ascii'), accId.encode('ascii'))
   b58Str = binary_to_base58(hmacStr)
   return b58Str[0:7]

################################################################################
class PyBtcWallet(object):
   """
   ***NOTE: This an abridged version of the initial description. Not much of
      what's mentionned here has any relevance to the wallet structure anymore,
      it is preserved for historical purposes only.

   I have ONLY implemented deterministic wallets, using ECDSA
   Diffie-Hellman shared-secret crypto operations.  This allows
   one to actually determine the next PUBLIC KEY in the address
   chain without actually having access to the private keys.
   This makes it possible to synchronize online-offline computers
   once and never again.

   You can import random keys into your wallet, but if it is
   encrypted, you will have to supply a passphrase to make sure
   it can be encrypted as well.

   Presumably, wallets will be used for one of three purposes:

   (1) Spend money and receive payments
   (2) Watching-only wallets - have the private keys, just not on this computer
   (3) May be watching *other* people's addrs.  There's a variety of reasons
       we might want to watch other peoples' addresses, but most them are not
       relevant to a "basic" BTC user.  Nonetheless it should be supported to
       watch money without considering it part of our own assets
   """

   #############################################################################
   def __init__(self, *, wltId=None, accId=None, proto=None):
      # (Major, Minor, Minor++, even-more-minor)
      self.version        = PYBTCWALLET_VERSION
      self.watchingOnly   = True
      self.wltCreateDate  = 0
      self.addressTypes   = []
      self.defaultAddressType = AddressEntryType_Default
      self.defaultChangeType = AddressEntryType_Default

      # Three dictionaries hold all data
      self.addrMap     = {}  # maps 20-byte addresses to PyBtcAddress objects
      self.commentsMap = {}  # maps 20-byte addresses to user-created comments
      self.commentLocs = {}  # map comment keys to wallet file locations
      self.labelName   = ''
      self.labelDescr  = ''
      self.linearAddr160List = []
      self.addrByString = {}
      self.txAddrMap = {}    # cache for getting tx-labels based on addr search
      if USE_TESTNET or USE_REGTEST:
         self.addrPoolSize = 10
      else:
         self.addrPoolSize = CLI_OPTIONS.keypool

      # For file sync features
      self.walletPath = ''
      self.lastSyncBlockNum = 0

      # Private key encryption details
      self.useEncryption = False
      self.testedComputeTime = None
      self.kdfMemoryReq = 0

      # Deterministic wallet, need a root key.  Though we can still import keys.
      # The unique ID contains the network byte (id[-1]) but is not intended to
      # resemble the address of the root key
      self._walletId    = None   # Base58 version of reversed-uniqueIDBin
      self._accountId   = None
      self._dbId        = None
      self._settingsId  = None

      self.lastComputedChainIndex = 0
      self._highestUsedChainIndex = -1

      #To enable/disable wallet row in wallet table model
      self.isEnabled = True

      #list of callables and their args to perform after a wallet
      #has been scanned. Entries are order as follows:
      #[[method1, [arg1, ar2, arg3]], [method2, [arg1, arg2]]]
      #list is cleared after each scan.
      self.actionsToTakeAfterScan = []
      self.balance = Balances()

      self.bridgeWalletObj = None
      if wltId != None:
         self.bridgeWalletObj = BridgeWalletWrapper(wltId, accId)
      elif proto != None:
         self.loadFromProto(proto)

   #############################################################################
   ## setup routines
   @staticmethod
   def loadFromBridge(walletId):
      wallet = PyBtcWallet(wltId=walletId)
      walletProto = wallet.bridgeWalletObj.getData()
      wallet.loadFromProto(walletProto)
      return wallet

   def loadFromProto(self, payload):
      self._walletId       = payload.walletId
      self._accountId      = payload.accountId
      self._dbId           = payload.dbId
      self._settingsId     = computeSettingsId(self._walletId, self._accountId)
      self.bridgeWalletObj = BridgeWalletWrapper(self._walletId, self._accountId)

      self.labelName   = payload.label
      self.labelDescr  = payload.desc

      self.useEncryption = payload.usesEncryption
      self.lastComputedChainIndex = payload.lookupCount
      self._highestUsedChainIndex = payload.useCount
      self.watchingOnly = payload.watchingOnly
      self.addressTypes = payload.addressTypes
      self.defaultAddressType = payload.defaultAddressType
      self.kdfMemoryReq = payload.kdfMemReq * (1024**2)

      #addrMap and chainIndexMap
      for addr in payload.addressData:
         addrObj = PyBtcAddress(self)
         addrObj.loadFromProto(addr)
         self.addAddress(addrObj)

      #comments
      for commentIt in payload.comments:
         self.commentsMap[commentIt.key] = commentIt.val

   ####
   def register(self, isNew):
      TheBridge.service.registerWallet(self.walletId, self.accountId, isNew)

   ####
   def syncData(self):
      dataProto = self.bridgeWalletObj.getData()
      self.lastComputedChainIndex = dataProto.lookupCount
      self._highestUsedChainIndex = dataProto.useCount

      for addr in dataProto.addressData:
         addrObj = PyBtcAddress(self)
         addrObj.loadFromProto(addr)
         self.addAddress(addrObj, updateOnly=True)

   def updateBalancesAndCount(self):
      result = self.bridgeWalletObj.getBalanceAndCount()
      self.balance = Balances.fromProto(result)

   ####
   def addAddress(self, addrObj, updateOnly: bool=False):
      prefixedHash = addrObj.getPrefixedAddr()
      if prefixedHash in self.addrMap and updateOnly:
         return

      if addrObj.parentWallet == None:
         addrObj.parentWallet = self
      self.addrMap[prefixedHash] = addrObj
      self.linearAddr160List.append(prefixedHash)

      self._highestUsedChainIndex = max(
         addrObj.chainIndex,
         self._highestUsedChainIndex)
      self.addrByString[addrObj.getAddressString()] = prefixedHash

   def getAddrDataFromDB(self):
      result = self.bridgeWalletObj.getAddrCombinedList()

      #update addr map
      for addrProto in result.updatedAssets:
         addrObj = PyBtcAddress()
         addrObj.loadFromProto(addrProto)
         self.addAddress(addrObj)

      #update balances and txio count
      for addrCombinedData in result.balances:
         addr = addrCombinedData.scrAddr
         if addr in self.addrMap:
            addrObj = self.addrMap[addr]
            addrObj.balance = Balances.fromProto(addrCombinedData.balances)
         else:
            print ("[getAddrDataFromDB] missing address " + addr.hex())

   #############################################################################
   ## comments
   def getComment(self, hashVal):
      """
      This method is used for both address comments, as well as tx comments
      In the first case, use the 20-byte addr160. Use 32-byte tx
      hash for the tx-comment case.
      """
      if hashVal in self.commentsMap:
         return self.commentsMap[hashVal]
      else:
         return ''

   def setComment(self, hashVal, newComment):
      """
      This method is used for both address comments, as well as tx comments
      In the first case, use the 20-byte addr160. Use 32-byte tx
      hash for the tx-comment case.
      """
      self.commentsMap[hashVal] = newComment
      self.bridgeWalletObj.setComment(hashVal, newComment)

   ####
   def getAddrCommentIfAvail(self, txHash):
      # If we haven't extracted relevant addresses for this tx, yet -- do it
      if txHash not in self.txAddrMap:
         self.txAddrMap[txHash] = []
         try:
            tx = TheBDM.bdv().getTxByHash(txHash)
         except:
            return ''
         if tx.isInitialized():
            for i in range(tx.getNumTxOut()):
               txout = tx.getTxOutCopy(i)
               stype = getTxOutScriptType(txout.getScript())
               scrAddr = tx.getScrAddrForTxOut(i)

               if stype in CPP_TXOUT_HAS_ADDRSTR:
                  addrStr = scrAddr_to_addrStr(scrAddr)
                  addr160 = addrStr_to_hash160(addrStr)[1]
                  if self.hasAddr(addr160):
                     self.txAddrMap[txHash].append(addr160)
               else:
                  pass

      addrComments = []
      for a160 in self.txAddrMap[txHash]:
         h160 = a160[1:]
         if h160 in self.commentsMap and '[[' not in self.commentsMap[h160]:
            addrComments.append(self.commentsMap[h160])

      return '; '.join(addrComments)

   ####
   def getAddrCommentFromLe(self, le):
      # If we haven't extracted relevant addresses for this tx, yet -- do it
      txHash = le.txHash
      if txHash not in self.txAddrMap:
         self.txAddrMap[txHash] = le.scrAddrs

      addrComments = []
      for a160 in self.txAddrMap[txHash]:
         hash160 = a160[1:]
         if hash160 in self.commentsMap and '[[' not in self.commentsMap[hash160]:
            addrComments.append(self.commentsMap[hash160])

      return '; '.join(addrComments)

   def getCommentForLE(self, le):
      # Smart comments for LedgerEntry objects:  get any direct comments ...
      # if none, then grab the one for any associated addresses.
      txHash = le.txHash
      if txHash in self.commentsMap:
         comment = self.commentsMap[txHash]
      else:
         # [[ COMMENTS ]] are not meant to be displayed on main ledger
         comment = self.getAddrCommentFromLe(le)
         if comment.startswith('[[') and comment.endswith(']]'):
            comment = ''

      return comment

   ####
   def getCommentForAddrBookEntry(self, abe):
      comment = self.getComment(abe.getAddr160())
      if len(comment)>0:
         return comment

      for regTx in abe.getTxList():
         comment = self.getComment(regTx.getTxHash())
         if len(comment)>0:
            return comment
      return ''

   ####
   def getCommentForTxList(self, a160, txhashList):
      comment = self.getComment(a160)
      if len(comment)>0:
         return comment

      for txHash in txhashList:
         comment = self.getComment(txHash)
         if len(comment)>0:
            return comment
      return ''

   #############################################################################
   ## balances & tx count getters
   def getBalance(self, balType="Spendable"):
      return self.balance.getBalance(balType)

   def getTxnCount(self):
      return self.balance.txCount

   ####
   def getAddrBalance(self, addrHash, balType="Spendable"):
      if not self.hasAddrHash(addrHash):
         return -1
      else:
         addrObj = self.addrMap[addrHash]
         return addrObj.getBalance(balType)

   def getAddrTotalTxnCount(self, addrHash):
      try:
         addrObj = self.addrMap[addrHash]
         return addrObj.getTxioCount()
      except:
         return 0

   #############################################################################
   ## UTXO/coin selection
   def initCoinSelectionInstance(self, height):
      return self.bridgeWalletObj.initCoinSelectionInstance(height)

   ####
   def getFullUTXOList(self):
      #return all mined UTXOs
      from armoryengine.CoinSelection import PyUnspentTxOut
      utxos = self.bridgeWalletObj.getUtxos(value=2**64 - 1)
      utxoList = []
      for utxo in utxos:
         utxoList.append(PyUnspentTxOut().createFromBridgeUtxo(utxo))
      return utxoList

   def getZCUTXOList(self):
      #return all ZC UTXOs
      from armoryengine.CoinSelection import PyUnspentTxOut
      utxos = self.bridgeWalletObj.getUtxos(zc=True)
      utxoList = []
      for utxo in utxos:
         utxoList.append(PyUnspentTxOut().createFromBridgeUtxo(utxo))
      return utxoList

   def getRBFTxOutList(self):
      #return all replaceable ZC outputs
      from armoryengine.CoinSelection import PyUnspentTxOut
      utxos = self.bridgeWalletObj.getUtxos(rbf=True)
      utxoList = []
      for utxo in utxos:
         utxoList.append(PyUnspentTxOut().createFromBridgeUtxo(utxo))
      return utxoList

   #############################################################################
   ## address lookups
   def getAddrByHash(self, addrHash):
      if addrHash not in self.addrMap:
         return None
      return self.addrMap[addrHash]

   def hasAddrHash(self, addrHash):
      return addrHash in self.addrMap

   def getAddrByString(self, addrStr):
      if addrStr not in self.addrByString:
         return None
      return self.addrMap[self.addrByString[addrStr]]

   def hasAddrString(self, addrStr):
      return addrStr in self.addrByString

   ####
   def getTimeRangeForAddress(self, addr160):
      if addr160 not in self.addrMap:
         return None
      else:
         return self.addrMap[addr160].getTimeRange()

   def getBlockRangeForAddress(self, addr160):
      if addr160 not in self.addrMap:
         return None
      else:
         return self.addrMap[addr160].getBlockRange()

   #############################################################################
   ## getting new addresses
   def peekChangeAddr(self, addrType=AddressEntryType_Default):
      newAddrProto = self.bridgeWalletObj.peekChangeAddress(addrType)
      newAddrObj = PyBtcAddress()
      newAddrObj.loadFromProto(newAddrProto)
      return newAddrObj

   def getNewChangeAddr(self, addrType=AddressEntryType_Default):
      newAddrProto = self.bridgeWalletObj.getChangeAddr(addrType)
      newAddrObj = PyBtcAddress()
      newAddrObj.loadFromProto(newAddrProto)
      self.addAddress(newAddrObj)
      return newAddrObj

   def getNextUnusedAddress(self, addrType=AddressEntryType_Default):
      newAddrProto = self.bridgeWalletObj.getNewAddress(addrType)
      newAddrObj = PyBtcAddress()
      newAddrObj.loadFromProto(newAddrProto)
      self.addAddress(newAddrObj)
      return newAddrObj

   ####
   def fillAddressPool(self, numPool, progressId, callback=None):
      """
      This is only ever called by explicit user request. We therefor assume
      the addresses are being restored (flagged as not new)
      """
      self.bridgeWalletObj.extendAddressPool(
         numPool, False, progressId, callback)

   ####
   def getAddressTypes(self):
      return self.addressTypes

   def getDefaultAddressType(self):
      return self.defaultAddressType

   def getDefaultChangeType(self):
      #TODO: add entry in cpp wallets for default change type
      return self.defaultChangeType

   def setAddressTypeFor(self, addrObj, addrType):
      if addrObj.addrType == addrType:
         return

      if addrType not in self.getAddressTypes():
         raise Exception("ineligible address type")

      protoAddr = self.bridgeWalletObj.setAddressTypeFor(
         addrObj.assetId, addrType)
      addrObj.loadFromProto(protoAddr)

   #############################################################################
   ## create/export
   @staticmethod
   def createNewWallet(replyCallback: callable, callbackId: str,
      shortLabel: str='', longLabel: str='', extraEntropy: bytes=None):

      """
      This method will create a new wallet, using as much customizability
      as you want.  You can enable encryption, and set the target params
      of the key-derivation function (compute-time and max memory usage).
      The KDF parameters will be experimentally determined to be as hard
      as possible for your computer within the specified time target
      (default, 0.25s).  It will aim for maximizing memory usage and using
      only 1 or 2 iterations of it, but this can be changed by scaling
      down the kdfMaxMem parameter (default 32 MB).

      If you use encryption, don't forget to supply a 32-byte passphrase,
      created via SecureBinaryData(pythonStr).  This method will apply
      the passphrase so that the wallet is "born" encrypted.

      The field plainRootKey could be used to recover a written backup
      of a wallet, since all addresses are deterministically computed
      from the root address.  This obviously won't reocver any imported
      keys, but does mean that you can recover your ENTIRE WALLET from
      only those 32 plaintext bytes AND the 32-byte chaincode.

      We skip the atomic file operations since we don't even have
      a wallet file yet to safely update.

      DO NOT CALL THIS FROM BDM METHOD.  IT MAY DEADLOCK.
      """
      LOGINFO('***Creating new deterministic wallet')

      addrPoolSize = 10 if USE_TESTNET or USE_REGTEST else CLI_OPTIONS.keypool
      TheBridge.utils.createWallet(
         addrPoolSize,
         shortLabel, longLabel, extraEntropy,
         callbackId, replyCallback
      )

   ####
   def getRootPKCC(self, pkIsCompressed=False):
      '''Get the root public key and chain code for this wallet. The key may be
         compressed or uncompressed.'''
      root = self.addrMap['ROOT']
      wltRootPubKey = root.binPublicKey65.copy().toBinStr()
      wltChainCode = root.chaincode.copy().toBinStr()

      # Neither should happen, but just in case....
      if len(wltRootPubKey) != 65:
         LOGERROR('There\'s something wrong with your watch-only wallet! The ')
         LOGERROR('root public key can\'t be retrieved.')
         return
      if len(wltChainCode) != 32:
         LOGERROR('There\'s something wrong with your watch-only wallet! The ')
         LOGERROR('root chain code can\'t be retrieved.')
         return

      # Finish assembling data for the final output.
      if pkIsCompressed == True:
         wltRootCompPubKey = \
            CryptoECDSA().CompressPoint(SecureBinaryData(wltRootPubKey))
         wltRootPubKey = wltRootCompPubKey.toBinStr()

      return (wltRootPubKey, wltChainCode)

   def getRootPKCCBackupData(self, pkIsCompressed=True, et16=True):
      '''
      Get the root public key and chain code for this wallet. The root pub
      key/chain code output format will be as follows. All data will be output
      in EasyType16 format.

      ---PART 1: Root Data ID (9 bytes)---
      - Compressed pub key's "sign byte" flag (mask 0x80) + root data format
        version (mask 0x7F)  (1 byte)
      - Wallet ID  (6 bytes)
      - Checksum of the initial byte + the wallet ID  (2 bytes)

      ---PART 2: Root Data (64 bytes)---
      - Compressed public key minus the first ("sign") byte  (32 bytes)
      - Chain code  (32 bytes)
      '''
      # Get the root pub key & chain code. The key will be compressed.
      self.wltRootPubKey, self.wltChainCode = self.getRootPKCC(True)

      # The "version byte" will actually contain the root data format version
      # (mask 0x7F) and a bit (mask 0x80) indicating if the first byte of the
      # compressed public key is 0x02 (0) or 0x03 (1). Done so that the ET16
      # output of the PK & CC will cover 4 lines, with a 5th chunk of data
      # containing everything else.
      rootPKCCFormatVer = PYROOTPKCCVER
      if self.wltRootPubKey[0] == '\x03':
         rootPKCCFormatVer ^= 0x80

      # Produce the root ID object. Convert to ET16 if necessary.
      wltRootIDConcat = int_to_binary(rootPKCCFormatVer) + self.uniqueIDBin
      rootIDConcatChksum = computeChecksum(wltRootIDConcat, nBytes=2)
      wltRootIDConcat += rootIDConcatChksum
      if et16 == True:
         lineNoSpaces = binary_to_easyType16(wltRootIDConcat)
         pcs = [lineNoSpaces[i*4:(i+1)*4] for i in range((len(lineNoSpaces)-1)/4+1)]
         wltRootIDConcat = ' '.join(pcs)

      # Get 4 rows of PK & CC data. Convert to ET16 data if necessary.
      pkccLines = []
      wltPKCCConcat = self.wltRootPubKey[1:] + self.wltChainCode
      for i in range(0, len(wltPKCCConcat), 16):
         concatData = wltPKCCConcat[i:i+16]
         if et16 == True:
            concatData = makeSixteenBytesEasy(concatData)
         pkccLines.append(concatData)

      # Return the root ID & the PK/CC data.
      return (wltRootIDConcat, pkccLines)

   def writePKCCFile(self, newPath):
      '''Make a copy of this wallet with only the public key and chain code.'''
      # Open the PKCC file for writing.
      newFile = open(newPath, 'wb')

      # Write the data to the file. The file format is as follows:
      # PKCC data format version  (UINT8)
      # Root ID  (VAR_STR)
      # Number of PKCC lines  (UINT8)
      # PKCC lines  (VAR_STR)
      outRootIDET16, outPKCCET16Lines = self.getRootPKCCBackupData(True)
      newFile.write(str(PYROOTPKCCVER) + '\n')
      newFile.write(outRootIDET16 + '\n')
      for a in outPKCCET16Lines:
         newFile.write(a + '\n')

      # Clean everything up.
      newFile.close()

   ####
   def forkOnlineWallet(self, newWalletFile, shortLabel='', longLabel=''):
      """
      Make a copy of this wallet that contains no private key data
      """
      # TODO: Fix logic, says aborting but continues with method.
      # Decide on and implement correct functionality.
      if not self.addrMap['ROOT'].hasPrivKey():
         LOGWARN('This wallet is already void of any private key data!')
         LOGWARN('Aborting wallet fork operation.')

      onlineWallet = PyBtcWallet()
      onlineWallet.fileTypeStr = self.fileTypeStr
      onlineWallet.version = self.version
      onlineWallet.wltCreateDate = self.wltCreateDate
      onlineWallet.useEncryption = False
      onlineWallet.watchingOnly = True

      if not shortLabel:
         shortLabel = self.labelName
      if not longLabel:
         longLabel = self.labelDescr

      onlineWallet.labelName  = (shortLabel + ' (Watch)')[:32]
      onlineWallet.labelDescr = (longLabel + ' (Watching-only copy)')[:256]

      newAddrMap = {}
      for addr160,addrObj in self.addrMap.items():
         onlineWallet.addrMap[addr160] = addrObj.copy()
         onlineWallet.addrMap[addr160].binPrivKey32_Encr  = SecureBinaryData()
         onlineWallet.addrMap[addr160].binPrivKey32_Plain = SecureBinaryData()
         onlineWallet.addrMap[addr160].binInitVector16    = SecureBinaryData()
         onlineWallet.addrMap[addr160].useEncryption = False
         onlineWallet.addrMap[addr160].createPrivKeyNextUnlock = False

      onlineWallet.commentsMap = self.commentsMap
      onlineWallet.opevalMap = self.opevalMap

      onlineWallet.uniqueIDBin = self.uniqueIDBin
      onlineWallet._highestUsedChainIndex     = self._highestUsedChainIndex
      onlineWallet.lastComputedChainIndex    = self.lastComputedChainIndex

      onlineWallet.writeFreshWalletFile(newWalletFile, shortLabel, longLabel)
      return onlineWallet

   ####
   def createBackupString(self, callback,
      passphrase: str=None, unlockHandler: callable=None):
      return self.bridgeWalletObj.createBackupStringForWallet(callback,
         passphrase, unlockHandler)

   #############################################################################
   ## helpers
   def getLinearAddrList(self, withImported=True, withAddrPool=False):
      """
      Retrieves a list of addresses, by hash, in the order they 
      appear in the wallet file.  Can ignore the imported addresses
      to get only chained addresses, if necessary.

      I could do this with one list comprehension, but it would be long.
      I'm resisting the urge...
      """
      addrList = []
      for a160 in self.linearAddr160List:
         addr = self.addrMap[a160]
         if not str(a160)==str('ROOT') and (withImported or addr.chainIndex>=0):
            # Either we want imported addresses, or this isn't one
            if (withAddrPool or addr.chainIndex<=self._highestUsedChainIndex):
               addrList.append(addr)
      return addrList

   def getAddrListSortedByChainIndex(self, withRoot=False):
      """ Returns Addr160 list """
      addrList = []
      for addr160 in self.linearAddr160List:
         addr = self.addrMap[addr160]
         addrList.append([addr.chainIndex, addr160, addr])

      addrList.sort(key=lambda x: x[0])
      return addrList

   ####
   def pprint(self, indent='', allAddrInfo=True):
      print(indent + 'PyBtcWallet  :', self.walletId)
      print(indent + '   useEncrypt:', self.useEncryption)
      print(indent + '   watchOnly :', self.watchingOnly)
      print(indent + '   isLocked  :', self.isLocked)
      print(indent + '   ShortLabel:', self.labelName)
      print(indent + '   LongLabel :', self.labelDescr)
      print('')
      print(indent + 'Root key:', self.addrMap['ROOT'].getAddrStr(), end=' ')
      print('(this address is never used)')
      if allAddrInfo:
         self.addrMap['ROOT'].pprint(indent=indent)
      print(indent + 'All usable keys:')
      sortedAddrList = self.getAddrListSortedByChainIndex()
      for i,addr160,addrObj in sortedAddrList:
         if not addr160=='ROOT':
            print('\n' + indent + 'Address:', addrObj.getAddrStr())
            if allAddrInfo:
               addrObj.pprint(indent=indent)

   ####
   def getHistoryAsCSV(self, currentTop):
      file = open('%s.csv' % self.walletPath, 'wb')

      sortedAddrList = self.getAddrListSortedByChainIndex()
      chainCode = sortedAddrList[0][2].chaincode.toHexStr()

      bal = self.getBalance('full')
      bal = bal  / float(100000000)
      file.write("%s,%f,%s,#%d\n" % (self.walletId, bal, chainCode, currentTop))


      for i,addr160,addrObj in sortedAddrList:
         cppAddr = self.cppWallet.getScrAddrObjByKey(Hash160ToScrAddr(addr160))
         bal = cppAddr.getFullBalance() / float(100000000)

         le = cppAddr.getFirstLedger()
         unixtime = le.getTxTime()
         block = le.getBlockNum()

         if unixtime == 0:
            block = 0

         realtime = datetime.fromtimestamp(unixtime).strftime('%Y-%m-%d %H:%M:%S')
         timeAndBlock = ",#%d,%s,%d" % (block, realtime, unixtime)

         cppAddrObj = self.cppWallet.getAddrObjByIndex(addrObj.chainIndex)
         putStr = '%d,%s,%s,%f%s\n' \
                  % (i, cppAddrObj.getScrAddr(), addrObj.binPublicKey65.toHexStr(), bal, \
                     (timeAndBlock if unixtime != 0 else ""))

         file.write(putStr)

      file.close()

   ####
   def changePassphrase(self, isPriv, callbackId, callback):
      self.bridgeWalletObj.changePassphrase(isPriv, callbackId, callback)

   ####
   def createAddressBook(self):
      return self.bridgeWalletObj.createAddressBook()

   ####
   def setLabels(self, lshort, llong=''):
      self.labelName = lshort
      self.labelDescr = llong
      self.bridgeWalletObj.setLabels(self.labelName, self.labelDescr)

   ####
   def delete(self):
      return TheBridge.wltManager.deleteWallet(self.walletId)

   ###############################################################################
   ## UI related
   def getHistoryPage(self, pageID):
      try:
         return self.cppWallet.getHistoryPage(pageID)
      except:
         raise Exception('pageID is out of range')

   ####
   def disableWalletUI(self):
      self.isEnabled = False

   ####
   def returnFilteredAddrList(self, filterUse, filterType):
      addrList = []
      keepInUse = bool(filterUse != "Unused")
      keepChange = bool(filterUse == "Change")

      for addr in self.linearAddr160List:
         addrObj = self.addrMap[addr]
         if addrObj.filter(filterType, keepInUse, keepChange) == True:
            addrList.append(addrObj)
      return addrList

   def getFilteredAddrCount(self, filterUse, filterType = None):
      count = 0
      keepInUse = bool(filterUse != "Unused")
      keepChange = bool(filterUse == "Change")

      for addr in self.linearAddr160List:
         addrObj = self.addrMap[addr]
         if addrObj.filter(filterType, keepInUse, keepChange) == True:
            count += 1
      return count

   ####
   def getLedgerDelegateIdForScrAddr(self, scrAddr):
      return self.bridgeWalletObj.getLedgerDelegateIdForScrAddr(scrAddr)

   #############################################################################
   ## properties
   @property
   def walletId(self):
      if not self._walletId:
         raise Exception("missing walletId!")
      return self._walletId

   @property
   def accountId(self):
      if not self._accountId:
         raise Exception("missing accountId!")
      return self._accountId

   @property
   def dbId(self):
      if not self._dbId:
         raise Exception("missing dbId!")
      return self._dbId

   @property
   def settingsId(self):
      if not self._settingsId:
         raise Exception("missing settingsId!")
      return self._settingsId

   ####
   def getHighestUsedIndex(self):
      """
      This only retrieves the stored value, but it may not be correct if,
      for instance, the wallet was just imported but has been used before.
      """
      return self._highestUsedChainIndex + 1

   def getHighestComputedIndex(self):
      """
      This only retrieves the stored value, but it may not be correct if,
      for instance, the wallet was just imported but has been used before.
      """
      return self.lastComputedChainIndex

   ####
   def getWalletVersion(self):
      return (getVersionInt(self.version), getVersionString(self.version))

   ####
   def getKdfMemoryReqtBytes(self):
      return self.kdfMemoryReq

   def testKdfComputeTime(self, callback):
      """
      Experimentally determines the compute time required by this computer
      to execute with the current key-derivation parameters. This may be
      useful for when you transfer a wallet to a new computer that has
      different speed/memory characteristic.
      """
      def handleReply(reply):
         if reply.success == False:
            LOGERROR("failed to test KDF")
         else:
            self.testedComputeTime = reply.wallet.getUnlockTime
         callback(reply.success, self.testedComputeTime)
      self.bridgeWalletObj.getUnlockTime(handleReply)

   ####
   def getWalletPath(self, nameSuffix=None):
      raise Exception("fixme")
      fpath = self.walletPath

      if self.walletPath=='':
         fpath = os.path.join(ARMORY_HOME_DIR, buildWltFileName(self.uniqueIDB58))

      if not nameSuffix==None:
         pieces = os.path.splitext(fpath)
         if not pieces[0].endswith('_'):
            fpath = pieces[0] + '_' + nameSuffix + pieces[1]
         else:
            fpath = pieces[0] + nameSuffix + pieces[1]
      return fpath

   ####
   def getDisplayStr(self, pref="Wallet: "):
      return '%s"%s" (%s)' % (pref, self.labelName, self.walletId)

   #############################################################################
   ## settings
   def setSetting(self, propName, value):
      wltPropName = f'Wallet_{self.settingsId}_{propName}'
      TheSettings.set(wltPropName, value)

   def getSetting(self, propName, defaultValue=''):
      # Sometimes we need to settings specific to individual wallets -- we will
      # prefix the settings name with the wltID.
      wltPropName = f'Wallet_{self.settingsId}_{propName}'
      if TheSettings.hasSetting(wltPropName):
         return TheSettings.get(wltPropName)
      else:
         if not defaultValue=='':
            self.setSetting(propName, defaultValue)
         return defaultValue

   ###############################################################################
   ## misc/deprecated/regressions
   def doAfterScan(self):
      actionsList = self.actionsToTakeAfterScan
      self.actionsToTakeAfterScan = []

      for calls in actionsList:
         calls[0](*calls[1])

   def sweepAfterRescan(self, addrList, main): 
      #get a new address from the wallet to sweep the funds to
      sweepToAddr = self.getNextUnusedAddress().getAddr160()

      main.finishSweepScan(self, addrList, sweepToAddr)
      return

   def sweepAddressList(self, addrList, main):
      self.actionsToTakeAfterScan.append(
         [self.sweepAfterRescan, [addrList, main]])

      addrVec = []
      for addr in addrList:
         addrVec.append(ADDRBYTE + addr.getAddr160())

      _id = Cpp.SecureBinaryData().GenerateRandom(8).toHexStr()
      main.oneTimeScanAction[_id] = self.doAfterScan()
      TheBDM.bdv().registerAddrList(_id, addrList)

   ####
   def isWltSigningAnyLockbox(self, lockboxList):
      for lockbox in lockboxList:
         for addr160 in lockbox.a160List:
            if addr160 in self.addrMap:
               return True
      return False
