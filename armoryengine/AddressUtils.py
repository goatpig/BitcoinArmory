##############################################################################
#                                                                            #
# Copyright (C) 2011-2015, Armory Technologies, Inc.                         #
# Distributed under the GNU Affero General Public License (AGPL v3)          #
# See LICENSE or http://www.gnu.org/licenses/agpl.html                       #
#                                                                            #
# Copyright (C) 2016-2023, goatpig                                           #
#  Distributed under the MIT license                                         #
#  See LICENSE-MIT or https://opensource.org/licenses/MIT                    #
#                                                                            #
##############################################################################
import logging
import traceback
import binascii

from armoryengine.BinaryPacker import BinaryPacker, UINT8, BINARY_CHUNK
from armoryengine.ArmoryUtils import DATATYPE, ADDRBYTE, P2SHBYTE, \
   binary_to_hex, prettyHex, hash256, hash160, SCRADDR_BYTE_LIST, \
   ADDRBYTE, P2SHBYTE, LOGERROR


################################################################################
BASE58CHARS  = '123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz'
DEFAULT_RAWDATA_LOGLEVEL  = logging.DEBUG
HASH160PREFIX     = '\x00'

#address types, copied from cppForSwig/Wallets/Addresses.h
AddressEntryType_Default = 0
AddressEntryType_P2PKH = 1
AddressEntryType_P2PK = 2
AddressEntryType_P2WPKH = 3
AddressEntryType_Multisig = 4
AddressEntryType_Uncompressed = 0x10000000
AddressEntryType_P2SH = 0x40000000
AddressEntryType_P2WSH = 0x80000000

####
class BadAddressError(Exception): pass
class NonBase58CharacterError(Exception): pass

################################################################################
def CheckHash160(scrAddr):
   if not len(scrAddr)==21:
      raise BadAddressError("Supplied scrAddr is not a Hash160 value!")
   if scrAddr[0] != int.from_bytes(ADDRBYTE, "little") and \
      scrAddr[0] != int.from_bytes(P2SHBYTE, "little"):
      raise BadAddressError("Supplied scrAddr is not a Hash160 value!")
   return scrAddr[1:]

####
def Hash160ToScrAddr(a160):
   if not len(a160)==20:
      LOGERROR('Invalid hash160 value!')
   return HASH160PREFIX + a160

################################################################################
# BINARY/BASE58 CONVERSIONS
def binary_to_base58(binstr):
   """
   This method applies the Bitcoin-specific conversion from binary to Base58
   which may includes some extra "zero" bytes, such as is the case with the
   main-network addresses.

   This method is labeled as outputting an "addrStr", but it's really this
   special kind of Base58 converter, which makes it usable for encoding other
   data, such as ECDSA keys or scripts.
   """
   padding = 0
   for b in binstr:
      if b==b'\x00':
         padding+=1
      else:
         break

   n = 0
   for ch in binstr:
      n *= 256
      n += ch

   b58 = ''
   while n > 0:
      n, r = divmod (n, 58)
      b58 = BASE58CHARS[r] + b58
   return '1'*padding + b58

####
def base58_to_binary(s):
   """
   This method applies the Bitcoin-specific conversion from Base58 to binary
   which may includes some extra "zero" bytes, such as is the case with the
   main-network addresses.

   This method is labeled as inputting an "addrStr", but it's really this
   special kind of Base58 converter, which makes it usable for encoding other
   data, such as ECDSA keys or scripts.
   """

   if not s:
      return b''

   # Convert the string to an integer
   n = 0
   for c in s:
      n *= 58
      if c not in BASE58CHARS:
         raise NonBase58CharacterError('Character %r is not a valid base58 character' % c)
      digit = BASE58CHARS.index(c)
      n += digit

   # Convert the integer to bytes
   h = '%x' % n
   if len(h) % 2:
      h = '0' + h
   res = bytes(binascii.unhexlify(h.encode('utf8')))

   # Add padding back.
   pad = 0
   for c in s[:-1]:
      if c == BASE58CHARS[0]: pad += 1
      else: break
   return b'\x00' * pad + res

####
def encodePrivKeyBase58(privKeyBin):
   bin33 = PRIVKEYBYTE + privKeyBin
   chk = computeChecksum(bin33)
   return binary_to_base58(bin33 + chk)

################################################################################
def hash160_to_addrStr(binStr, netbyte=ADDRBYTE):
   """
   Converts the 20-byte pubKeyHash to 25-byte binary Bitcoin address
   which includes the network byte (prefix) and 4-byte checksum (suffix)
   """

   if not len(binStr) == 20:
      raise InvalidHashError('Input string is %d bytes' % len(binStr))

   packer = BinaryPacker()
   packer.put(BINARY_CHUNK, netbyte)
   packer.put(BINARY_CHUNK, binStr)
   hash21 = hash256(packer.getBinaryString())
   packer.put(BINARY_CHUNK, hash21[:4])
   return binary_to_base58(packer.getBinaryString())

