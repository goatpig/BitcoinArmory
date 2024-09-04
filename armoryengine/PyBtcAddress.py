from __future__ import (absolute_import, division,
                        print_function, unicode_literals)
################################################################################
#                                                                              #
# Copyright (C) 2011-2015, Armory Technologies, Inc.                           #
# Distributed under the GNU Affero General Public License (AGPL v3)            #
# See LICENSE or http://www.gnu.org/licenses/agpl.html                         #
#                                                                              #
################################################################################
from armoryengine.ArmoryUtils import ADDRBYTE, hash256, \
   KeyDataError, RightNow, LOGERROR, ChecksumError, convertKeyDataToAddress, \
   verifyChecksum, WalletLockError, createDERSigFromRS, binary_to_int, \
   computeChecksum, getVersionInt, PYBTCWALLET_VERSION, bitset_to_int, \
   LOGDEBUG, int_to_bitset, UnserializeError, int_to_binary, BIGENDIAN, \
   checkAddrStrValid, binary_to_hex, ENABLE_DETSIGN
from armoryengine.AddressUtils import AddressEntryType_Default
from armoryengine.BinaryPacker import BinaryPacker, UINT8, UINT16, UINT32, \
   UINT64, INT8, INT16, INT32, INT64, VAR_INT, VAR_STR, FLOAT, BINARY_CHUNK
from armoryengine.BinaryUnpacker import BinaryUnpacker
from armoryengine.Timer import TimeThisFunction

################################################################################
class PyBtcAddress(object):
   """
   PyBtcAddress --

   This class encapsulated EVERY kind of address object:
      -- Plaintext private-key-bearing addresses
      -- Encrypted private key addresses, with AES locking and unlocking
      -- Watching-only public-key addresses
      -- Address-only storage, representing someone else's key
      -- Deterministic address generation from previous addresses
      -- Serialization and unserialization of key data under all conditions
      -- Checksums on all serialized fields to protect against HDD byte errors

      For deterministic wallets, new addresses will be created from a chaincode
      and the previous address.  What is implemented here is a special kind of
      deterministic calculation that actually allows the user to securely
      generate new addresses even if they don't have the private key.  This
      method uses Diffie-Hellman shared-secret calculations to produce the new
      keys, and has the same level of security as all other ECDSA operations.
      There's a lot of fantastic benefits to doing this:

         (1) If all addresses in wallet are chained, then you only need to backup
             your wallet ONCE -- when you first create it.  Print it out, put it
             in a safety-deposit box, or tattoo the generator key to the inside
             of your eyelid:  it will never change.

         (2) You can keep your private keys on an offline machine, and keep a
             watching-only wallet online.  You will be able to generate new
             keys/addresses, and verify incoming transactions, without ever
             requiring your private key to touch the internet.

         (3) If your friend has the chaincode and your first public key, they
             too can generate new addresses for you -- allowing them to send
             you money multiple times, with different addresses, without ever
             needing to specifically request the addresses.
             (the downside to this is if the chaincode is compromised, all
             chained addresses become de-anonymized -- but is only a loss of
             privacy, not security)

      However, we do require some fairly complicated logic, due to the fact
      that a user with a full, private-key-bearing wallet, may try to generate
      a new key/address without supplying a passphrase.  If this happens, the
      wallet logic gets very complicated -- we don't want to reject the request
      to generate a new address, but we can't compute the private key until the
      next time the user unlocks their wallet.  Thus, we have to save off the
      data they will need to create the key, to be applied on next unlock.
   """

   #############################################################################
   def __init__(self, parentWallet=None):
      """
      We use SecureBinaryData objects to store pub, priv and IV objects,
      because that is what is required by the C++ code.  See EncryptionUtils.h
      to see that available methods.
      """
      self.prefixedHash          = []
      self.binPublicKey          = []  #33 or 65 bytes depending on address type
      self.precursorScript       = []
      self.isInitialized         = False
      self.chainIndex            = 0
      self.useEncryption         = False
      self.hasPrivKey            = False
      self.addrType              = AddressEntryType_Default
      self.parentWallet          = parentWallet
      self.addressString         = None
      
      self.fullBalance           = 0
      self.spendableBalance      = 0
      self.unconfirmedBalance    = 0
      self.txioCount             = 0

      self.isUsed = False
      self.isChange = False

   #############################################################################
   def hasPubKey(self):
      return (len(self.binPublicKey) != 0)

   ##############################################################################
   def getPubKey(self):
      '''Return the public key of the address.'''
      if len(self.binPublicKey) == 0:
         raise KeyDataError('PyBtcAddress does not have a public key!')
      return self.binPublicKey

   #############################################################################
   def getAddressString(self):
      return self.addressString

   #############################################################################
   def getAddr160(self):
      if len(self.prefixedHash)!=21:
         raise KeyDataError('PyBtcAddress does not have an address string!')
      return self.prefixedHash[1:]

   #############################################################################
   def getPrefixedAddr(self):
      if len(self.prefixedHash)!=21:
         raise KeyDataError('PyBtcAddress does not have an address string!')
      return self.prefixedHash
     
   #############################################################################
   def getPrecursorScript(self):
      if len(self.precursorScript) == 0:
         raise KeyDataError('PyBtcAddress does not have a precursor!')
      return self.precursorScript

   #############################################################################
   def isCompressed(self):
      # Armory wallets (v1.35) do not support compressed keys
      return False 

   #############################################################################
   def markAsRootAddr(self):
      self.chainIndex = -1

   #############################################################################
   def isAddrChainRoot(self):
      return (self.chainIndex==-1)

   #############################################################################
   def loadFromProto(self, payload):
      self.__init__()

      self.prefixedHash = payload.prefixedHash
      self.binPublicKey = payload.publicKey
      self.chainIndex = payload.index
      self.assetId = payload.assetId
      self.isInitialized = True
      self.addrType = payload.addrType
      self.addressString = payload.addressString
      self.hasPrivKey = payload.hasPrivKey
      self.use_encryption = payload.usesEncryption

      self.precursorScript = payload.precursorScript
      self.isUsed = payload.isUsed
      self.isChange = payload.isChange

   #############################################################################
   def getTxioCount(self):
      from armoryengine.BDM import TheBDM, BDM_OFFLINE, BDM_UNINITIALIZED
      if TheBDM.getState() in (BDM_OFFLINE,BDM_UNINITIALIZED):
         return "N/A"

      return self.txioCount

   #############################################################################
   def getFullBalance(self):
      return self.fullBalance

   #############################################################################
   def getSpendableBalance(self):
      return self.spendableBalance

   #############################################################################
   def getUnconfirmedBalance(self):
      return self.unconfirmedBalance

   #############################################################################
   def getComment(self):
      if self.parentWallet is None:
         return ''

      return self.parentWallet.getCommentForAddr(self.getAddr160())

   #############################################################################
   def filter(self, filterType, isUsed, isChange):
      if self.chainIndex < 0:
         return False

      if filterType != None and filterType != self.addrType:
         return False

      if isUsed != self.isUsed:
         return False

      if isChange != self.isChange:
         return False

      return True