from __future__ import (absolute_import, division,
                        print_function, unicode_literals)
################################################################################
#                                                                              #
# Copyright (C) 2011-2015, Armory Technologies, Inc.                           #
# Distributed under the GNU Affero General Public License (AGPL v3)            #
# See LICENSE or http://www.gnu.org/licenses/agpl.html                         #
#                                                                              #
# Copyright (C) 2016-2023, goatpig                                             #
#  Distributed under the MIT license                                           #
#  See LICENSE-MIT or https://opensource.org/licenses/MIT                      #
#                                                                              #
################################################################################
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
from armoryengine.AddressUtils import addrStr_to_hash160, \
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

DEFAULT_COMPUTE_TIME_TARGET = 0.25
DEFAULT_MAXMEM_LIMIT        = 32*1024*1024

PYROOTPKCCVER = 1 # Current version of root pub key/chain code backup format
PYROOTPKCCVERMASK = 0x7F
PYROOTPKCCSIGNMASK = 0x80

# Only works on PyBtcWallet
# If first arg is not PyBtcWallet call the function as if it was
# not decorated, it should throw whatever error or do whatever it would
# do withouth this decorator. This decorator does nothing if applied to 
# the methods of any other class
def CheckWalletRegistration(func):
   def inner(*args, **kwargs):
      if len(args)>0 and isinstance(args[0],PyBtcWallet):
         if args[0].isRegistered():
            return func(*args, **kwargs)
         elif 'doRegister' in kwargs and kwargs['doRegister'] == False:
            return func(*args, **kwargs)
         else:
            raise WalletUnregisteredError
      else:
         return func(*args, **kwargs)
   return inner

def buildWltFileName(uniqueIDB58):
   return 'armory_%s_.wallet' % uniqueIDB58

def computeSettingsId(wltId, accId):
   #armory's py implementation of hmac mangles the 2nd half but
   #we don't care for the purpose of concatenating ids
   hmacStr = HMAC256(wltId, accId)
   b58Str = binary_to_base58(hmacStr)
   return b58Str[0:7]