################################################################################
def hash160_to_p2shAddrStr(binStr):
   if not len(binStr) == 20:
      raise InvalidHashError('Input string is %d bytes' % len(binStr))

   packer = BinaryPacker()
   packer.put(BINARY_CHUNK, P2SHBYTE)
   packer.put(BINARY_CHUNK, binStr)
   hash21 = hash256(packer.getBinaryString())
   packer.put(BINARY_CHUNK, hash21[:4])
   return binary_to_base58(packer.getBinaryString())

################################################################################
def binScript_to_p2shAddrStr(binScript):
   return hash160_to_p2shAddrStr(hash160(binScript))

################################################################################
def addrStr_is_p2sh(b58Str):
   if len(b58Str)==0:
      return False

   if sum([(0 if c in BASE58CHARS else 1) for c in b58Str]) > 0:
      return False

   binStr = base58_to_binary(b58Str)
   if not len(binStr)==25:
      return False

   if not hash256(binStr[:21])[:4] == binStr[-4:]:
      return False

   return (binStr[0] == P2SHBYTE)

################################################################################
# As of version 0.90.1, this returns the prefix byte with the hash160.  This is
# because we need to handle/distinguish regular addresses from P2SH.  All code
# using this method must be updated to expect 2 outputs and check the prefix.
def addrStr_to_hash160(b58Str, p2shAllowed=True, \
                       addrByte = ADDRBYTE, p2shByte = P2SHBYTE):
   binStr = base58_to_binary(b58Str)
   
   addrByteInt = int.from_bytes(addrByte, "little")
   p2shByteInt = int.from_bytes(p2shByte, "little")

   if not p2shAllowed and binStr[0]==addrByteInt:
      raise P2SHNotSupportedError
   if not len(binStr) == 25:
      raise BadAddressError('Address string is %d bytes' % len(binStr))

   if not hash256(binStr[:21])[:4] == binStr[-4:]:
      raise ChecksumError('Address string has invalid checksum')

   if not binStr[0] in (addrByteInt, p2shByteInt):
      raise BadAddressError('Unknown addr prefix: %s' % binary_to_hex(binStr[0]))

   return (binStr[0], binStr[1:-4])

################################################################################
def isLikelyDataType(theStr, dtype=None):
   """
   This really shouldn't be used on short strings.  Hence
   why it's called "likely" datatype...
   """
   ret = None
   try:
      hexCount = sum([1 if c in BASE16CHARS else 0 for c in theStr])
   except:
      return DATATYPE.Binary
   b58Count = sum([1 if c in BASE58CHARS else 0 for c in theStr])
   canBeHex = hexCount==len(theStr)
   canBeB58 = b58Count==len(theStr)
   if canBeHex:
      ret = DATATYPE.Hex
   elif canBeB58 and not canBeHex:
      ret = DATATYPE.Base58
   else:
      ret = DATATYPE.Binary

   if dtype==None:
      return ret
   else:
      return dtype==ret

################################################################################
def scrAddr_to_script(scraddr):
   """
   Convert a scrAddr string (used by BDM) to the correct TxOut script
   Note this only works for P2PKH and P2SH scraddrs.  Multi-sig and
   all non-standard scripts cannot be derived from scrAddrs.  In a way,
   a scrAddr is intended to be an intelligent "hash" of the script,
   and it's a perk that most of the time we can reverse it to get the script.
   """
   if len(scraddr)==0:
      raise BadAddressError('Empty scraddr')

   prefix = scraddr[0]
   if not prefix in SCRADDR_BYTE_LIST or not len(scraddr)==21:
      LOGERROR('Bad scraddr: "%s"' % binary_to_hex(scraddr))
      raise BadAddressError('Invalid ScrAddress')

   if prefix==ADDRBYTE:
      return hash160_to_p2pkhash_script(scraddr[1:])
   elif prefix==P2SHBYTE:
      return hash160_to_p2sh_script(scraddr[1:])
   else:
      LOGERROR('Unsupported scraddr type: "%s"' % binary_to_hex(scraddr))
      raise BadAddressError('Can only convert P2PKH and P2SH scripts')

################################################################################
def script_to_scrAddr(binScript):
   """ Convert a binary script to scrAddr string (used by BDM) """
   from armoryengine.CppBridge import TheBridge
   return TheBridge.scriptUtils.getScrAddrForScript(binScript)

################################################################################
def script_to_addrStr(binScript):
   """ Convert a binary script to scrAddr string (used by BDM) """
   return scrAddr_to_addrStr(script_to_scrAddr(binScript))

################################################################################
def scrAddr_to_addrStr(scrAddr):
   from armoryengine.CppBridge import TheBridge
   return TheBridge.scriptUtils.getAddrStrForScrAddr(scrAddr)

################################################################################
# We beat around the bush here, to make sure it goes through addrStr which
# triggers errors if this isn't a regular addr or P2SH addr
def scrAddr_to_hash160(scrAddr):
   addr = scrAddr_to_addrStr(scrAddr)
   atype, a160 = addrStr_to_hash160(addr)
   return (atype, a160)

################################################################################
def addrStr_to_scrAddr(addrStr, p2pkhByte = ADDRBYTE, p2shByte = P2SHBYTE):
   if addrStr == '':
      return ''

   if not checkAddrStrValid(addrStr, [p2pkhByte, p2shByte]):
      BadAddressError('Invalid address: "%s"' % addrStr)

   atype, a160 = addrStr_to_hash160(addrStr, True, p2pkhByte, p2shByte)
   if atype==int.from_bytes(p2pkhByte, "little"):
      return p2pkhByte + a160
   elif atype==int.from_bytes(p2shByte, "little"):
      return p2shByte + a160
   else:
      BadAddressError('Invalid address: "%s"' % addrStr)

################################################################################
# output script type to address type resolution
def getAddressTypeForOutputType(scriptType):
   if scriptType == CPP_TXOUT_STDHASH160:
      return AddressEntryType_P2PKH

   elif scriptType == CPP_TXOUT_STDPUBKEY33:
      return AddressEntryType_P2PK

   elif scriptType == CPP_TXOUT_STDPUBKEY65:
      return AddressEntryType_P2PK + AddressEntryType_Uncompressed

   elif scriptType == CPP_TXOUT_MULTISIG:
      return AddressEntryType_Multisig

   elif scriptType == CPP_TXOUT_P2SH:
      return AddressEntryType_P2SH

   elif scriptType == CPP_TXOUT_P2WPKH:
      return AddressEntryType_P2WPKH

   elif scriptType == CPP_TXOUT_P2WSH:
      return AddressEntryType_P2WSH

   raise Exception("unknown address type")

################################################################################
def addrTypeInSet(addrType, addrTypeSet):
   if addrType in addrTypeSet:
      return True

   def nestedSearch(nestedType):
      if not (addrType & nestedType):
         return False

      for aType in addrTypeSet:
         if aType & nestedType:
            return True
      return False

   #couldn't find an exact address type match, try to filter by nested types
   if nestedSearch(AddressEntryType_P2SH):
      return True

   if nestedSearch(AddressEntryType_P2WSH):
      return True

   return False

################################################################################
def ComputeFragIDBase58(M, wltIDBin):
   mBin4   = int_to_binary(M, widthBytes=4, endOut=BIGENDIAN)
   fragBin = hash256(wltIDBin + mBin4)[:4]
   fragB58 = str(M) + binary_to_base58(fragBin)
   return fragB58

################################################################################
def ComputeFragIDLineHex(M, index, wltIDBin, isSecure=False, addSpaces=False):
   fragID  = int_to_hex((128+M) if isSecure else M)
   fragID += int_to_hex(index+1)
   fragID += binary_to_hex(wltIDBin)

   if addSpaces:
      fragID = ' '.join([fragID[i*4:(i+1)*4] for i in range(4)])

   return fragID

################################################################################
def ReadFragIDLineBin(binLine):
   doMask = binary_to_int(binLine[0]) > 127
   M      = binary_to_int(binLine[0]) & 0x7f
   fnum   = binary_to_int(binLine[1])
   fragID  = binLine[2:]

   idBase58 = ComputeFragIDBase58(M, fragID) + '-#' + str(fnum)
   return (M, fnum, fragID, doMask, idBase58)

################################################################################
def ReadFragIDLineHex(hexLine):
   return ReadFragIDLineBin( hex_to_binary(hexLine.strip().replace(' ','')))

################################################################################
# For super-debug mode, we'll write out raw data
def LOGRAWDATA(rawStr, loglevel=DEFAULT_RAWDATA_LOGLEVEL):
   dtype = isLikelyDataType(rawStr)
   stkOneUp = traceback.extract_stack()[-2]
   filename,method = stkOneUp[0], stkOneUp[1]
   methodStr  = '(PPRINT from %s:%d)\n' % (filename,method)
   pstr = rawStr[:]
   if dtype==DATATYPE.Binary:
      pstr = binary_to_hex(rawStr)
      pstr = prettyHex(pstr, indent='  ', withAddr=False)
   elif dtype==DATATYPE.Hex:
      pstr = prettyHex(pstr, indent='  ', withAddr=False)
   else:
      pstr = '   ' + '\n   '.join(pstr.split('\n'))

   logging.log(loglevel, methodStr + pstr)