class PyBtcWallet(object):
   """
   This class encapsulates all the concepts and variables in a "wallet",
   and maintains the passphrase protection, key stretching, encryption,
   etc, required to maintain the wallet.  This class also includes the
   file I/O methods for storing and loading wallets.

   ***NOTE:  I have ONLY implemented deterministic wallets, using ECDSA
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

   This class is included in the combined-python-cpp module, because we really
   need to maintain a persistent Cpp.BtcWallet if this class is to be useful
   (we don't want to have to rescan the entire blockchain every time we do any
   wallet operations).

   The file format was designed from the outset with lots of unused space to
   allow for expansion without having to redefine the file format and break
   previous wallets.  Luckily, wallet information is cheap, so we don't have
   to stress too much about saving space (100,000 addresses should take 15 MB)

   This file is NOT for storing Tx-related information.  I want this file to
   be the minimal amount of information you need to secure and backup your
   entire wallet.  Tx information can always be recovered from examining the
   blockchain... your private keys cannot be.

   We track version numbers, just in case.  We start with 1.0

   Version 1.0:
   ---
   fileID      -- (8)  '\xbaWALLET\x00' for wallet files
   version     -- (4)   getVersionInt(PYBTCWALLET_VERSION)
   magic bytes -- (4)   defines the blockchain for this wallet (BTC, NMC)
   wlt flags   -- (8)   64 bits/flags representing info about wallet
   binUniqueID -- (6)   first 5 bytes of first address in wallet
                        (rootAddr25Bytes[:5][::-1]), reversed
                        This is not intended to look like the root addr str
                        and is reversed to avoid having all wallet IDs start 
                        with the same characters (since the network byte is front)
   create date -- (8)   unix timestamp of when this wallet was created
                        (actually, the earliest creation date of any addr
                        in this wallet -- in the case of importing addr
                        data).  This is used to improve blockchain searching
   Short Name  -- (32)  Null-terminated user-supplied short name for wlt
   Long Name   -- (256) Null-terminated user-supplied description for wlt
   Highest Used-- (8)   The chain index of the highest used address
   ---
   Crypto/KDF  -- (512) information identifying the types and parameters
                        of encryption used to secure wallet, and key
                        stretching used to secure your passphrase.
                        Includes salt. (the breakdown of this field will
                        be described separately)
   KeyGenerator-- (237) The base address for a determinstic wallet.
                        Just a serialized PyBtcAddress object.
   ---
   UNUSED     -- (1024) unused space for future expansion of wallet file
   ---
   Remainder of file is for key storage and various other things.  Each
   "entry" will start with a 4-byte code identifying the entry type, then
   20 bytes identifying what address the data is for, and finally then
   the subsequent data .  So far, I have three types of entries that can
   be included:

      \x01 -- Address/Key data (as of PyBtcAddress version 1.0, 237 bytes)
      \x02 -- Address comments (variable-width field)
      \x03 -- Address comments (variable-width field)
      \x04 -- OP_EVAL subscript (when this is enabled, in the future)

   Please see PyBtcAddress for information on how key data is serialized.
   Comments (\x02) are var-width, and if a comment is changed to
   something longer than the existing one, we'll just blank out the old
   one and append a new one to the end of the file.  It looks like

   02000000 01 <Addr> 4f This comment is enabled (01) with 4f characters


   For file syncing, we protect against corrupted wallets by doing atomic
   operations before even telling the user that new data has been added.
   We do this by copying the wallet file, and creating a walletUpdateFailed
   file.  We then modify the original, verify its integrity, and then delete
   the walletUpdateFailed file.  Then we create a backupUpdateFailed flag,
   do the identical update on the backup file, and delete the failed flag. 
   This guaranatees that no matter which nanosecond the power goes out,
   there will be an uncorrupted wallet and we know which one it is.

   We never let the user see any data until the atomic write-to-file operation
   has completed


   Additionally, we implement key locking and unlocking, with timeout.  These
   key locking features are only DEFINED here, not actually enforced (because
   this is a library, not an application).  You can set the default/temporary
   time that the KDF key is maintained in memory after the passphrase is
   entered, and this class will keep track of when the wallet should be next
   locked.  It is up to the application to check whether the current time
   exceeds the lock time.  This will probably be done in a kind of heartbeat
   method, which checks every few seconds for all sorts of things -- including
   wallet locking.
   """

   #############################################################################
   def __init__(self, *, wltId=None, accId=None, proto=None):
      self.fileTypeStr    = '\xbaWALLET\x00'
      self.version        = PYBTCWALLET_VERSION  # (Major, Minor, Minor++, even-more-minor)
      self.eofByte        = 0
      self.watchingOnly   = True
      self.wltCreateDate  = 0
      self.addressTypes   = []
      self.defaultAddressType = AddressEntryType_Default
      self.defaultChangeType = AddressEntryType_Default

      # Three dictionaries hold all data
      self.addrMap     = {}  # maps 20-byte addresses to PyBtcAddress objects
      self.commentsMap = {}  # maps 20-byte addresses to user-created comments
      self.commentLocs = {}  # map comment keys to wallet file locations
      self.opevalMap   = {}  # maps 20-byte addresses to OP_EVAL data (future)
      self.labelName   = ''
      self.labelDescr  = ''
      self.linearAddr160List = []
      self.addrByString = {}
      self.txAddrMap = {}    # cache for getting tx-labels based on addr search
      if USE_TESTNET or USE_REGTEST:
         self.addrPoolSize = 10  # this makes debugging so much easier!
      else:
         self.addrPoolSize = CLI_OPTIONS.keypool

      self.importList = []

      # For file sync features
      self.walletPath = ''
      self.doBlockchainSync = BLOCKCHAIN_READONLY
      self.lastSyncBlockNum = 0

      # Private key encryption details
      self.useEncryption  = False
      self.testedComputeTime = None
      self.kdfMemoryReq = 0

      # Deterministic wallet, need a root key.  Though we can still import keys.
      # The unique ID contains the network byte (id[-1]) but is not intended to
      # resemble the address of the root key
      #self.uniqueIDBin = ''
      self._walletId    = None   # Base58 version of reversed-uniqueIDBin
      self._accountId   = None
      self._dbId        = None
      self._settingsId  = None

      self.lastComputedChainAddr160 = None
      self.lastComputedChainIndex   = 0
      self.highestUsedChainIndex    = 0

      # All PyBtcAddress serializations are exact same size, figure it out now
      self.pybtcaddrSize = 237

      # Finally, a bunch of offsets that tell us where data is stored in the
      # file: this can be generated automatically on unpacking (meaning it
      # doesn't require manually updating offsets if I change the format), and
      # will save us a couple lines of code later, when we need to update things
      self.offsetWltFlags  = -1
      self.offsetLabelName = -1
      self.offsetLabelDescr  = -1
      self.offsetTopUsed   = -1
      self.offsetRootAddr  = -1
      self.offsetKdfParams = -1
      self.offsetCrypto    = -1

      # These flags are ONLY for unit-testing the walletFileSafeUpdate function
      self.interruptTest1  = False
      self.interruptTest2  = False
      self.interruptTest3  = False

      #flags the wallet if it has off chain imports (from a consistency repair)
      self.hasNegativeImports = False

      #To enable/disable wallet row in wallet table model
      self.isEnabled = True

      #list of callables and their args to perform after a wallet 
      #has been scanned. Entries are order as follows:
      #[[method1, [arg1, ar2, arg3]], [method2, [arg1, arg2]]]
      #list is cleared after each scan.
      self.actionsToTakeAfterScan = []

      self.balance_spendable = 0
      self.balance_unconfirmed = 0
      self.balance_full = 0
      self.txnCount = 0

      self.bridgeWalletObj = None
      if wltId != None:
         self.bridgeWalletObj = BridgeWalletWrapper(wltId, accId)
      elif proto != None:
         self.loadFromProto(proto)
         self.bridgeWalletObj = BridgeWalletWrapper(
            self.walletId, self.accountId)

   #############################################################################
   def isWltSigningAnyLockbox(self, lockboxList):
      for lockbox in lockboxList:
         for addr160 in lockbox.a160List:
            if addr160 in self.addrMap:
               return True
      return False

   #############################################################################
   def getWalletVersion(self):
      return (getVersionInt(self.version), getVersionString(self.version))

   #############################################################################
   def getTimeRangeForAddress(self, addr160):
      if addr160 not in self.addrMap:
         return None
      else:
         return self.addrMap[addr160].getTimeRange()

   #############################################################################
   def getBlockRangeForAddress(self, addr160):
      if addr160 not in self.addrMap:
         return None
      else:
         return self.addrMap[addr160].getBlockRange()

   #############################################################################
   def setBlockchainSyncFlag(self, syncYes=True):
      self.doBlockchainSync = syncYes

   #############################################################################
   def getCommentForAddrBookEntry(self, abe):
      comment = self.getComment(abe.getAddr160())
      if len(comment)>0:
         return comment

      # SWIG BUG!
      # http://sourceforge.net/tracker/?func=detail&atid=101645&aid=3403085&group_id=1645
      # Apparently, using the -threads option when compiling the swig module
      # causes the "for i in vector<...>:" mechanic to sometimes throw seg faults!
      # For this reason, this method was replaced with the one below:
      for regTx in abe.getTxList():
         comment = self.getComment(regTx.getTxHash())
         if len(comment)>0:
            return comment

      return ''

   #############################################################################
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
   def hasAnyImported(self):
      for a160,addr in self.addrMap.items():
         if addr.chainIndex == -2:
            return True
      return False

   def isRegistered(self):
      return True

   #############################################################################
   # The IGNOREZC args on the get*Balance calls determine whether unconfirmed
   # change (sent-to-self) will be considered spendable or unconfirmed.  This
   # was added after the malleability issues cropped up in Feb 2014.  Zero-conf
   # change was always deprioritized, but using --nospendzeroconfchange makes
   # it totally unspendable
   def getBalance(self, balType="Spendable"):
      if balType.lower() in ('spendable','spend'):
         return self.balance_spendable
      elif balType.lower() in ('unconfirmed','unconf'):
         return self.balance_unconfirmed
      elif balType.lower() in ('total','ultimate','unspent','full'):
         return self.balance_full
      else:
         raise TypeError('Unknown balance type! "' + balType + '"')

   #############################################################################
   def getTxnCount(self):
      return self.txnCount

   #############################################################################  
   def updateBalancesAndCount(self):
      result = self.bridgeWalletObj.getBalanceAndCount()
      self.balance_full = result.full
      self.balance_spendable = result.spendable
      self.balance_unconfirmed = result.unconfirmed
      self.txnCount = result.txnCount

   #############################################################################
   def getAddrBalance(self, addrHash, balType="Spendable", topBlockHeight=UINT32_MAX):
      if not self.hasAddrHash(addrHash):
         return -1
      else:
         addrObj = self.addrMap[addrHash]

         if balType.lower() in ('spendable','spend'):
            return addrObj.getSpendableBalance()
         elif balType.lower() in ('unconfirmed','unconf'):
            return addrObj.getUnconfirmedBalance()
         elif balType.lower() in ('ultimate','unspent','full'):
            return addrObj.getFullBalance()
         else:
            raise TypeError('Unknown balance type!')

   #############################################################################
   @CheckWalletRegistration
   def getTxLedger(self, ledgType='Full'):
      """
      Gets the ledger entries for the entire wallet, from C++/SWIG data structs
      """
      ledgBlkChain = self.getHistoryPage(0)
      ledg = []
      ledg.extend(ledgBlkChain)
      return ledg

   #############################################################################
   @CheckWalletRegistration
   def getUTXOListForSpendVal(self, valToSpend = 2**64 - 1):
      """ Returns UnspentTxOut/C++ objects 
      returns a set of unspent TxOuts to cover for the value to spend 
      """

      if not self.doBlockchainSync==BLOCKCHAIN_DONOTUSE:
         from armoryengine.CoinSelection import PyUnspentTxOut
         utxos = self.bridgeWalletObj.getUtxosForValue(self.uniqueIDB58, valToSpend)
         utxoList = []
         for i in range(len(utxos)):
            utxoList.append(PyUnspentTxOut().createFromBridgeUtxo(utxos[i]))
         return utxoList
      else:
         LOGERROR('***Blockchain is not available for accessing wallet-tx data')
         return []

   #############################################################################
   @CheckWalletRegistration
   def getFullUTXOList(self):
      """
      DO NOT USE THIS CALL UNLESS NECESSARY.
      This call returns *ALL* of the wallet's UTXOs. If your intent is to get
      UTXOs to spend coins, use getUTXOListForSpendVal and pass the amount you 
      want to spend as the argument.
      
      If you want to get UTXOs for browsing the history, use 
      getUTXOListForBlockRange with the top and bottom block of the desired range
      """

      #return full set of unspent TxOuts
      if not self.doBlockchainSync==BLOCKCHAIN_DONOTUSE:
         #calling this with no value argument will return the full UTXO list
         from armoryengine.CoinSelection import PyUnspentTxOut
         utxos = self.bridgeWalletObj.getUtxos(value=2**64 - 1)
         utxoList = []
         for utxo in utxos:
            utxoList.append(PyUnspentTxOut().createFromBridgeUtxo(utxo))
         return utxoList
      else:
         LOGERROR('***Blockchain is not available for accessing wallet-tx data')
         return []

   #############################################################################
   @CheckWalletRegistration
   def getZCUTXOList(self):
      #return full set of unspent ZC outputs
      if not self.doBlockchainSync==BLOCKCHAIN_DONOTUSE:
         from armoryengine.CoinSelection import PyUnspentTxOut
         utxos = self.bridgeWalletObj.getUtxos(zc=True)
         utxoList = []
         for utxo in utxos:
            utxoList.append(PyUnspentTxOut().createFromBridgeUtxo(utxo))
         return utxoList
      else:
         LOGERROR('***Blockchain is not available for accessing wallet-tx data')
         return []

   #############################################################################
   @CheckWalletRegistration
   def getRBFTxOutList(self):
      #return full set of unspent ZC outputs
      if not self.doBlockchainSync==BLOCKCHAIN_DONOTUSE:
         from armoryengine.CoinSelection import PyUnspentTxOut
         utxos = self.bridgeWalletObj.getUtxos(rbf=True)
         utxoList = []
         for i in range(len(utxos.utxo)):
            utxoList.append(PyUnspentTxOut().createFromBridgeUtxo(utxos.utxo[i]))
         return utxoList
      else:
         LOGERROR('***Blockchain is not available for accessing wallet-tx data')
         return []

   #############################################################################
   def getAddrByHash(self, addrHash):
      if addrHash not in self.addrMap:
         return None
      return self.addrMap[addrHash]

   #############################################################################
   def hasAddrHash(self, addrHash):
      return addrHash in self.addrMap

   #############################################################################
   def getAddrByString(self, addrStr):
      if addrStr not in self.addrByString:
         return None
      return self.addrMap[self.addrByString[addrStr]]

   #############################################################################
   def hasAddrString(self, addrStr):
      return addrStr in self.addrByString

   #############################################################################
   @staticmethod
   def createNewWallet(passphrase=None, \
      kdfTargSec=DEFAULT_COMPUTE_TIME_TARGET, kdfMaxMem=DEFAULT_MAXMEM_LIMIT, \
      shortLabel='', longLabel='', extraEntropy=None):

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

      #create cpp wallet
      walletId = TheBridge.utils.createWallet(
         addrPoolSize,
         passphrase, "",
         #kdfTargSec, kdfMaxMem,
         shortLabel, longLabel,
         extraEntropy)
      return PyBtcWallet().loadFromBridge(walletId)

   #############################################################################
   @staticmethod
   def loadFromBridge(walletId):
      wallet = PyBtcWallet(uniqueId=walletId)
      walletProto = wallet.bridgeWalletObj.getData()
      wallet.loadFromProto(walletProto)
      return wallet

   #############################################################################
   def peekChangeAddr(self, addrType=AddressEntryType_Default):
      newAddrProto = self.bridgeWalletObj.getNewAddress(addrType)
      newAddrObj = PyBtcAddress()
      newAddrObj.loadFromProto(newAddrProto)
      return newAddrObj

   #############################################################################
   def addAddress(self, addrObj):
      prefixedHash = addrObj.getPrefixedAddr()
      self.addrMap[prefixedHash] = addrObj
      self.linearAddr160List.append(prefixedHash)

      self.highestUsedChainIndex = \
         max(addrObj.chainIndex, self.highestUsedChainIndex)

      self.addrByString[addrObj.getAddressString()] = prefixedHash

   #############################################################################
   def getNewChangeAddr(self, addrType=AddressEntryType_Default):
      newAddrProto = self.bridgeWalletObj.getNewAddress(addrType)
      newAddrObj = PyBtcAddress()
      newAddrObj.loadFromProto(newAddrProto)

      self.addAddress(newAddrObj)
      return newAddrObj

   #############################################################################
   def getNextUnusedAddress(self, addrType=AddressEntryType_Default):
      newAddrProto = self.bridgeWalletObj.getNewAddress(addrType)
      newAddrObj = PyBtcAddress()
      newAddrObj.loadFromProto(newAddrProto)

      self.addAddress(newAddrObj)
      return newAddrObj

   #############################################################################
   def getHighestUsedIndex(self):
      """ 
      This only retrieves the stored value, but it may not be correct if,
      for instance, the wallet was just imported but has been used before.
      """
      return self.highestUsedChainIndex

   #############################################################################
   def getHighestComputedIndex(self):
      """
      This only retrieves the stored value, but it may not be correct if,
      for instance, the wallet was just imported but has been used before.
      """
      return self.lastComputedChainIndex

   #############################################################################
   @CheckWalletRegistration
   def detectHighestUsedIndex(self):
      """
      This method is used to find the highestUsedChainIndex value of the 
      wallet WITHIN its address pool.  It will NOT extend its address pool
      in this search, because it is assumed that the wallet couldn't have
      used any addresses it had not calculated yet.

      If you have a wallet IMPORT, though, or a wallet that has been used
      before but does not have this information stored with it, then you
      should be using the next method:

            self.freshImportFindHighestIndex()

      which will actually extend the address pool as necessary to find the
      highest address used.
      """

      highestIndex = self.bridgeWalletObj.getHighestUsedIndex()
      if highestIndex > self.highestUsedChainIndex:
         self.highestUsedChainIndex = highestIndex

      return highestIndex

   #############################################################################
   @TimeThisFunction
   def freshImportFindHighestIndex(self, stepSize=None):
      """
      This is much like detectHighestUsedIndex, except this will extend the
      address pool as necessary.  It assumes that you have a fresh wallet
      that has been used before, but was deleted and restored from its root
      key and chaincode, and thus we don't know if only 10 or 10,000 addresses
      were used.

      If this was an exceptionally active wallet, it's possible that we
      may need to manually increase the step size to be sure we find
      everything.  In fact, there is no way to tell FOR SURE what is the
      last addressed used: one must make an assumption that the wallet
      never calculated more than X addresses without receiving a payment...
      """
      if not stepSize:
         stepSize = self.addrPoolSize

      topCompute = 0
      topUsed    = 0
      oldPoolSize = self.addrPoolSize
      self.addrPoolSize = stepSize
      # When we hit the highest address, the topCompute value will extend
      # out [stepsize] addresses beyond topUsed, and the topUsed will not
      # change, thus escaping the while loop
      nWhile = 0
      while topCompute - topUsed < 0.9*stepSize:
         topCompute = self.fillAddressPool(stepSize, isActuallyNew=False)
         topUsed = self.detectHighestUsedIndex()
         nWhile += 1
         if nWhile>10000:
            raise WalletAddressError('Escaping inf loop in freshImport...')

      self.addrPoolSize = oldPoolSize
      return topUsed

   #############################################################################
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

   #############################################################################
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

   #############################################################################
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

   #############################################################################
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
      onlineWallet.highestUsedChainIndex     = self.highestUsedChainIndex
      onlineWallet.lastComputedChainAddr160  = self.lastComputedChainAddr160
      onlineWallet.lastComputedChainIndex    = self.lastComputedChainIndex

      onlineWallet.writeFreshWalletFile(newWalletFile, shortLabel, longLabel)
      return onlineWallet

   #############################################################################
   def testKdfComputeTime(self):
      """
      Experimentally determines the compute time required by this computer
      to execute with the current key-derivation parameters.  This may be
      useful for when you transfer a wallet to a new computer that has
      different speed/memory characteristic.
      """
      testPassphrase = SecureBinaryData('This is a simple passphrase')
      start = RightNow()
      self.kdf.DeriveKey(testPassphrase)
      self.testedComputeTime = (RightNow()-start)
      return self.testedComputeTime

   #############################################################################
   def verifyPassphrase(self, securePassphrase):
      """
      Verify a user-submitted passphrase.  This passphrase goes into
      the key-derivation function to get actual encryption key, which
      is what actually needs to be verified

      Since all addresses should have the same encryption, we only need
      to verify correctness on the root key
      """
      kdfOutput = self.kdf.DeriveKey(securePassphrase)
      try:
         isValid = self.addrMap['ROOT'].verifyEncryptionKey(kdfOutput)
         return isValid
      finally:
         kdfOutput.destroy()

   #############################################################################
   def verifyEncryptionKey(self, secureKdfOutput):
      """
      Verify the underlying encryption key (from KDF).
      Since all addresses should have the same encryption,
      we only need to verify correctness on the root key.
      """
      return self.addrMap['ROOT'].verifyEncryptionKey(secureKdfOutput)

   #############################################################################
   def computeSystemSpecificKdfParams(self, targetSec=0.25, maxMem=32*1024*1024):
      """
      WARNING!!! DO NOT CHANGE KDF PARAMS AFTER ALREADY ENCRYPTED THE WALLET
                 By changing them on an already-encrypted wallet, we are going
                 to lose the original AES256-encryption keys -- which are
                 uniquely determined by (numIter, memReqt, salt, passphrase)

                 Only use this method before you have encrypted your wallet,
                 in order to determine good KDF parameters based on your
                 computer's specific speed/memory capabilities.
      """
      kdf = KdfRomix()
      kdf.computeKdfParams(targetSec, long(maxMem))

      mem   = kdf.getMemoryReqtBytes()
      nIter = kdf.getNumIterations()
      salt  = SecureBinaryData(kdf.getSalt().toBinStr())
      return (mem, nIter, salt)

   #############################################################################
   def restoreKdfParams(self, mem, numIter, secureSalt):
      """
      This method should only be used when we are loading an encrypted wallet
      from file.  DO NOT USE THIS TO CHANGE KDF PARAMETERS.  Doing so may
      result in data loss!
      """
      self.kdf = KdfRomix(mem, numIter, secureSalt)

   #############################################################################
   def changeKdfParams(self, mem, numIter, salt, securePassphrase=None):
      """
      Changing KDF changes the wallet encryption key which means that a KDF
      change is essentially the same as an encryption key change.  As such,
      the wallet must be unlocked if you intend to change an already-
      encrypted wallet with KDF.

      TODO: this comment doesn't belong here...where does it go? :
      If the KDF is NOT yet setup, this method will do it.  Supply the target
      compute time, and maximum memory requirements, and the underlying C++
      code will experimentally determine the "hardest" key-derivation params
      that will run within the specified time and memory usage on the system
      executing this method.  You should set the max memory usage very low
      (a few kB) for devices like smartphones, which have limited memory
      availability.  The KDF will then use less memory but more iterations
      to achieve the same compute time.
      """
      if self.useEncryption:
         if not securePassphrase:
            LOGERROR('')
            LOGERROR('You have requested changing the key-derivation')
            LOGERROR('parameters on an already-encrypted wallet, which')
            LOGERROR('requires modifying the encryption on this wallet.')
            LOGERROR('Please unlock your wallet before attempting to')
            LOGERROR('change the KDF parameters.')
            raise WalletLockError('Cannot change KDF without unlocking wallet')
         elif not self.verifyPassphrase(securePassphrase):
            LOGERROR('Incorrect passphrase to unlock wallet')
            raise PassphraseError('Incorrect passphrase to unlock wallet')

      secureSalt = SecureBinaryData(salt)
      newkdf = KdfRomix(mem, numIter, secureSalt)
      bp = BinaryPacker()
      bp.put(BINARY_CHUNK, self.serializeKdfParams(newkdf), width=256)
      updList = [[WLT_UPDATE_MODIFY, self.offsetKdfParams, bp.getBinaryString()]]

      if not self.useEncryption:
         # We may be setting the kdf params before enabling encryption
         self.walletFileSafeUpdate(updList)
      else:
         # Must change the encryption key: and we won't get here unless
         # we have a passphrase to use.  This call will take the
         self.changeWalletEncryption(securePassphrase=securePassphrase, \
                                     extraFileUpdates=updList, kdfObj=newkdf)

      self.kdf = newkdf

   #############################################################################
   def changeWalletEncryption(self, secureKdfOutput=None, \
                                    securePassphrase=None, \
                                    extraFileUpdates=[],
                                    kdfObj=None, Progress=emptyFunc):
      """
      Supply the passphrase you would like to use to encrypt this wallet
      (or supply the KDF output directly, to skip the passphrase part).
      This method will attempt to re-encrypt with the new passphrase.
      This fails if the wallet is already locked with a different passphrase.
      If encryption is already enabled, please unlock the wallet before
      calling this method.

      Make sure you set up the key-derivation function (KDF) before changing
      from an unencrypted to an encrypted wallet.  An error will be thrown
      if you don't.  You can use something like the following

         # For a target of 0.05-0.1s compute time:
         (mem,nIter,salt) = wlt.computeSystemSpecificKdfParams(0.1)
         wlt.changeKdfParams(mem, nIter, salt)

      Use the extraFileUpdates to pass in other changes that need to be
      written to the wallet file in the same atomic operation as the
      encryption key modifications.
      """

      if not kdfObj:
         kdfObj = self.kdf

      oldUsedEncryption = self.useEncryption
      if securePassphrase or secureKdfOutput:
         newUsesEncryption = True
      else:
         newUsesEncryption = False

      oldKdfKey = None
      if oldUsedEncryption:
         if self.isLocked:
            raise WalletLockError('Must unlock wallet to change passphrase')
         else:
            oldKdfKey = self.kdfKey.copy()


      if newUsesEncryption and not self.kdf:
         raise EncryptionError('KDF must be setup before encrypting wallet')

      # Prep the file-update list with extras passed in as argument
      walletUpdateInfo = list(extraFileUpdates)

      # Derive the new KDF key if a passphrase was supplied
      newKdfKey = secureKdfOutput
      if securePassphrase:
         newKdfKey = self.kdf.DeriveKey(securePassphrase)

      if oldUsedEncryption and newUsesEncryption and self.verifyEncryptionKey(newKdfKey):
         LOGWARN('Attempting to change encryption to same passphrase!')
         return # Wallet is encrypted with the new passphrase already


      # With unlocked key data, put the rest in a try/except/finally block
      # To make sure we destroy the temporary kdf outputs
      try:
         # If keys were previously unencrypted, they will be not have
         # initialization vectors and need to be generated before encrypting.
         # This is why we have the enableKeyEncryption() call

         if not oldUsedEncryption==newUsesEncryption:
            # If there was an encryption change, we must change the flags
            # in the wallet file in the same atomic operation as changing
            # the stored keys.  We can't let them get out of sync.
            self.useEncryption = newUsesEncryption
            walletUpdateInfo.append(self.createChangeFlagsEntry())
            self.useEncryption = oldUsedEncryption
            # Restore the old flag just in case the file write fails

         newAddrMap  = {}
         i=1
         nAddr = len(self.addrMap)
         
         for addr160,addr in self.addrMap.items():
            Progress(i, nAddr)
            i = i +1

            newAddrMap[addr160] = addr.copy()
            newAddrMap[addr160].enableKeyEncryption(generateIVIfNecessary=True)
            newAddrMap[addr160].changeEncryptionKey(oldKdfKey, newKdfKey)
            newAddrMap[addr160].walletByteLoc = addr.walletByteLoc
            walletUpdateInfo.append( \
               [WLT_UPDATE_MODIFY, addr.walletByteLoc, newAddrMap[addr160].serialize()])


         # Try to update the wallet file with the new encrypted key data
         updateSuccess = self.walletFileSafeUpdate( walletUpdateInfo )

         if updateSuccess:
            # Finally give the new data to the user
            for addr160,addr in newAddrMap.items():
               self.addrMap[addr160] = addr.copy()

         self.useEncryption = newUsesEncryption
         if newKdfKey:
            self.lock() 
            self.unlock(newKdfKey, Progress=Progress)

      finally:
         # Make sure we always destroy the temporary passphrase results
         if newKdfKey: newKdfKey.destroy()
         if oldKdfKey: oldKdfKey.destroy()

   #############################################################################
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

   #############################################################################
   def getDisplayStr(self, pref="Wallet: "):
      return '%s"%s" (%s)' % (pref, self.labelName, self.walletId)

   #############################################################################
   def getCommentForAddress(self, addr160):
      try:
         assetIndex = self.cppWallet.getAssetIndexForAddr(addr160)
         hashList = self.cppWallet.getScriptHashVectorForIndex(assetIndex)
      except:
         return ''

      for _hash in hashList:
         if _hash in self.commentsMap:
            return self.commentsMap[_hash]

      return ''

   #############################################################################
   def getComment(self, hashVal):
      """
      This method is used for both address comments, as well as tx comments
      In the first case, use the 20-byte binary pubkeyhash.  Use 32-byte tx
      hash for the tx-comment case.
      """
      if hashVal in self.commentsMap:
         return self.commentsMap[hashVal]
      else:
         return ''

   #############################################################################
   def setComment(self, hashVal, newComment):
      """
      This method is used for both address comments, as well as tx comments
      In the first case, use the 20-byte binary pubkeyhash.  Use 32-byte tx
      hash for the tx-comment case.
      """

      self.commentsMap[hashVal] = newComment
      self.bridgeWalletObj.setComment(hashVal, newComment)

   #############################################################################
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
                  #LOGERROR("Unrecognized scraddr: " + binary_to_hex(scrAddr))

      addrComments = []
      for a160 in self.txAddrMap[txHash]:
         h160 = a160[1:]
         if h160 in self.commentsMap and '[[' not in self.commentsMap[h160]:
            addrComments.append(self.commentsMap[h160])

      return '; '.join(addrComments)

   #############################################################################
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

   #############################################################################
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

   #############################################################################
   def setLabels(self, lshort, llong=''):
      self.labelName = lshort
      self.labelDescr = llong

      self.bridgeWalletObj.setLabels(self.labelName, self.labelDescr)

   #############################################################################
   def deleteImportedAddress(self, addr160):
      """
      We want to overwrite a particular key in the wallet.  Before overwriting
      the data looks like this:
         [  \x00  |  <20-byte addr160>  |  <237-byte keydata> ]
      And we want it to look like:
         [  \x04  |  <2-byte length>  | \x00\x00\x00... ]
      So we need to construct a wallet-update vector to modify the data
      starting at the first byte, replace it with 0x04, specifies how many
      bytes are in the deleted entry, and then actually overwrite those 
      bytes with 0s
      """

      if not self.addrMap[addr160].chainIndex==-2:
         raise WalletAddressError('You can only delete imported addresses!')

      overwriteLoc = self.addrMap[addr160].walletByteLoc - 21
      overwriteLen = 20 + self.pybtcaddrSize - 2

      overwriteBin = ''
      overwriteBin += int_to_binary(WLT_DATATYPE_DELETED, widthBytes=1)
      overwriteBin += int_to_binary(overwriteLen,         widthBytes=2)
      overwriteBin += '\x00'*overwriteLen

      self.walletFileSafeUpdate([[WLT_UPDATE_MODIFY, overwriteLoc, overwriteBin]])

      # IMPORTANT:  we need to update the wallet structures to reflect the
      #             new state of the wallet.  This will actually be easiest
      #             if we just "forget" the current wallet state and re-read
      #             the wallet from file
      wltPath = self.walletPath
      passCppWallet = self.cppWallet
      self.cppWallet.removeAddressBulk([Hash160ToScrAddr(addr160)])
      self.readWalletFile(wltPath)
      self.cppWallet = passCppWallet
      self.register(False)

   #############################################################################  
   def importExternalAddressBatch(self, privKeyList):
      addr160List = []
      for key, a160 in privKeyList:
         self.importExternalAddressData(key)
         addr160List.append(Hash160ToScrAddr(a160))
      return addr160List

   #############################################################################
   def getAddrListSortedByChainIndex(self, withRoot=False):
      """ Returns Addr160 list """
      addrList = []
      for addr160 in self.linearAddr160List:
         addr=self.addrMap[addr160]
         addrList.append( [addr.chainIndex, addr160, addr] )

      addrList.sort(key=lambda x: x[0])
      return addrList

   #############################################################################
   def getAddrList(self):
      """ Returns list of PyBtcAddress objects """
      addrList = []
      for addr160,addrObj in self.addrMap.items():
         if addr160=='ROOT':
            continue
         # I assume these will be references, not copies
         addrList.append( addrObj )
      return addrList

   #############################################################################
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
            if (withAddrPool or addr.chainIndex<=self.highestUsedChainIndex):
               addrList.append(addr)

      return addrList

   #############################################################################
   def getAddress160ByChainIndex(self, desiredIdx):
      """
      It should be safe to assume that if the index is less than the highest 
      computed, it will be in the chainIndexMap, but I don't like making such
      assumptions.  Perhaps something went wrong with the wallet, or it was
      manually reconstructed and has holes in the chain.  We will regenerate
      addresses up to that point, if necessary (but nothing past the value
      self.lastComputedChainIndex.
      """
      if desiredIdx>self.lastComputedChainIndex or desiredIdx<0:
         # I removed the option for fillPoolIfNecessary, because of the risk
         # that a bug may lead to generation of billions of addresses, which
         # would saturate the system's resources and fill the HDD.
         raise WalletAddressError('Chain index is out of range')

      if desiredIdx in self.chainIndexMap:
         return self.chainIndexMap[desiredIdx]
      else:
         # Somehow the address isn't here, even though it is less than the
         # last computed index
         closestIdx = 0
         for idx,addr160 in self.chainIndexMap.items():
            if closestIdx<idx<=desiredIdx:
               closestIdx = idx
               
         gap = desiredIdx - closestIdx
         extend160 = self.chainIndexMap[closestIdx]
         for i in range(gap+1):
            extend160 = self.computeNextAddress(extend160)
            if desiredIdx==self.addrMap[extend160].chainIndex:
               return self.chainIndexMap[desiredIdx]

   #############################################################################
   def pprint(self, indent='', allAddrInfo=True):
      raise NotImplementedError("deprecated")
      print(indent + 'PyBtcWallet  :', self.uniqueIDB58)
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

   #############################################################################
   def isEqualTo(self, wlt2, debug=False):
      isEqualTo = True
      isEqualTo = isEqualTo and (self.uniqueIDB58 == wlt2.uniqueIDB58)
      isEqualTo = isEqualTo and (self.labelName  == wlt2.labelName )
      isEqualTo = isEqualTo and (self.labelDescr == wlt2.labelDescr)
      try:

         rootstr1 = binary_to_hex(self.addrMap['ROOT'].serialize())
         rootstr2 = binary_to_hex(wlt2.addrMap['ROOT'].serialize())
         isEqualTo = isEqualTo and (rootstr1 == rootstr2)
         if debug:
            print('')
            print('RootAddrSelf:')
            print(prettyHex(rootstr1, indent=' '*5))
            print('RootAddrWlt2:')
            print(prettyHex(rootstr2, indent=' '*5))
            print('RootAddrDiff:', end=' ')
            pprintDiff(rootstr1, rootstr2, indent=' '*5)

         for addr160 in self.addrMap.keys():
            addrstr1 = binary_to_hex(self.addrMap[addr160].serialize())
            addrstr2 = binary_to_hex(wlt2.addrMap[addr160].serialize())
            isEqualTo = isEqualTo and (addrstr1 == addrstr2)
            if debug:
               print('')
               print('AddrSelf:', binary_to_hex(addr160), end=' ')
               print(prettyHex(binary_to_hex(self.addrMap['ROOT'].serialize()), indent='     '))
               print('AddrSelf:', binary_to_hex(addr160), end=' ')
               print(prettyHex(binary_to_hex(wlt2.addrMap['ROOT'].serialize()), indent='     '))
               print('AddrDiff:', end=' ')
               pprintDiff(addrstr1, addrstr2, indent=' '*5)
      except:
         return False

      return isEqualTo

   #############################################################################
   def toJSONMap(self):
      outjson = {}
      outjson['name']             = self.labelName
      outjson['description']      = self.labelDescr
      outjson['walletversion']    = getVersionString(PYBTCWALLET_VERSION)
      outjson['balance']          = AmountToJSON(self.getBalance('Spend'))
      outjson['keypoolsize']      = self.addrPoolSize
      outjson['numaddrgen']       = len(self.addrMap)
      outjson['highestusedindex'] = self.highestUsedChainIndex
      outjson['watchingonly']     = self.watchingOnly
      outjson['createdate']       = self.wltCreateDate
      outjson['walletid']         = self.uniqueIDB58
      outjson['isencrypted']      = self.useEncryption
      outjson['islocked']         = self.isLocked if self.useEncryption else False
      outjson['keylifetime']      = self.defaultKeyLifetime

      return outjson

   #############################################################################
   def fromJSONMap(self, jsonMap, skipMagicCheck=False):
      self.labelName   = jsonMap['name']
      self.labelDescr  = jsonMap['description']
      self.addrPoolSize  = jsonMap['keypoolsize']
      self.highestUsedChainIndex  = jsonMap['highestusedindex']
      self.watchingOnly  = jsonMap['watchingonly']
      self.wltCreateDate  = jsonMap['createdate']
      self.uniqueIDB58  = jsonMap['walletid']
      jsonVer = hex_to_binary(jsonMap['walletversion'])

      # Issue a warning if the versions don't match
      if not jsonVer == getVersionString(PYBTCWALLET_VERSION):
         LOGWARN('Unserializing wallet of different version')
         LOGWARN('   Wallet Version: %d' % jsonVer)
         LOGWARN('   Armory Version: %d' % UNSIGNED_TX_VERSION)

   ###############################################################################
   def getAddrTotalTxnCount(self, addrHash):
      try:
         addrObj = self.addrMap[addrHash]
         return addrObj.getTxioCount()
      except:
         return 0

   ###############################################################################
   def getAddrDataFromDB(self):
      result = self.bridgeWalletObj.getAddrCombinedList()

      #update addr map
      for addrProto in result.updatedAssets:
         addrObj = PyBtcAddress()
         addrObj.loadFromProto(addrProto)

         addrHash = addrObj.getPrefixedAddr()
         self.addrMap[addrHash] = addrObj

      #update balances and txio count
      for addrCombinedData in result.balances:
         addr = addrCombinedData.scrAddr
         if addr in self.addrMap:
            addrObj = self.addrMap[addr]

            addrObj.fullBalance        = addrCombinedData.balances.full
            addrObj.spendableBalance   = addrCombinedData.balances.spendable
            addrObj.unconfirmedBalance = addrCombinedData.balances.unconfirmed
            addrObj.txioCount          = addrCombinedData.balances.txnCount
         else:
            print ("[getAddrDataFromDB] missing address " + addr.hex())

   ###############################################################################
   def getHistoryAsCSV(self, currentTop):
      file = open('%s.csv' % self.walletPath, 'wb')

      sortedAddrList = self.getAddrListSortedByChainIndex()
      chainCode = sortedAddrList[0][2].chaincode.toHexStr()

      bal = self.getBalance('full')
      bal = bal  / float(100000000)
      file.write("%s,%f,%s,#%d\n" % (self.uniqueIDB58, bal, chainCode, currentTop))


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

   ###############################################################################
   @CheckWalletRegistration
   def getHistoryPage(self, pageID):
      try:
         return self.cppWallet.getHistoryPage(pageID)
      except:
         raise Exception('pageID is out of range')

   ###############################################################################
   @CheckWalletRegistration
   def doAfterScan(self):
      actionsList = self.actionsToTakeAfterScan
      self.actionsToTakeAfterScan = []

      for calls in actionsList:
         calls[0](*calls[1])

   ###############################################################################
   @CheckWalletRegistration
   def sweepAfterRescan(self, addrList, main): 
      #get a new address from the wallet to sweep the funds to
      sweepToAddr = self.getNextUnusedAddress().getAddr160()

      main.finishSweepScan(self, addrList, sweepToAddr)
      return

   ###############################################################################
   @CheckWalletRegistration
   def sweepAddressList(self, addrList, main):
      self.actionsToTakeAfterScan.append([self.sweepAfterRescan, [addrList, main]])    

      addrVec = []
      for addr in addrList:
         addrVec.append(ADDRBYTE + addr.getAddr160())

      _id = Cpp.SecureBinaryData().GenerateRandom(8).toHexStr()
      main.oneTimeScanAction[_id] = self.doAfterScan()
      TheBDM.bdv().registerAddrList(_id, addrList)

   ###############################################################################
   @CheckWalletRegistration
   def disableWalletUI(self):
      self.isEnabled = False

   ###############################################################################
   @CheckWalletRegistration
   def getLedgerEntryForTxHash(self, txHash):
      return self.cppWallet.getLedgerEntryForTxHash(txHash)

   ###############################################################################
   def getAddrObjectForHash(self, hashVal):
      assetIndex = self.cppWallet.getAssetIndexForAddr(hashVal)
      if assetIndex == 2**32:
         raise("unknown hash")

      try:
         addr160 = self.chainIndexMap[assetIndex]
      except:
         if assetIndex < -2:
            importIndex = self.cppWallet.convertToImportIndex(assetIndex)
            addr160 = self.linearAddr160List[importIndex]
         else:
            raise Exception("invalid address index")

      return self.addrMap[addr160]

   ###############################################################################
   def returnFilteredAddrList(self, filterUse, filterType):
      addrList = []
      keepInUse = bool(filterUse != "Unused")
      keepChange = bool(filterUse == "Change")

      for addr in self.linearAddr160List:
         addrObj = self.addrMap[addr]
         if addrObj.filter(filterType, keepInUse, keepChange) == True:
            addrList.append(addrObj)

      return addrList

   ###############################################################################
   def getFilteredAddrCount(self, filterUse, filterType = None):
      count = 0
      keepInUse = bool(filterUse != "Unused")
      keepChange = bool(filterUse == "Change")

      for addr in self.linearAddr160List:
         addrObj = self.addrMap[addr]
         if addrObj.filter(filterType, keepInUse, keepChange) == True:
            count += 1

      return count

   ###############################################################################
   def getAddrByIndex(self, index):
      if index > -2:
         addr160 = self.chainIndexMap[index]
         return self.addrMap[addr160]
      else:
         importIndex = self.cppWallet.convertFromImportIndex(index)
         addr160 = self.linearAddr160List[importIndex]
         return self.addrMap[addr160]

   ###############################################################################
   def getImportCppAddrList(self):
      addrList = []
      for addrIndex in self.importList:

         addrObj = self.cppWallet.getImportAddrObjByIndex(addrIndex)
         addrComment = self.getCommentForAddress(addrObj.getAddrHash()[1:])
         addrObj.setComment(addrComment)

         addrList.append(addrObj)

      return addrList

   ###############################################################################
   def hasImports(self):
      return len(self.importList) != 0

   ###############################################################################
   def loadFromProto(self, payload):
      self._walletId    = payload.walletId
      self._accountId   = payload.accountId
      self._dbId        = payload.dbId
      self._settingsId  = computeSettingsId(self._walletId, self._accountId)

      self.labelName   = payload.label
      self.labelDescr  = payload.desc

      self.useEncryption = payload.usesEncryption
      self.lastComputedChainIndex = payload.lookupCount
      self.highestUsedChainIndex = payload.useCount
      self.watchingOnly = payload.watchingOnly
      self.addressTypes = payload.addressTypes
      self.defaultAddressType = payload.defaultAddressType
      self.kdfMemoryReq = payload.kdfMemReq

      #addrMap and chainIndexMap
      for addr in payload.addressData:
         addrObj = PyBtcAddress(self)
         addrObj.loadFromProto(addr)
         self.addAddress(addrObj)

      #importList
      for addr160, addrObj in self.addrMap.items():
         if addrObj.chainIndex <= -2:
            self.importList.append(len(self.linearAddr160List) - 1)

      #comments
      for commentIt in payload.comments:
         self.commentsMap[commentIt.key] = commentIt.val

   #############################################################################
   def fillAddressPool(self, numPool, progressId, callback=None):
      """
      Usually, when we fill the address pool, we are generating addresses
      for the first time, and thus there is no chance it's ever seen the
      blockchain.  However, this method is also used for recovery/import
      of wallets, where the address pool has addresses that probably have
      transactions already in the blockchain.
      """

      def completeProcess(*args):
         self.loadFromProto(*args)
         if callback:
            callback()

      self.bridgeWalletObj.extendAddressPool(
         progressId, numPool, completeProcess)

   #############################################################################
   def register(self, isNew):
      TheBridge.service.registerWallet(self.uniqueIDB58, isNew)

   #############################################################################
   def createBackupString(self, unlockHandler, callback):
      def callbackHandler(callbackFunc, reply):
         callbackFunc(reply)

      return self.bridgeWalletObj.createBackupStringForWallet(
         unlockHandler, callbackHandler, [callback])

   #############################################################################
   def getAddressTypes(self):
      return self.addressTypes

   #############################################################################
   def getDefaultAddressType(self):
      return self.defaultAddressType

   def getDefaultChangeType(self):
      #TODO: add entry in cpp wallets for default change type
      return self.defaultChangeType

   #############################################################################
   def setAddressTypeFor(self, addrObj, addrType):
      if addrObj.addrType == addrType:
         return

      if addrType not in self.getAddressTypes():
         raise Exception(\
            "[PyBtcWallet::setAddressTypeFor] inneligible address type")

      protoAddr = self.bridgeWalletObj.setAddressTypeFor(
         addrObj.assetId, addrType)
      addrObj.loadFromProto(protoAddr)

   #############################################################################
   def initCoinSelectionInstance(self, height):
      return self.bridgeWalletObj.initCoinSelectionInstance(height)

   def createAddressBook(self):
      return self.bridgeWalletObj.createAddressBook()

   def getLedgerDelegateIdForScrAddr(self, scrAddr):
      return self.bridgeWalletObj.getLedgerDelegateIdForScrAddr(scrAddr)

   def getKdfMemoryReqtBytes(self):
      return self.kdfMemoryReq

   #############################################################################
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

   #############################################################################
   ## settings ##
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
