from __future__ import (absolute_import, division,
                        print_function, unicode_literals)
################################################################################
#                                                                              #
# Copyright (C) 2011-2015, Armory Technologies, Inc.                           #
# Distributed under the GNU Affero General Public License (AGPL v3)            #
# See LICENSE or http://www.gnu.org/licenses/agpl.html                         #
#                                                                              #
################################################################################
import logging
import os
import binascii

from armoryengine.ArmoryUtils import BlockComponent, BIGENDIAN, \
   LITTLEENDIAN, enum, UINT32_MAX, UNINITIALIZED, hash256, \
   CPP_TXOUT_NONSTANDARD, CPP_TXOUT_P2SH, CPP_TXOUT_MULTISIG, \
   CPP_TXOUT_P2WPKH, CPP_TXOUT_P2WSH, CPP_TXOUT_NONSTANDARD, \
   CPP_TXOUT_STDPUBKEY33, CPP_TXOUT_STDPUBKEY65, CPP_TXOUT_STDHASH160, \
   CPP_TXIN_STDUNCOMPR, CPP_TXIN_STDCOMPR, CPP_TXIN_SPENDP2SH, \
   CPP_TXIN_P2WPKH_P2SH, CPP_TXIN_P2WSH_P2SH, MIN_RELAY_TX_FEE, \
   int_to_binary, SignatureError, indent, binary_to_hex, \
   hash160, sha256, ONE_BTC
from armoryengine.AddressUtils import hash160_to_addrStr, binary_to_base58, \
   CheckHash160, binScript_to_p2shAddrStr, script_to_addrStr, \
   script_to_scrAddr, scrAddr_to_addrStr, BadAddressError
from armoryengine.BinaryPacker import BinaryPacker, UINT8, UINT32, UINT64, \
   VAR_INT, BINARY_CHUNK
from armoryengine.BinaryUnpacker import BinaryUnpacker
from armoryengine.AsciiSerialize import AsciiSerializable
from armoryengine.CppBridge import TheBridge, BridgeSigner
from armoryengine.CoinSelection import sumTxOutList
from armoryengine.PyBtcAddress import PyBtcAddress
from armoryengine.BDM import TheBDM, BDM_BLOCKCHAIN_READY
from armoryengine.Script import convertScriptToOpStrings

from qtdialogs.DlgUnlockWallet import UnlockWalletHandler

UNSIGNED_TX_VERSION = 2

WITNESS_MARKER = 0
WITNESS_FLAG = 1

TXIN_EXT_P2SHSCRIPT = 0x10
USTX_EXT_SIGNERTYPE = 0x20
USTX_EXT_SIGNERSTATE = 0x30

SIGHASH_ALL = 1
SIGHASH_NONE = 2
SIGHASH_SINGLE = 3
SIGHASH_ANYONECANPAY = 0x80

USTX_TYPE_UNKNOWN = 0
USTX_TYPE_MODERN  = 1
USTX_TYPE_LEGACY  = 2
USTX_TYPE_PSBT    = 3

BASE_SCRIPT = 'base_script'

################################################################################
class InputSignedStatusObject(object):
   def __init__(self, protoData):
      self.proto = protoData
      self.pubKeyMap = {}
      for pubKeyPair in protoData.signStates:
         self.pubKeyMap[pubKeyPair.pubKey] = pubKeyPair.hasSig

   #############################################################################
   def isSignedForPubKey(self, pubkey):
      if pubkey not in self.pubKeyMap:
         return False
      return self.pubKeyMap[pubkey]

   #############################################################################
   def getPubKeyList(self):
      pkList = []
      for key in self.pubKeyMap:
         pkList.append(key)
      return pkList

################################################################################
class SignerObject(object):
   def __init__(self):
      self.signer = BridgeSigner()

   #############################################################################
   def __del__(self):
      self.signer.cleanup()

   #############################################################################
   def setup(self):
      self.signer.initNew()

   #############################################################################
   def setVersion(self, version):
      self.signer.setVersion(version)

   #############################################################################
   def setLockTime(self, locktime):
      self.signer.setLockTime(locktime)

   #############################################################################
   def addSpenderByOutpoint(self, hashVal, index, seq):
      self.signer.addSpenderByOutpoint(hashVal, index, seq)

   #############################################################################
   def populateUtxo(self, hashVal, index, value, script):
      self.signer.populateUtxo(hashVal, index, value, script)

   #############################################################################
   def addSupportingTx(self, rawTxData):
      self.signer.addSupportingTx(rawTxData)

   #############################################################################
   def addRecipient(self, value, script):
      self.signer.addRecipient(value, script)

   #############################################################################
   def toTxSigCollect(self, ustxType):
      return self.signer.toTxSigCollect(ustxType)

   #############################################################################
   def fromTxSigCollect(self, txSigCollect):
      self.signer.fromTxSigCollect(txSigCollect)

   #############################################################################
   def signTx(self, wltId, callback, parentDlg):
      def callbackHandler(callbackFunc, reply):
         callbackFunc(reply.success)

      unlockHandler = UnlockWalletHandler(wltId, "Sign Transaction", parentDlg)
      self.signer.signTx(wltId, unlockHandler, callbackHandler, [callback])

   #############################################################################
   def getSignedTx(self):
      rawSignedTx = self.signer.getSignedTx()
      if rawSignedTx == None:
         raise SignatureError()

      signedTx = PyTx()
      signedTx.unserialize(rawSignedTx)
      return signedTx

   #############################################################################
   def getUnsignedTx(self):
      rawTx = self.signer.getUnsignedTx()
      if rawTx == None:
         raise SignatureError()

      pytx = PyTx()
      pytx.unserialize(rawTx)
      return pytx

   #############################################################################
   def getSignedStateForInput(self, inputId):
      return InputSignedStatusObject(
         self.signer.getSignedStateForInput(inputId))

   #############################################################################
   def resolve(self, wltId):
      return self.signer.resolve(wltId)

   #############################################################################
   def fromType(self):
      return self.signer.fromType()

   #############################################################################
   def canLegacySerialize(self):
      return self.signer.canLegacySerialize()


################################################################################
def getMultisigScriptInfo(rawScript):
   """
   Gets the Multi-Sig tx type, as well as all the address-160 strings of
   the keys that are needed to satisfy this transaction.  This currently
   only identifies M-of-N transaction types, returning unknown otherwise.

   However, the address list it returns should be valid regardless of
   whether the type was unknown:  we assume all 20-byte chunks of data
   are public key hashes, and 65-byte chunks are public keys.

   M==0 (output[0]==0) indicates this isn't a multisig script
   """

   if not getTxOutScriptType(rawScript)==CPP_TXOUT_MULTISIG:
      return [0, 0, None, None]

   scrAddr = ''
   addr160List = []
   pubKeyList   = []

   M,N = 0,0

   pubKeyStr = Cpp.BtcUtils().getMultisigPubKeyInfoStr(rawScript)

   bu = BinaryUnpacker(pubKeyStr)
   M = bu.get(UINT8)
   N = bu.get(UINT8)

   if M==0:
      return [0, 0, None, None]

   for i in range(N):
      pkstr = bu.get(BINARY_CHUNK, 33)
      if pkstr[0] == '\x04':
         pkstr += bu.get(BINARY_CHUNK, 32)
      pubKeyList.append(pkstr)
      addr160List.append(hash160(pkstr))

   return M, N, addr160List, pubKeyList


################################################################################
def getHash160ListFromMultisigScrAddr(scrAddr):
   mslen = len(scrAddr) - 3
   if not (mslen%20==0 and scrAddr[0]==SCRADDR_MULTISIG_BYTE):
      raise BadAddressError('Supplied ScrAddr is not multisig!')

   catList = scrAddr[3:]
   return [catList[20*i:20*(i+1)] for i in range(len(catList)/20)]


################################################################################
# These two methods are just easier-to-type wrappers around the C++ methods
def getTxOutScriptType(script):
   return TheBridge.scriptUtils.getTxOutScriptType(script)

################################################################################
# These two methods are just easier-to-type wrappers around the C++ methods
def getTxOutScriptDisplayStr(script):
   scrAddr = script_to_scrAddr(script)
   scrType = getTxOutScriptType(script)
   if scrType in CPP_TXOUT_HAS_ADDRSTR:
      return scrAddr_to_addrStr(scrAddr)
   elif scrType==CPP_TXOUT_MULTISIG:
      M, N, addrs, pubs = getMultisigScriptInfo(script)
      p2shStr = script_to_addrStr(script_to_p2sh_script(script))
      return '[Multisig %d-of-%d] (not P2SH but would be %s)' % (M,N,p2shStr)
   else:
      return '[Non-Standard Script: %s]: ' % binary_to_hex(scrAddr[1:65])


################################################################################
def getTxInScriptType(txinObj):
   """
   NOTE: this method takes a TXIN object, not just the script itself.  This
         is because this method needs to see the OutPoint to distinguish an
         UNKNOWN TxIn from a coinbase-TxIn
   """
   script = txinObj.binScript
   prevTx = txinObj.outpoint.txHash
   return TheBridge.scriptUtils.getTxInScriptType(script, prevTx)

################################################################################
def getTxInP2SHScriptType(txinObj):
   """
   If this TxIn is identified as SPEND-P2SH, then it contains a subscript that
   is really a TxOut script (which must hash to the value included on the
   actual TxOut script).

   Use this to get the TxOut script type of the Spend-P2SH subscript
   """
   scrType = getTxInScriptType(txinObj)
   if not scrType==CPP_TXIN_SPENDP2SH:
      return None

   lastPush = TheBridge.scriptUtils.getLastPushDataInScript(txinObj.binScript)

   return getTxOutScriptType(lastPush)


################################################################################
def TxInExtractAddrStrIfAvail(txinObj):
   rawScript  = txinObj.binScript
   prevTxHash = txinObj.outpoint.txHash
   scrType = TheBridge.scriptUtils.getTxInScriptType(rawScript, prevTxHash)
   lastPush = TheBridge.scriptUtils.getLastPushDataInScript(rawScript)

   if scrType in [CPP_TXIN_STDUNCOMPR, CPP_TXIN_STDCOMPR]:
      return hash160_to_addrStr( hash160(lastPush) )
   elif scrType == CPP_TXIN_SPENDP2SH:
      return binScript_to_p2shAddrStr(lastPush)
   elif scrType == CPP_TXIN_P2WPKH_P2SH:
      return binScript_to_p2shAddrStr(hash160(rawScript))
   elif scrType == CPP_TXIN_P2WSH_P2SH:
      return binScript_to_p2shAddrStr(sha256(rawScript[1:]))
   else:
      return ''


################################################################################
def TxInExtractPreImageIfAvail(txinObj):
   rawScript  = txinObj.binScript
   prevTxHash = txinObj.outpoint.txHash
   scrType = Cpp.BtcUtils().getTxInScriptTypeInt(rawScript, prevTxHash)

   if scrType == [CPP_TXIN_STDUNCOMPR, CPP_TXIN_STDCOMPR, CPP_TXIN_SPENDP2SH]:
      return Cpp.BtcUtils().getLastPushDataInScript(rawScript)
   else:
      return ''




# Finally done with all the base conversion functions and ECDSA code
# Now define the classes for the objects that will use this


################################################################################
#  Transaction Classes
################################################################################


################################################################################
class PyOutPoint(BlockComponent):
   def __init__(self, txHash=None, txOutIndex=None):
      self.txHash     = txHash
      self.txOutIndex = txOutIndex

   def __eq__(self, op2):
      return self.serialize()==op2.serialize()
   def __ne__(self, op2):
      return not self.__eq__(op2)

   def unserialize(self, toUnpack):
      if isinstance(toUnpack, BinaryUnpacker):
         opData = toUnpack
      else:
         opData = BinaryUnpacker( toUnpack )

      if opData.getRemainingSize() < 36: raise UnserializeError
      self.txHash = opData.get(BINARY_CHUNK, 32)
      self.txOutIndex = opData.get(UINT32)
      return self

   def serialize(self):
      binOut = BinaryPacker()
      binOut.put(BINARY_CHUNK, self.txHash)
      binOut.put(UINT32, self.txOutIndex)
      return binOut.getBinaryString()

   def getTxHashStr(self):
      return binascii.hexlify(self.txHash)

   def pprint(self, nIndent=0, endian=BIGENDIAN):
      indstr = indent*nIndent
      print(indstr + 'OutPoint:')
      print(indstr + indent + 'PrevTxHash:', \
                  binary_to_hex(self.txHash, endian), \
                  '(BE)' if endian==BIGENDIAN else '(LE)')
      print(indstr + indent + 'TxOutIndex:', self.txOutIndex)


#####
class PyTxIn(BlockComponent):
   def __init__(self):
      self.outpoint   = UNINITIALIZED
      self.binScript  = UNINITIALIZED
      self.intSeq     = 2**32-1
      self.outpointValue = 2**64-1
      self.supportTx = None

   def unserialize(self, toUnpack):
      if isinstance(toUnpack, BinaryUnpacker):
         txInData = toUnpack
      else:
         txInData = BinaryUnpacker( toUnpack )

      self.outpoint  = PyOutPoint().unserialize(txInData.get(BINARY_CHUNK, 36) )

      scriptSize     = txInData.get(VAR_INT)
      if txInData.getRemainingSize() < scriptSize+4: raise UnserializeError
      self.binScript = txInData.get(BINARY_CHUNK, scriptSize)
      self.intSeq    = txInData.get(UINT32)
      return self

   def getOutPoint(self):
      return self.outpoint

   def getScript(self):
      return self.binScript
   
   def serialize(self):
      binOut = BinaryPacker()
      binOut.put(BINARY_CHUNK, self.outpoint.serialize() )
      binOut.put(VAR_INT, len(self.binScript))
      binOut.put(BINARY_CHUNK, self.binScript)
      binOut.put(UINT32, self.intSeq)
      return binOut.getBinaryString()

   def pprint(self, nIndent=0, endian=BIGENDIAN):
      indstr = indent*nIndent
      print(indstr + 'PyTxIn:')
      print(indstr + indent + 'PrevTxHash:', \
                  binary_to_hex(self.outpoint.txHash, endian), \
                      '(BE)' if endian==BIGENDIAN else '(LE)')
      print(indstr + indent + 'TxOutIndex:', self.outpoint.txOutIndex)
      print(indstr + indent + 'Script:    ', \
                  '('+binary_to_hex(self.binScript)[:64]+')')
      addrStr = TxInExtractAddrStrIfAvail(self)
      if len(addrStr)==0:
         addrStr = '<UNKNOWN>'
      print(indstr + indent + 'Sender:    ', addrStr)
      print(indstr + indent + 'Seq:       ', self.intSeq)

   def toString(self, nIndent=0, endian=BIGENDIAN):
      indstr = indent*nIndent
      indstr2 = indstr + indent
      result = indstr + 'PyTxIn:'
      result = ''.join([result, '\n',  indstr2 + 'PrevTxHash:', \
                  binary_to_hex(self.outpoint.txHash, endian), \
                      '(BE)' if endian==BIGENDIAN else '(LE)'])
      result = ''.join([result, '\n',  indstr2 + 'TxOutIndex:', \
                                    str(self.outpoint.txOutIndex)])
      result = ''.join([result, '\n',  indstr2 + 'Script:    ', \
                  '('+binary_to_hex(self.binScript)[:64]+'...)'])
      addrStr = TxInExtractAddrStrIfAvail(self)
      if len(addrStr)>0:
         result = ''.join([result, '\n',  indstr2 + 'Sender:    ', addrStr])
      result = ''.join([result, '\n',  indstr2 + 'Seq:       ', str(self.intSeq)])
      return result

#####
class PyTxOut(BlockComponent):
   def __init__(self):
      self.value     = UNINITIALIZED
      self.binScript = UNINITIALIZED

   def unserialize(self, toUnpack):
      if isinstance(toUnpack, BinaryUnpacker):
         txOutData = toUnpack
      else:
         txOutData = BinaryUnpacker( toUnpack )

      self.value       = txOutData.get(UINT64)
      scriptSize       = txOutData.get(VAR_INT)
      if txOutData.getRemainingSize() < scriptSize: raise UnserializeError
      self.binScript = txOutData.get(BINARY_CHUNK, scriptSize)
      return self

   def getValue(self):
      return self.value

   def getScript(self):
      return self.binScript

   def getScrAddressStr(self):
      return script_to_addrStr(self.binScript)

   def serialize(self):
      binOut = BinaryPacker()
      binOut.put(UINT64, self.value)
      binOut.put(VAR_INT, len(self.binScript))
      binOut.put(BINARY_CHUNK, self.binScript)
      return binOut.getBinaryString()

   def pprint(self, nIndent=0, endian=BIGENDIAN):
      print(self.toString(nIndent, endian))

   def toString(self, nIndent=0, endian=BIGENDIAN):
      indstr  = indent*nIndent
      indstr2 = indent*nIndent + indent
      valStr, btcStr = str(self.value), str(float(self.value)/ONE_BTC)
      result = indstr + 'TxOut:\n'
      result += indstr2 + 'Value:   %s (%s)\n' % (valStr, btcStr)
      result += indstr2
      txoutType = getTxOutScriptType(self.binScript)

      if txoutType in [CPP_TXOUT_STDPUBKEY33, CPP_TXOUT_STDPUBKEY65]:
         result += 'Script:  PubKey(%s) OP_CHECKSIG \n' % \
                                          script_to_addrStr(self.binScript)
      elif txoutType == CPP_TXOUT_STDHASH160:
         result += 'Script:  OP_DUP OP_HASH160 (%s) OP_EQUALVERIFY OP_CHECKSIG' % \
                                          script_to_addrStr(self.binScript)
      elif txoutType == CPP_TXOUT_P2SH:
         result += 'Script:  OP_HASH160 (%s) OP_EQUAL' % \
                                          script_to_addrStr(self.binScript)
      else:
         opStrList = convertScriptToOpStrings(self.binScript)
         result += 'Script:  ' + ' '.join(opStrList)

      return result

#####
class PyTxWitness(BlockComponent):
   def __init__(self):
      self.binWitness  = UNINITIALIZED

   def unserialize(self, toUnpack):
      if isinstance(toUnpack, BinaryUnpacker):
         txWitnessData = toUnpack
      else:
         txWitnessData = BinaryUnpacker( toUnpack )

      self.binWitness = []
      stackSize = txWitnessData.get(VAR_INT)
      self.binWitness.append(stackSize)
      if stackSize > 0:
         for i in range(0, stackSize, 1):
            stackItemSize = txWitnessData.get(VAR_INT)
            if txWitnessData.getRemainingSize() < stackItemSize: raise UnserializeError
            stackItem = txWitnessData.get(BINARY_CHUNK, stackItemSize)
            self.binWitness.append(stackItemSize)
            self.binWitness.append(stackItem)
      return self

   def getWitnesses(self):
      return self.binWitness

   def getSize(self):
      if self.binWitness != UNINITIALIZED:
         return len(self.binWitness)
      return 0

   def serialize(self):
      binOut = BinaryPacker()
      binOut.put(VAR_INT, self.binWitness[0])
      for i in range(1, len(self.binWitness), 2):
         binOut.put(VAR_INT, self.binWitness[i])
         binOut.put(BINARY_CHUNK, self.binWitness[i+1])
      return binOut.getBinaryString()

   def pprint(self, nIndent=0, endian=BIGENDIAN):
      print(self.toString(nIndent, endian))

   def toString(self, nIndent=0, endian=BIGENDIAN):
      indstr = indent*nIndent
      indstr2 = indstr + indent
      result = indstr + 'PyWitness:'
      result = ''.join([result, '\n',  indstr2 + 'Stack Size:', \
                                       str(self.binWitness[0])])
      for i in range(0, int(len(self.binWitness)/2), 2):
          result = ''.join([result, '\n',  indstr2 + 'Stack Item:', \
                                       str(self.binWitness[i])])
      return result

#####
class PyTx(BlockComponent):
   def __init__(self):
      self.version    = UNINITIALIZED
      self.inputs     = UNINITIALIZED
      self.outputs    = UNINITIALIZED
      self.lockTime   = 0
      self.thisHash   = UNINITIALIZED
      self.rbfFlag    = False
      self.witnesses  = UNINITIALIZED
      self.useWitness = False
      self.size = 0

   def isInitialized(self):
      return self.size != 0

   def serialize(self):
      binOut = BinaryPacker()
      binOut.put(UINT32, self.version)
      if self.useWitness:
         binOut.put(UINT8, WITNESS_MARKER)
         binOut.put(UINT8, WITNESS_FLAG)
      binOut.put(VAR_INT, len(self.inputs))
      for txin in self.inputs:
         binOut.put(BINARY_CHUNK, txin.serialize())
      binOut.put(VAR_INT, len(self.outputs))
      for txout in self.outputs:
         binOut.put(BINARY_CHUNK, txout.serialize())
      if self.useWitness:
         for witItem in self.witnesses:
            binOut.put(BINARY_CHUNK, witItem.serialize())
      binOut.put(UINT32, self.lockTime)
      return binOut.getBinaryString()

   def serializeWithoutWitness(self):
      binOut = BinaryPacker()
      binOut.put(UINT32, self.version)
      binOut.put(VAR_INT, len(self.inputs))
      for txin in self.inputs:
         binOut.put(BINARY_CHUNK, txin.serialize())
      binOut.put(VAR_INT, len(self.outputs))
      for txout in self.outputs:
         binOut.put(BINARY_CHUNK, txout.serialize())
      binOut.put(UINT32, self.lockTime)
      return binOut.getBinaryString()

   def unserialize(self, toUnpack):
      txsize = 0
      if isinstance(toUnpack, BinaryUnpacker):
         txData = toUnpack
         txsize = toUnpack.getSize()
      else:
         txsize = len(toUnpack)
         txData = BinaryUnpacker( toUnpack )

      startPos = txData.getPosition()
      self.inputs     = []
      self.outputs    = []
      self.witnesses  = []
      self.version    = txData.get(UINT32)
      marker = txData.get(UINT8)
      flag = txData.get(UINT8)
      if marker == WITNESS_MARKER and flag == WITNESS_FLAG:
         self.useWitness = True
      else:
         txData.rewind(2)
      numInputs  = txData.get(VAR_INT)
      for i in range(numInputs):
         txin = PyTxIn().unserialize(txData)
         self.inputs.append( txin )

      numOutputs = txData.get(VAR_INT)
      for i in range(numOutputs):
         self.outputs.append( PyTxOut().unserialize(txData) )
      if self.useWitness:
         for i in range(numInputs):
            self.witnesses.append( PyTxWitness().unserialize(txData))

      self.lockTime   = txData.get(UINT32)
      endPos = txData.getPosition()
      self.nBytes = endPos - startPos
      self.thisHash = hash256(self.serializeWithoutWitness())

      self.size = txsize
      return self

   def getNumTxOut(self):
      return len(self.outputs)

   def getTxOut(self, id):
      return self.outputs[id]

   def getNumTxIn(self):
      return len(self.inputs)

   def getTxIn(self, id):
      return self.inputs[id]

   def getHash(self):
      return self.thisHash

   def getHashHex(self, endianness=LITTLEENDIAN):
      return binary_to_hex(self.getHash(), endOut=endianness)

   def copy(self):
      return PyTx().unserialize(self.serialize())

   def copyWithoutWitness(self):
      return PyTx().unserialize(self.serializeWithoutWitness())

   def getSize(self):
      return self.size

   def getTxWeight(self):
      if self.useWitness == False:
         return self.size

      witnessSize = 0
      for witness in self.witnesses:
         witnessSize += witness.getSize()

      witnessSize *= 0.75
      return self. size - witnessSize

   def makeRecipientsList(self):
      """
      Make a list of lists, each one containing information about
      an output in this tx.  Usually contains
         [ScriptType, Value, Script]
      May include more information if any of the scripts are multi-sig,
      such as public keys and multi-sig type (M-of-N)
      """
      recipInfoList = []
      for txout in self.outputs:
         recipInfoList.append([])

         scrType = getTxOutScriptType(txout.binScript)
         recipInfoList[-1].append(scrType)
         recipInfoList[-1].append(txout.value)
         recipInfoList[-1].append(txout.binScript)
         if scrType == CPP_TXOUT_MULTISIG:
            recipInfoList[-1].append(getMultisigScriptInfo(txout.binScript))
         else:
            recipInfoList[-1].append([])

      return recipInfoList


   def pprint(self, nIndent=0, endian=BIGENDIAN):
      indstr = indent*nIndent
      print(indstr + 'Transaction:')
      print(indstr + indent + 'TxHash:   ', self.getHashHex(endian), \
                                    '(BE)' if endian==BIGENDIAN else '(LE)')
      print(indstr + indent + 'Version:  ', self.version)
      print(indstr + indent + 'nInputs:  ', len(self.inputs))
      print(indstr + indent + 'nOutputs: ', len(self.outputs))
      if self.useWitness:
         print(indstr + indent + 'nWitnesses: ', len(self.witnesses))
      print(indstr + indent + 'LockTime: ', self.lockTime)
      print(indstr + indent + 'Inputs: ')
      for inp in self.inputs:
         inp.pprint(nIndent+2, endian=endian)
      print(indstr + indent + 'Outputs: ')
      for out in self.outputs:
         out.pprint(nIndent+2, endian=endian)
      for witness in self.witnesses:
         witness.pprint(nIndent+2, endian=endian)

   def toString(self, nIndent=0, endian=BIGENDIAN):
      indstr = indent*nIndent
      result = indstr + 'Transaction:'
      result = ''.join([result, '\n',  indstr + indent + 'TxHash:   ', self.getHashHex(endian), \
                                    '(BE)' if endian==BIGENDIAN else '(LE)'])
      result = ''.join([result, '\n',   indstr + indent + 'Version:  ', str(self.version)])
      result = ''.join([result, '\n',   indstr + indent + 'nInputs:  ', str(len(self.inputs))])
      result = ''.join([result, '\n',   indstr + indent + 'nOutputs: ', str(len(self.outputs))])
      result = ''.join([result, '\n',   indstr + indent + 'nWitnesses: ', str(len(self.witnesses))])
      result = ''.join([result, '\n',   indstr + indent + 'LockTime: ', str(self.lockTime)])
      result = ''.join([result, '\n',   indstr + indent + 'Inputs: '])
      for inp in self.inputs:
         result = ''.join([result, '\n',   inp.toString(nIndent+2, endian=endian)])
      result = ''.join([result, '\n', indstr + indent + 'Outputs: '])
      for out in self.outputs:
         result = ''.join([result, '\n',  out.toString(nIndent+2, endian=endian)])
      for witness in self.witnesses:
         result = ''.join([result, '\n',  witness.toString(nIndent+2, endian=endian)])
      return result

   def pprintHex(self, nIndent=0):
      bu = BinaryUnpacker(self.serialize())
      theSer = self.serialize()
      print(binary_to_hex(bu.get(BINARY_CHUNK, 4)))
      if self.useWitness:
         print(binary_to_hex(bu.get(BINARY_CHUNK, 1)))
         print(binary_to_hex(bu.get(BINARY_CHUNK, 1)))
      nTxin = bu.get(VAR_INT)
      print('VAR_INT(%d)' % nTxin)
      for i in range(nTxin):
         print(binary_to_hex(bu.get(BINARY_CHUNK,32)))
         print(binary_to_hex(bu.get(BINARY_CHUNK,4)))
         scriptSz = bu.get(VAR_INT)
         print('VAR_IN(%d)' % scriptSz)
         print(binary_to_hex(bu.get(BINARY_CHUNK,scriptSz)))
         print(binary_to_hex(bu.get(BINARY_CHUNK,4)))
      nTxout = bu.get(VAR_INT)
      print('VAR_INT(%d)' % nTxout)
      for i in range(nTxout):
         print(binary_to_hex(bu.get(BINARY_CHUNK,8)))
         scriptSz = bu.get(VAR_INT)
         print(binary_to_hex(bu.get(BINARY_CHUNK,scriptSz)))
      if self.useWitness:
         for i in range(nTxin):
            stackSize = bu.get(VAR_INT)
            print('VAR_IN(%d)' % stackSize)
            for j in range(stackSize):
               stackItemSize = bu.get(VAR_INT)
               print('VAR_IN(%d)' % stackItemSize)
               print(binary_to_hex(bu.get(BINARY_CHUNK, stackItemSize)))
      print(binary_to_hex(bu.get(BINARY_CHUNK, 4)))

   def setRBF(self, flag):
      self.rbfFlag = flag

   def isRBF(self):
      return self.rbfFlag

# Use to identify status of individual sigs on an UnsignedTxINPUT
TXIN_SIGSTAT = enum('ALREADY_SIGNED',
                    'WLT_ALREADY_SIGNED',
                    'WLT_CAN_SIGN',
                    'NO_SIGNATURE')

# Use to identify status of USTXI objects of an UnsignedTransaction obj
TX_SIGSTAT = enum('SIGNING_COMPLETE',
                  'WLT_CAN_COMPLETE',
                  'WLT_CAN_CONTRIB',
                  'CANNOT_COMPLETE')


################################################################################
# This is a container object for holding data about USTXI signing status
class InputSigningStatus(object):
   def __init__(self):
      self.M              = 0
      self.N              = 0
      self.statusN        = []
      self.statusM        = []
      self.allSigned      = False
      self.wltCanSign     = False
      self.wltIsRelevant  = False
      self.wltCanComplete = False


   def pprint(self, indent=3, lutFunc=None):
      if lutFunc is None:
         def lutDispLetter(code):
            if   code==TXIN_SIGSTAT.ALREADY_SIGNED:
               return '#'
            elif code==TXIN_SIGSTAT.WLT_ALREADY_SIGNED:
               return '@'
            elif code==TXIN_SIGSTAT.WLT_CAN_SIGN:
               return '-'
            elif code==TXIN_SIGSTAT.NO_SIGNATURE:
               return '_'
         lutFunc = lutDispLetter

      ind = ' '*indent
      print(ind, end=' ')
      print('(%d-of-%d)' % (self.M, self.N), end=' ')
      print('AllSigned: %s' % str(self.allSigned).ljust(6), end=' ')
      print('AllSlots:',  ' '.join([lutFunc(s) for s in self.statusN]), ' ', end=' ')
      print('ReqSorted:', ' '.join([lutFunc(s) for s in self.statusM]), ' ')


################################################################################
# This is a container object for holding a list of InputSigningStatus objects
# and aggregating info about
class TxSigningStatus(object):
   def __init__(self):
      self.numInputs        = 0
      self.statusList       = []
      self.canBroadcast     = False
      self.wltCanSign       = False
      self.wltIsRelevant    = False
      self.wltAlreadySigned = False
      self.wltCanComplete   = False


   def pprint(self, indent=3, lutFunc=None):
      print(' '*indent + 'Tx has %d inputs:' % self.numInputs)
      for stat in self.statusList:
         stat.pprint(indent+3, lutFunc)


################################################################################
def generatePreHashTxMsgToSign(pytx, txInIndex, prevTxOutScript, hashcode=1):
   """
   This wraps up all the complexity of:
   https://en.bitcoin.it/w/images/en/7/70/Bitcoin_OpCheckSig_InDetail.png
   into a few simple lines of code!
   (blank all scripts except this one, insert prev script, append hashcode)

   Right now only supports SIGHASH_ALL
   """
   if not hashcode == 1:
      # NO OTHER HASHCODES HAVE BEEN TESTED
      LOGERROR('Only hashcode=1 is supported at this time!')
      LOGERROR('Requested hashcode=%d' % hashcode)
      return None

   # Create a copy of the tx with all scripts blanked out
   txCopy = pytx.copyWithoutWitness()
   for i in range(len(txCopy.inputs)):
      txCopy.inputs[i].binScript = ''

   # Set the script of the TxIn being signed, to the previous TxOut script
   txCopy.inputs[txInIndex].binScript = prevTxOutScript
   
   # Remove outputs given SIGHASH type (DISABLED DUE TO FIRST CONDITIONAL ABOVE)
   if hashcode & 0x7fffffff == SIGHASH_NONE:
      txCopy.outputs = []
   elif hashcode & 0x7fffffff == SIGHASH_SINGLE:
      txCopy.outputs = [txCopy.outputs[txInIndex]]

   # Remove all other inputs if needed (DISABLED DUE TO FIRST CONDITIONAL ABOVE)
   if hashcode & SIGHASH_ANYONECANPAY:
      txCopy.inputs = [txCopy.inputs[txInIndex]]

   hashCode1  = int_to_binary(hashcode, widthBytes=1)
   hashCode4  = int_to_binary(hashcode, widthBytes=4, endOut=LITTLEENDIAN)
   preHashMsg = txCopy.serialize() + hashCode4
   return preHashMsg, hashCode1



################################################################################
class UnsignedTxInput(AsciiSerializable):
   """
   The name is really "UnsignedTx" input ... it is an input of an unsignedTx

   This used to be part of the PyTxDP class itself, but it didn't make sense
   to be tracking all those individual, parallel lists anymore.  So now we
   use this class to hold all the data that used to be split between half
   a dozen TxDP vars, and just store a list of these in the TxDP.

   This holds the full, raw, supporting transaction for the particular input
   needing to be signed, as well as the raw P2SH script if needed.  Can also
   store a "contributor" ID -- all UTXO inputs with the same contribID will
   be shown in the user interface as belonging to a single person/device.

   A "locator" is a 4-byte, user-specified string, that identifies where
   in a BIP32 wallet a particular address is located.  This isn't needed
   for regular Armory wallets, but may be needed in the future for HW or
   other lightweight wallets to be able to locate their keys (because they
   don't store all the keys, they compute them on-the-fly and need to be
   told the BIP32 branch where it is).

   This class will also be used to store/track signatures.  Note that for
   regular Pay2PubKeyHash scripts, "adding a signature" to this object
   means also adding a public key

   insert* inputs are either single values (for single-sig scraddrs), or
   [index, val] pairs which specify which input is associated with the val

   self.txoScript is always the exact script of the TxOut being spend

   self.scriptType is the CPP_TXOUT_TYPE of the txoScript *UNLESS* that
   script is P2SH -- then it will be the type of the P2SH subscript,
   and that subscript will be stored in self.p2shScript

   i.e.  consider a bare multisig and a P2SH multisig:

   Bare Multisig:

      scriptType == CPP_TXOUT_MULTISIG
      txoScript  == "OP_2 [KeyA] [KeyB] [KeyC] OP_3 OP_CHECKMULTISIG"
      p2shScript == ''

   Same script but using P2SH:

      scriptType == CPP_TXOUT_MULTISIG
      txoScript  == "OP_DUP [ScriptHash] OP_EQUALVERIFY"
      p2shScript == "OP_2 [KeyA] [KeyB] [KeyC] OP_3 OP_CHECKMULTISIG"

   The txoScript variable always contains the script that needs to be inserted
   into the TxIn script when signing (look at OP_CHECKSIG for details)
   """

   EQ_ATTRS_SIMPLE = ['version', 'supportTx', 'outpoint', 'txoScript', 'value',
                      'scriptType', 'contribID', 'contribLabel', 'p2shScript',
                      'sequence', 'keysListed', 'sigsNeeded']
   EQ_ATTRS_LISTS  = ['scrAddrs', 'signatures', 'wltLocators', 'pubKeys']


   #############################################################################
   def __init__(self, rawSupportTx='',
                      txoutIndex=-1,
                      p2shMap={},
                      pubKeyMap=None,
                      insertSigs=None,
                      insertWltLocs=None,
                      contribID=None,
                      contribLabel=None,
                      sequence=UINT32_MAX,
                      version=UNSIGNED_TX_VERSION,
                      inputID=UINT32_MAX):

      if not rawSupportTx:
         self.isInitialized = False
         return

      tx = PyTx().unserialize(rawSupportTx)
      txout = tx.outputs[txoutIndex]

      self.isInitialized = True

      self.version     = version
      self.supportTx   = rawSupportTx
      self.outpoint    = PyOutPoint(tx.getHash(), txoutIndex)
      self.txoScript   = txout.getScript()
      self.scriptType  = getTxOutScriptType(self.txoScript)
      self.value       = txout.getValue()
      self.contribID   = '' if contribID is None else contribID
      self.contribLabel= '' if contribLabel is None else contribLabel
      self.p2shMap     = p2shMap if p2shMap is not None else dict()
      self.sequence    = sequence

      # Each of these will be a single value for single-signature UTXOs
      self.keysListed  = 0
      self.sigsNeeded  = 0
      self.scrAddrs    = None
      self.pubKeys     = None
      self.signatures  = None
      self.wltLocators = None

      self.isLegacyScript = True
      self.witnessData = ''
      self.isSegWit = False
      self.inputID = inputID


      if pubKeyMap is not None and not isinstance(pubKeyMap, dict):
         if isinstance(pubKeyMap, (list,tuple)):
            pub = dict([[SCRADDR_P2PKH_BYTE+hash160(pk), pk] for pk in pubKeyMap])
         elif isinstance(pubKeyMap, basestring):
            pub = {SCRADDR_P2PKH_BYTE+hash160(pubKeyMap): pubKeyMap}
         else:
            LOGERROR('Invalid type for pub keys input: %s', str(type(pubKeyMap)))
            self.isInitialized = False
            return 
         pubKeyMap = pub
         

      #####
      # If this is P2SH, let's check things, and then use the sub-script
      nested = False
      baseScript = self.txoScript
      if self.scriptType==CPP_TXOUT_P2SH:
         nested = True

      # "insert*s" can either be a single values, or a list
      # of pairs [multisgIndex, pubKey]
      sigInsertMethod = self.setSignature
      if self.isSegWit:
         sigInsertMethod = self.setWitnessData
      
      insertData = [(insertSigs,    sigInsertMethod),
                    (insertWltLocs, self.setWltLocator)]

      for insList,insFunc in insertData:
         if insList is None:
            continue

         listlist = insList
         if not isinstance(listlist, (list, tuple)):
            listlist = [[0, insList]]

         if not isinstance(listlist[0], (list, tuple)):
            listlist = [listlist]

         for idx,data in listlist:
            if idx >= self.keysListed:
               LOGERROR('Insert list has index too big')
               continue
            insFunc(idx, data)


   #############################################################################
   def setPubKey(self, msIndex, pubKey):
      self.pubKeys[msIndex] = pubKey


   #############################################################################
   def setSignature(self, msIndex, sigStr):
      self.signatures[msIndex] = sigStr
      
   #############################################################################   
   def setWitnessData(self, msIndex, witData):
      self.setSignature(msIndex, '')
      self.witnessData = witData


   #############################################################################
   def setWltLocator(self, msIndex, wltLoc):
      self.wltLocators[msIndex] = wltLoc



   #############################################################################
   def getPublicKeyList(self):
      """
      This returns a list of PAIRS:  [pubkey, wltLocator]
      The wallet-locator is a client-dependent string which helps lightweight
      wallets find a particular key within their own wallet structure.  This
      would only be used by clients that don't store any keys, but always
      recompute them on the fly.  As of this writing, I'm not aware of any
      wallets doing this yet, so you can expect all wltLoc's to be empty.
      And even if they are not empty, you can still ignore it, unless you
      put the wltLoc yourself to help yourself out later!
      """
      return zip(self.pubKeys, self.wltLocators)

   #############################################################################
   def createSigScript(self, stripExtraSigs=True):
      """
      Here, we don't care what the orig script was in the TxOut being spent,
      unless it was P2SH.  It's because this method assumes that all the sigs
      are already created, valid and in the right place -- which requires the
      previous TxOut script to do (before we get to this method).

      The exception is if this is P2SH, in which case we have to append the
      script to the end of the sigScript.

      However we *do* care about the original TxOut script's TYPE, so that we
      know what data to include in the sig script.  For instance, Pay2PubKey
      scripts only require a signature, whereas Pay2PubKeyHash requires both
      a pub key and a sig.  P2SH always needs the serialized script appended
      to the end of it.
      """

      outScript = ''

      if self.scriptType in CPP_TXOUT_STDSINGLESIG and not self.signatures[0]:
         return ''

      # Remove Padding and guarantee LowS is used
      for i in range(len(self.signatures)):
         sig = self.signatures[i]
         if sig:
            rBin, sBin = getRSFromDERSig(sig)
            hashCode = sig[-1]
            # Don't forget to tack on the one-byte hashcode
            newSig = createDERSigFromRS(rBin, sBin) + hashCode
            if newSig != sig:
               self.signatures[i] = newSig

      # All signatures are already DER-encoded. 
      if self.scriptType == CPP_TXOUT_P2SH:
         raise InvalidScriptError('Nested P2SH not allowed')
      if self.scriptType in [CPP_TXOUT_STDPUBKEY33, CPP_TXOUT_STDPUBKEY65]:
         # Only need the signature to complete coinbase TxOut
         outScript = scriptPushData(self.signatures[0])
      elif self.scriptType==CPP_TXOUT_STDHASH160:
         # Gotta include the public key, too, for standard TxOuts
         serSig    = scriptPushData(self.signatures[0])
         serPubKey = scriptPushData(self.pubKeys[0])
         outScript = serSig + serPubKey
      elif  self.scriptType==CPP_TXOUT_P2WPKH:
         outScript = ''
      elif self.scriptType==CPP_TXOUT_MULTISIG:
         # Serialize non-empty sigs, replace empty ones with OP_0
         sigList = self.signatures[:]
         countSigs = lambda slist: sum([ (1 if s else 0)  for s in slist ])
         if stripExtraSigs:
            # Mainnet appears to treat extra sigs as non-std.  Remove if req
            while countSigs(sigList) > self.sigsNeeded:
               sigList.pop()

         OP_0 = getOpCode('OP_0')
         pushSig = lambda sig: (scriptPushData(sig) if sig else '')
         outScript = OP_0 + ''.join([pushSig(s) for s in sigList])
      elif self.scriptType==CPP_TXOUT_P2WSH:
         return self.p2shMap[BASE_SCRIPT]
      
      else:
         raise InvalidScriptError('Non-std script, cannot create sig script')


      # If P2SH, the script type already identifies the subscript.  But we
      # can tell because the p2shScript var will be non-empty.  All we do
      # for these types of script is append the raw p2sh script to the end
      if BASE_SCRIPT in self.p2shMap:
         outScript += scriptPushData(self.p2shMap[BASE_SCRIPT])

      return outScript
      
   #############################################################################
   def verifyAllSignatures(self, signer):
      signStat = self.evaluateSigningStatus(signer)
      return signStat.allSigned

   #############################################################################
   def toJSONMap(self, lite=False):
      outjson = {}
      outjson['version']      = self.version
      outjson['magicbytes']   = binary_to_hex(MAGIC_BYTES)
      outjson['outpoint']     = binary_to_hex(self.outpoint.serialize())
      if BASE_SCRIPT in self.p2shMap:
         outjson['p2shscript']   = binary_to_hex(self.p2shMap[BASE_SCRIPT])
      else:
         outjson['p2shscript'] = "N/A"

      outjson['contribid']    = self.contribID
      outjson['contriblabel'] = self.contribLabel
      outjson['sequence']     = self.sequence
      outjson['numkeys']      = self.keysListed

      outjson['keys'] = []
      for i in range(self.keysListed):
         outjson['keys'].append({})
         outjson['keys'][i]['pubkeyhex'] = binary_to_hex(self.pubKeys[i])
         outjson['keys'][i]['dersighex'] = binary_to_hex(self.signatures[i])
         outjson['keys'][i]['wltlochex'] = binary_to_hex(self.wltLocators[i])


      # Add a few convenience keys to avoid the caller having to calcs for it
      supportPyTx = PyTx().unserialize(self.supportTx)
      txoidx = binary_to_int(self.outpoint.serialize()[-4:], LITTLEENDIAN)

      outjson['supporttxhash_le'] = supportPyTx.getHashHex()
      outjson['supporttxhash_be'] = hex_switchEndian(supportPyTx.getHashHex())
      outjson['supporttxhash']    = outjson['supporttxhash_be'] # BE is default
      outjson['supporttxoutindex']= txoidx
      outjson['inputvalue'] = supportPyTx.outputs[txoidx].value

      if not lite:
         outjson['supporttx'] = binary_to_hex(self.supportTx)

      return outjson

   #############################################################################
   def fromJSONMap(self, jsonMap, skipMagicCheck=False):

      # There is a lite version of toJSONMap(), but can't create from a lite copy
      if not 'supporttx' in jsonMap:
         raise UnserializeError('Incomplete unsigned transaction map')


      ver   = jsonMap['version']
      magic = hex_to_binary(jsonMap['magicbytes'])

      if not ver == UNSIGNED_TX_VERSION:
         LOGWARN('Unserialing USTX of different version')
         LOGWARN('   USTX    Version: %d' % ver)
         LOGWARN('   Armory  Version: %d' % UNSIGNED_TX_VERSION)

      # Check the magic bytes of the lockbox match
      if not magic == MAGIC_BYTES and not skipMagicCheck:
         LOGERROR('Wrong network!')
         LOGERROR('    USTX    Magic: ' + binary_to_hex(magic))
         LOGERROR('    Armory  Magic: ' + binary_to_hex(MAGIC_BYTES))
         raise NetworkIDError('Network magic bytes mismatch')

      rawSupportTx = hex_to_binary(jsonMap['supporttx'])
      txoutIndex   = jsonMap['supporttxoutindex']
      p2sh         = hex_to_binary(jsonMap['p2shscript'])
      contribID    = jsonMap['contribid']
      contribLabel = jsonMap['contriblabel']
      sequence     = jsonMap['sequence']
      
      pubkeyMap = {}
      insertSigs = []
      insertWltLocs = []
      for i in range(jsonMap['numkeys']):
         pub = hex_to_binary(jsonMap['keys'][i]['pubkeyhex'])
         sig = hex_to_binary(jsonMap['keys'][i]['dersighex'])
         loc = hex_to_binary(jsonMap['keys'][i]['wltlochex'])

         pubkeyMap[SCRADDR_P2PKH_BYTE + hash160(pub)] = pub
         insertSigs.append([i, sig])
         insertWltLocs.append([i, loc])

      self.__init__(rawSupportTx, txoutIndex, p2sh, 
                    pubkeyMap, insertSigs, insertWltLocs,
                    contribID, contribLabel, sequence)

      return self


   #############################################################################
   def serialize(self):
      if not self.isInitialized:
         LOGERROR('Cannot serialize an uninitialzed unsigned txin')
         return None

      bp = BinaryPacker()
      bp.put(UINT32,       self.version)
      bp.put(BINARY_CHUNK, MAGIC_BYTES, 4)
      bp.put(BINARY_CHUNK, self.outpoint.serialize(), 36)
      bp.put(VAR_STR,      self.supportTx)
      
      if len(self.p2shMap) > 0:
         bp.put(VAR_STR,   self.p2shMap[BASE_SCRIPT])
      else:
         bp.put(UINT8, 0)

      bp.put(VAR_STR,      self.contribID)
      bp.put(VAR_STR,      toBytes(self.contribLabel))
      bp.put(UINT32,       self.sequence)
      bp.put(VAR_INT,      self.keysListed)

      for i in range(self.keysListed):
         bp.put(VAR_STR,      self.pubKeys[i])
         if not self.isSegWit:
            bp.put(VAR_STR,      self.signatures[i])
         else:
            bp.put(VAR_STR,      self.witnessData)
         bp.put(VAR_STR,      self.wltLocators[i])

      if len(self.p2shMap) > 1:
         p2sh_bp = BinaryPacker()

         p2sh_bp.put(VAR_INT, len(self.p2shMap) - 1)
         for key in self.p2shMap:
            if key == BASE_SCRIPT:
               continue

            val = self.p2shMap[key]
            
            p2sh_bp.put(VAR_STR, key)
            p2sh_bp.put(VAR_STR, val)

         bp.put(UINT8, TXIN_EXT_P2SHSCRIPT)
         bp.put(VAR_STR, p2sh_bp.getBinaryString())

      return bp.getBinaryString()


   #############################################################################
   def unserialize(self, rawBinaryData, skipMagicCheck=False):
      
      p2shMap = {}

      bu = BinaryUnpacker(rawBinaryData)
      version    = bu.get(UINT32)
      magic      = bu.get(BINARY_CHUNK, 4)
      outpt      = bu.get(BINARY_CHUNK, 36)
      suppTx     = bu.get(VAR_STR)
      
      p2shScr    = bu.get(VAR_STR)
      if len(p2shScr) > 0:
         p2shMap[BASE_SCRIPT] = p2shScr
      
      contrib    = bu.get(VAR_STR)
      contribLbl = toUnicode(bu.get(VAR_STR))
      seq        = bu.get(UINT32)
      nEntry     = bu.get(VAR_INT)

      pubMap, sigList, locList = {},[],[]
      for i in range(nEntry):
         pub = bu.get(VAR_STR)
         sigList.append([i, bu.get(VAR_STR)])
         locList.append([i, bu.get(VAR_STR)])
         
         if p2shScr == None or len(p2shScr) == 0:
            scrAddr = ADDRBYTE+hash160(pub)
         else:
            scrAddr = P2SHBYTE+hash160(p2shScr)
         pubMap[scrAddr] = pub
         
      while bu.getRemainingSize() > 0:
         prefix = bu.get(UINT8)
         
         if prefix == TXIN_EXT_P2SHSCRIPT:
            _len = bu.get(VAR_INT)
            count = bu.get(VAR_INT)
            for i in range(0, count):
               key = bu.get(VAR_STR)
               val = bu.get(VAR_STR)
               p2shMap[key] = val
         else:
            _len = bu.get(VAR_INT)
            bu.advance(_len)


      if not outpt[:32] == hash256(suppTx):
         raise UnserializeError('OutPoint hash does not match supporting tx')

      if not magic==MAGIC_BYTES and not skipMagicCheck:
         LOGERROR('WRONG NETWORK!')
         LOGERROR('   MAGIC BYTES:  ' + magic)
         LOGERROR('   Expected:     ' + MAGIC_BYTES)
         raise NetworkIDError('Network magic bytes mismatch')


      if not version==UNSIGNED_TX_VERSION:
         LOGWARN('Unserialing UnsignedTxInput of different version')
         LOGWARN('   USTX    Version: %d' % version)
         LOGWARN('   Armory  Version: %d' % UNSIGNED_TX_VERSION)

      self.__init__(suppTx,
                    binary_to_int(outpt[-4:]),
                    p2shMap,
                    pubMap,
                    sigList,
                    locList,
                    contrib,
                    contribLbl,
                    seq,
                    version)

      return self

   #############################################################################
   def evaluateSigningStatus(self, signerObj):
      inputSignStatus = signerObj.getSignedStateForInput(self.inputID)
      signStatus = InputSigningStatus()

      signStatus.M = inputSignStatus.proto.mCount
      signStatus.N = inputSignStatus.proto.nCount
      signStatus.statusM = [TXIN_SIGSTAT.NO_SIGNATURE]*signStatus.M
      signStatus.statusN = [TXIN_SIGSTAT.NO_SIGNATURE]*signStatus.N

      # First evaluate if we have sigs for each key, or if we *COULD* sign
      # This simply returns signed-or-notsigned if no wallet is supplied
      signStatus.wltIsRelevant = False
      signStatus.wltCanSign    = False

      pubkeyList = inputSignStatus.getPubKeyList()
      for i in range(signStatus.N):
         pubkey = pubkeyList[i]

         if inputSignStatus.isSignedForPubKey(pubkey):
            signStatus.statusN[i] = TXIN_SIGSTAT.ALREADY_SIGNED

      # Now we sort the results and compare to M-value to get high-level metrics
      # SIGSTAT enumeration values sort the way we ultimately want to display
      signStatus.statusM = sorted(signStatus.statusN)[:signStatus.M]
   
      # Since values are sorted, the last element tells us whether we're done
      signStatus.allSigned = (signStatus.statusM[-1] in \
         [TXIN_SIGSTAT.ALREADY_SIGNED, TXIN_SIGSTAT.WLT_ALREADY_SIGNED])

      signStatus.wltCanComplete = (signStatus.statusM[-1] == TXIN_SIGSTAT.WLT_CAN_SIGN)

      return signStatus


   #############################################################################
   def pprint(self, indent=3):
      ind = ' '*indent
      txHashStr = binary_to_hex(self.outpoint.txHash, BIGENDIAN)[:8]
      txoIdx    = self.outpoint.txOutIndex
      scrType   = CPP_TXOUT_SCRIPT_NAMES[self.scriptType]

      print('UnsignedTxInput --  %s:%d (%s)' % (txHashStr, txoIdx, scrType))

   #############################################################################
   def getUnspentTxOut(self):
      from armoryengine.CoinSelection import PyUnspentTxOut
      return PyUnspentTxOut(
            txHash = self.outpoint.txHash,
            txoIdx = self.outpoint.txOutIndex,
            val=self.value,
            fullScript=self.txoScript)
   
   #############################################################################
   def getP2shMapForSigner(self):
      p2sh_map = {}

      if not isinstance(self.p2shMap, dict):
         return p2sh_map

      if len(self.p2shMap) == 0:
         return p2sh_map

      txoScrAddr = binary_to_hex(script_to_scrAddr(self.txoScript)[1:])
      for key, val in self.p2shMap.iteritems():
         if key == BASE_SCRIPT:
            p2sh_map[txoScrAddr] = val
            continue
         p2sh_map[key] = val

         #special case for p2wsh
         if len(key) == 34 and key[0] == '\x00' and key[1] == '\x20':
            try:
               key_hex = binary_to_hex(key[2:])
               p2sh_map[key_hex] = val
            except:
               continue

      return p2sh_map




################################################################################
class NullAuthData(object):
   def __init__(self):
      pass

   def serialize(self):
      return ''

   def unserialize(self, s):
      return self

   def __eq__(self, nad2):
      return True

   def __ne__(self, nad2):
      return False

################################################################################
class DecoratedTxOut(AsciiSerializable):
   """
   The name is really "UnsignedTx" output ... it is an output of an unsignedTx

   self.authData can be anything that the offline computer will recognize
   as authentication of the owner of the txOut script.  This could be our
   planned rootkey + multiplier technique (or multiple root keys and
   multipliers, in multisig), or it could be a chain of X509 certs that
   link an ID to this script.

   As of this writing, we're not utilizing any of these auth techniques,
   yet, so we simply assume it will be None, or that it has a .serialize()
   and .unserialize() method so this class doesn't have to care

   ContribID and ContribLabel are optional fields that help in Simulfunding
   situations, where there may be tons of inputs and outputs (change) that 
   would otherwise appear unrelated.  We need a way to associate them so we 
   can display intelligent stuff to the user.
   """
   def __init__(self, script=None, value=None, p2sh=None,
                      wltLocator=None, authMethod='NONE', authData=None,
                      contribID=None, contribLabel=None, 
                      version=UNSIGNED_TX_VERSION):

      if None in [script, value]:
         self.isInitialized = False
         return

      self.isInitialized = True
      self.version    = version
      self.binScript  = script
      self.value      = value
      self.p2shScript = p2sh if p2sh else ''
      self.wltLocator = wltLocator if wltLocator else ''
      self.authMethod = authMethod
      self.authData   = authData if authData else NullAuthData()
      self.contribID  = contribID if contribID else ''
      self.contribLabel = contribLabel if contribLabel else ''

      # Derived values
      self.scrAddr    = script_to_scrAddr(script)
      self.scriptType = getTxOutScriptType(script)
      self.multiInfo  = {}

      # P2SH destinations don't *require* the subscript like USTXI do
      baseScript = self.binScript
      if p2sh and self.scriptType == CPP_TXOUT_P2SH:
         self.scriptType = getTxOutScriptType(p2sh)
         baseScript = p2sh


      if self.scriptType == CPP_TXOUT_MULTISIG:
         M, N, a160s, pubs = getMultisigScriptInfo(baseScript)
         self.multiInfo['M'] = M
         self.multiInfo['N'] = N
         self.multiInfo['Addr160s'] = a160s
         self.multiInfo['PubKeys'] = pubs

      if self.scriptType in [CPP_TXOUT_P2WPKH, CPP_TXOUT_P2WSH]:
         self.version = version | 0xFF000000

   #############################################################################
   def setAuthData(self, authType, authObj):
      self.authMethod = authType
      self.authData   = authObj

   #############################################################################
   def setWltLocator(self, wltLocStr):
      self.wltLocator = wltLocStr

   #############################################################################
   def toJSONMap(self):
      """
      No lite version needed, since these are usually very small.

      Below is the list of supported TXOUT types defined around line 500 in 
      armoryengine/ArmoryUtils.py.  This list will probably not be maintained
      as regularly as the one in ArmoryUtils.py, since these comments are not
      required to be accurate for the code to be functional.  Please confirm
      that it matches the ArmoryUtils list for the current version of Armory
      before relying on it.

      CPP_TXOUT_STDHASH160   = 0
      CPP_TXOUT_STDPUBKEY65  = 1
      CPP_TXOUT_STDPUBKEY33  = 2
      CPP_TXOUT_MULTISIG     = 3
      CPP_TXOUT_P2SH         = 4
      CPP_TXOUT_NONSTANDARD  = 5

      CPP_TXOUT_HAS_ADDRSTR  = [CPP_TXOUT_STDHASH160, 
                                CPP_TXOUT_STDPUBKEY65,
                                CPP_TXOUT_STDPUBKEY33,
                                CPP_TXOUT_P2SH]

      CPP_TXOUT_STDSINGLESIG = [CPP_TXOUT_STDHASH160, 
                                CPP_TXOUT_STDPUBKEY65,
                                CPP_TXOUT_STDPUBKEY33]

      CPP_TXOUT_SCRIPT_NAMES = ['']*6
      CPP_TXOUT_SCRIPT_NAMES[CPP_TXOUT_STDHASH160]  = 'Standard (PKH)'
      CPP_TXOUT_SCRIPT_NAMES[CPP_TXOUT_STDPUBKEY65] = 'Standard (PK65)'
      CPP_TXOUT_SCRIPT_NAMES[CPP_TXOUT_STDPUBKEY33] = 'Standard (PK33)'
      CPP_TXOUT_SCRIPT_NAMES[CPP_TXOUT_MULTISIG]    = 'Multi-Signature'
      CPP_TXOUT_SCRIPT_NAMES[CPP_TXOUT_P2SH]        = 'Standard (P2SH)'
      CPP_TXOUT_SCRIPT_NAMES[CPP_TXOUT_NONSTANDARD] = 'Non-Standard'
      """

      outjson = {}
      outjson['version']      = self.version
      outjson['magicbytes']   = binary_to_hex(MAGIC_BYTES)
      outjson['txoutscript']  = binary_to_hex(self.binScript)
      outjson['txoutvalue']   = self.value
      outjson['p2shscript']   = binary_to_hex(self.p2shScript)
      outjson['wltlocator']   = binary_to_hex(self.wltLocator)
      outjson['authmethod']   = self.authMethod # we expect plaintext
      outjson['authdata']     = binary_to_hex(self.authData.serialize()) # we expect this won't be
      outjson['contribid']    = self.contribID
      outjson['contriblabel'] = self.contribLabel

      # Some computed values so the caller doesn't have to deal with it
      scrType = getTxOutScriptType(self.binScript)
      outjson['scripttypeint'] = scrType  # armoryengine/ArmoryUtils.py line ~500
      outjson['scripttypestr'] = CPP_TXOUT_SCRIPT_NAMES[scrType]
      outjson['isp2sh']        = (scrType == CPP_TXOUT_P2SH)
      outjson['ismultisig']    = (scrType == CPP_TXOUT_MULTISIG)

      outjson['hasaddrstr']    = False
      outjson['addrstr']       = ''
      if scrType in CPP_TXOUT_HAS_ADDRSTR:
         outjson['hasaddrstr']    = True
         outjson['addrstr']       = script_to_addrStr(self.binScript)

      return outjson


   #############################################################################
   def fromJSONMap(self, jsonMap, skipMagicCheck=False):

      ver   = jsonMap['version'] 
      magic = hex_to_binary(jsonMap['magicbytes'])

      # Issue a warning if the versions don't match
      if not ver == UNSIGNED_TX_VERSION:
         LOGWARN('Unserialing USTX of different version')
         LOGWARN('   USTX    Version: %d' % ver)
         LOGWARN('   Armory  Version: %d' % UNSIGNED_TX_VERSION)

      # Check the magic bytes of the lockbox match
      if not magic == MAGIC_BYTES and not skipMagicCheck:
         LOGERROR('Wrong network!')
         LOGERROR('    USTX    Magic: ' + binary_to_hex(magic))
         LOGERROR('    Armory  Magic: ' + binary_to_hex(MAGIC_BYTES))
         raise NetworkIDError('Network magic bytes mismatch')

      script = hex_to_binary(jsonMap['txoutscript'])
      value  =               jsonMap['txoutvalue']
      p2sh   = hex_to_binary(jsonMap['p2shscript'])
      loc    = hex_to_binary(jsonMap['wltlocator'])
      meth   =               jsonMap['authmethod']
      data   = hex_to_binary(jsonMap['authdata'])
      cid    =               jsonMap['contribid']
      clbl   =               jsonMap['contriblabel']
      
      authData = NullAuthData().unserialize(data)
      self.__init__(script, value, p2sh, loc, meth, authData, cid, clbl)
      return self

   


   #############################################################################
   def serialize(self):
      if not self.isInitialized:
         LOGERROR('Cannot serialize an uninitialzed unsigned txin')
         return None

      bp = BinaryPacker()
      bp.put(UINT32,       self.version)
      if (self.version & 0xFF000000) == 0xFF000000:
         bp.put(UINT32, 0xDEADBEEF)

      bp.put(BINARY_CHUNK, MAGIC_BYTES)
      bp.put(VAR_STR,      self.binScript)
      bp.put(UINT64,       self.value)
      bp.put(VAR_STR,      self.p2shScript)
      bp.put(VAR_STR,      self.wltLocator)
      bp.put(VAR_STR,      self.authMethod)
      bp.put(VAR_STR,      self.authData.serialize())
      bp.put(VAR_STR,      self.contribID)
      bp.put(VAR_STR,      self.contribLabel)

      return bp.getBinaryString()



   #############################################################################
   def unserialize(self, rawData, skipMagicCheck=False):
      bu = BinaryUnpacker(rawData)
      version    = bu.get(UINT32)
      if (version & 0xFF000000) == 0xFF000000:
         bu.get(UINT32)

      magic      = bu.get(BINARY_CHUNK, 4)
      script     = bu.get(VAR_STR)
      value      = bu.get(UINT64)
      p2shScr    = bu.get(VAR_STR)
      wltLoc     = bu.get(VAR_STR)
      authMeth   = bu.get(VAR_STR)
      authData   = bu.get(VAR_STR)
      contribID  = bu.get(VAR_STR)
      contribLBL = toUnicode(bu.get(VAR_STR))

      if not magic==MAGIC_BYTES and not skipMagicCheck:
         LOGERROR('WRONG NETWORK!')
         LOGERROR('   MAGIC BYTES:  ' + magic)
         LOGERROR('   Expected:     ' + MAGIC_BYTES)
         raise NetworkIDError('Network magic bytes mismatch')


      if not (version & 0x00FFFFFF) == UNSIGNED_TX_VERSION:
         LOGWARN('Unserialing UnsignedTxInput of different version')
         LOGWARN('   USTX    Version: %d' % version)
         LOGWARN('   Armory  Version: %d' % UNSIGNED_TX_VERSION)


      authDataObj = NullAuthData().unserialize(authData)

      self.__init__(script, value, p2shScr, wltLoc, 
                             authMeth, authDataObj, contribID, contribLBL, version)
      return self


   #############################################################################
   def pprint(self, indent=3):
      ind = ' '*indent
      scrType   = CPP_TXOUT_SCRIPT_NAMES[self.scriptType]

      print(ind + 'Version:     ', self.version)
      if self.scriptType in CPP_TXOUT_HAS_ADDRSTR:
         print(ind + 'Address:     ', scrAddr_to_addrStr(self.scrAddr))
         if self.p2shScript:
            print(ind + 'P2SH Script: ', binary_to_hex(self.p2shScript)[:50])

      elif self.scriptType==CPP_TXOUT_MULTISIG:
         print(ind + 'Multisig:      %(M)s-of-%(N)s' % self.multiInfo)
      print(ind + 'Value:       ', coin2strNZS(self.value))
      print(ind + 'ContribID:   ', self.contribID)
      print(ind + 'ContribLabel:', self.contribLabel)


   #############################################################################
   def __eq__(self, obj2):

      if not isinstance(obj2, self.__class__):
         return False

      compareAttrs = ['version', 'binScript', 'value', 'p2shScript',
                      'wltLocator', 'authMethod', 'contribID', 'contribLabel',
                      'scrAddr', 'scriptType', 'multiInfo']

      compareLists = []
      compareMaps  = []

      for attr in compareAttrs:
         if not getattr(self, attr) == getattr(obj2, attr):
            LOGERROR('Compare failed for attribute: %s' % attr)
            LOGERROR('  self:   %s' % str(getattr(self,attr)))
            LOGERROR('  other:  %s' % str(getattr(obj2,attr)))
            return False


      for attr in compareLists:
         selfList  = getattr(self, attr)
         otherList = getattr(obj2, attr)
      
         if not len(selfList)==len(otherList):
            LOGERROR('List size compare failed for %s' % attr)
            return False

         i = -1
         for a,b in zip(selfList, otherList):
            i+=1
            if not a==b:
               LOGERROR('Failed list compare for attr %s, index %d' % (attr,i))
               return False
            
      return True


   def __ne__(self, obj2):
      return not self.__eq__(obj2)




################################################################################
################################################################################
# This class can be used for both multi-signature tx collection, as well as
# offline wallet signing (you are collecting signatures for a 1-of-1 tx only
# involving yourself).
class UnsignedTransaction(AsciiSerializable):
   """
   Let's call this a "USTX" to avoid confusion with "UTXO"s which are
   "unSPENT TxOut"s.

   UnsignedTransaction is basically a PyTx() object that has all the
   metadata needed for an offline device to sign it.

   The contribID is basically just the promissory note ID, if this was
   constrcuted from a collection of promissory notes.  For instance,
   we have 100 inputs to a tx, but only two contributors.  If only 12
   of those inputs have signatures (so far), then it would be good to
   know that, say, there's only two other people that need to provide
   sigs, not 88.
   """

   OBJNAME   = "UnsignedTx"
   BLKSTRING = "TXSIGCOLLECT"
   EMAILSUBJ = 'Armory Multi-sig Transaction to Sign - %s'
   EMAILBODY = """
               The chunk of text below is a proposed spending transaction 
               with all signatures available so far.  Open
               the Lockbox manager in Armory and click on "Review and Sign" 
               in the bottom row of the dashboard.  Copy this text into the
               import box, including the first and last lines.  You will be
               given the opportunity to confirm the transaction before 
               signing.  After it is signed, click "Export" in the bottom-right
               corner and send it back to me."""
           
   EQ_ATTRS_SIMPLE = ['version', 'lockTime', 'asciiID']
   EQ_ATTRS_LISTS  = ['ustxInputs', 'decorTxOuts']
               

   #############################################################################
   def __init__(self, pytx=None, pubKeyMap=None, txMap=None, p2shMap=None,
                                       version=UNSIGNED_TX_VERSION):
      self.version         = version
      self.pytxObj         = UNINITIALIZED
      self.uniqueIDB58     = ''
      self.asciiID         = ''  # need a common name for all ser/unser classes
      self.lockTime        = 0
      self.ustxInputs  = []
      self.decorTxOuts = []
      self.isLegacyTx = True

      self.signer = None

      txMap   = {} if txMap   is None else txMap
      p2shMap = {} if p2shMap is None else p2shMap

      if pytx:
         self.createFromPyTx(pytx, pubKeyMap, txMap, p2shMap)

   #############################################################################
   def computeUniqueIDB58(self, rawTxNoSigs):

      #force 0 locktime when computing tx unique ID for backwards compatibility
      rawSigNoLockTime = rawTxNoSigs[0:-4]
      rawSigNoLockTime += int_to_binary(0, 4)
      
      return binary_to_base58(hash256(rawSigNoLockTime))[:8]

   #############################################################################
   def setupSigner(self):
      if self.signer != None:
         return

      self.signer = SignerObject()
      self.signer.setup()
      self.signer.setVersion(self.version)
      self.signer.setLockTime(self.lockTime)

      for txin in self.pytxObj.inputs:
         self.signer.addSpenderByOutpoint(
            txin.outpoint.txHash,
            txin.outpoint.txOutIndex,
            txin.intSeq)
         self.signer.populateUtxo(
            txin.outpoint.txHash,
            txin.outpoint.txOutIndex,
            txin.outpointValue,
            txin.binScript)

         if txin.supportTx != None:
            self.signer.addSupportingTx(txin.supportTx)

      for txout in self.pytxObj.outputs:
         self.signer.addRecipient(txout.value, txout.binScript)

   #############################################################################
   def createFromUnsignedTxIO(self, ustxinList, dtxoList, lockTime=0):
      """
      All custom sequence numbers are set in the individual ustxi entries
      """

      nIn  = len(ustxinList)
      nOut = len(dtxoList)

      if self.pytxObj == UNINITIALIZED:
         self.pytxObj = PyTx()
         self.pytxObj.version  = UNSIGNED_TX_VERSION
         self.pytxObj.lockTime = lockTime
         self.lockTime = lockTime
         self.pytxObj.inputs   = [None]*nIn
         self.pytxObj.outputs  = [None]*nOut

         for iin,ustxi in enumerate(ustxinList):
            self.pytxObj.inputs[iin] = PyTxIn()
            self.pytxObj.inputs[iin].outpoint      = ustxi.outpoint
            self.pytxObj.inputs[iin].binScript     = ustxi.txoScript
            self.pytxObj.inputs[iin].intSeq        = ustxi.sequence
            self.pytxObj.inputs[iin].outpointValue = ustxi.value
            self.pytxObj.inputs[iin].supportTx     = ustxi.supportTx

         for iout,dtxo in enumerate(dtxoList):
            self.pytxObj.outputs[iout] = PyTxOut()
            self.pytxObj.outputs[iout].value     = dtxo.value
            self.pytxObj.outputs[iout].binScript = dtxo.binScript

      # Create copies of the input lists
      self.ustxInputs = ustxinList[:]
      self.decorTxOuts = dtxoList[:]
      
      # Identify input types
      self.isLegacyTx = True
      for ustxi in self.ustxInputs:
         self.isLegacyTx = self.isLegacyTx and ustxi.isLegacyScript

      # Finally issue a warning if this selection is super high fee
      totalIn  = sum([ustxi.value for ustxi in ustxinList])
      totalOut = sum([dtxo.value  for dtxo  in dtxoList  ])
      if totalIn - totalOut > 100*MIN_RELAY_TX_FEE:
         LOGWARN('Exceptionally high fee in createFromUnsignedTxIO')
         LOGWARN('TotalInputs  = %s BTC', coin2strNZS(totalIn))
         LOGWARN('TotalOutputs = %s BTC', coin2strNZS(totalOut))
         LOGWARN('Computed Fee = %s BTC', coin2strNZS(totalIn-totalOut))
      elif totalIn - totalOut < 0:
         raise ValueError('Supplied inputs are less than the supplied outputs')

      self.setupSigner()
      rawTxNoSigs = self.pytxObj.serialize()
      self.uniqueIDB58 = self.computeUniqueIDB58(rawTxNoSigs)
      self.asciiID = self.uniqueIDB58

      return self

   #############################################################################
   def createFromPyTx(self, pytx, pubKeyMap=None, txMap=None, p2shMap=None):
      """
      It might seem silly to convert pytx into USTXIs and DecoratedTxos, just
      to call the other method that reconstructs the same pytx object.  At
      least it all goes through the same construction method.
      """

      pubKeyMap   = {} if not pubKeyMap else pubKeyMap
      txMap       = {} if not txMap     else txMap
      p2shMap     = {} if not p2shMap   else p2shMap


      if len(txMap)==0 and not TheBDM.getState()==BDM_BLOCKCHAIN_READY:
         # TxDP includes the transactions that supply the inputs to this
         # transaction, so the BDM needs to be available to fetch those.
         raise BlockchainUnavailableError('Must input supporting transactions '
                                            'or access to the blockchain, to '
                                            'create the TxDP')

      ustxiList = []
      dtxoList = []

      # Get support tx for each input and create unsignedTx-input for each
      prevTxs = {}
      missingTxHashes = []
      for txin in pytx.inputs:
         # First, make sure that we have the previous Tx data available
         # We can't continue without it, since BIP 0010 will now require
         # the full tx of outputs being spent
         outpt = txin.outpoint
         txhash = outpt.txHash
         if not txhash in prevTxs:
            prevTxs[txhash] = {
               'txOuts' : [],
               'pyTx' : None
            }
         prevTxs[txhash]['txOuts'].append(outpt.txOutIndex)
         if not prevTxs[txhash]['pyTx']:
            if txhash in txMap:
               prevTxs[txhash]['pyTx'] = txMap[txhash].copy()
            else:
               missingTxHashes.append(txhash)

      if missingTxHashes:
         #try to get txdata from db for missing hashes
         if not TheBDM.getState()==BDM_BLOCKCHAIN_READY:
            raise InvalidScriptError('No previous-tx data available for TxDP')

         capnTxns = TheBridge.service.getTxsByHash(missingTxHashes)
         for capnTx in capnTxns:
            if not capnTx.hash in prevTxs:
               raise InvalidHashError('Could not find the referenced tx')
            prevTxs[capnTx.hash]['pyTx'] = PyTx().unserialize(capnTx.raw)

      count = 0
      #populate input list with tx data
      for txHash in prevTxs:
         pyPrevTx = prevTxs[txHash]['pyTx']
         if not pyPrevTx:
            raise InvalidScriptError('No previous-tx data available for TxDP')
         serializedTx = pyPrevTx.serializeWithoutWitness()

         for txoIdx in prevTxs[txHash]['txOuts']:
            ustxiList.append(UnsignedTxInput(
               serializedTx,
               txoIdx, {},
               pubKeyMap,
               sequence=txin.intSeq, inputID=count))
            count = count + 1


      # Create the DecoratedTxOut for each output.  Without any
      # supplemental auth info, this conversion isn't necessarily useful.
      for txout in pytx.outputs:
         scr = txout.getScript()
         val = txout.getValue()
         p2sh = p2shMap.get(script_to_scrAddr(scr)) # returns None if not P2SH

         # Append to the dtxo list
         dtxoList.append(DecoratedTxOut(scr, val, p2sh))

      return self.createFromUnsignedTxIO(ustxiList, dtxoList, pytx.lockTime)


   #############################################################################
   def createFromTxOutSelection(self, utxoSelection, scriptValuePairs,
                                pubKeyMap=None, txMap=None, p2shMap=None,
                                lockTime=0):

      totalUtxoSum = sumTxOutList(utxoSelection)
      totalOutputSum = sum([a[1] for a in scriptValuePairs])
      if not totalUtxoSum >= totalOutputSum:
         raise UstxError('More outputs than inputs!')

      thePyTx = PyTx()
      thePyTx.version = UNSIGNED_TX_VERSION
      thePyTx.lockTime = lockTime
      thePyTx.inputs = []
      thePyTx.outputs = []

      # We can prepare the outputs, first
      for script,value in scriptValuePairs:
         txout = PyTxOut()
         txout.value = value

         # Assume recipObj is either a PBA or a string
         if isinstance(script, PyBtcAddress):
            LOGERROR("Didn't know any func was still using this conditional")

         intType = getTxOutScriptType(script)
         if intType==CPP_TXOUT_NONSTANDARD:
            LOGWARN('Including non-standard script output')
            LOGWARN('Script: ' + binary_to_hex(script))
            raise BadAddressError('Invalid script for tx creation')

         txout.binScript = script[:]
         thePyTx.outputs.append(txout)

      # Prepare the inputs based on the utxo objects
      for iin,utxo in enumerate(utxoSelection):
         # First, make sure that we have the previous Tx data available
         # We can't continue without it, since BIP 0010 will now require
         # the full tx of outputs being spent
         txin = PyTxIn()
         txin.outpoint = PyOutPoint()
         txin.binScript = ''
         txin.intSeq = utxo.sequence
         txhash = utxo.getTxHash()
         txoIdx  = utxo.getTxOutIndex()
         txin.outpoint.txHash = txhash
         txin.outpoint.txOutIndex = txoIdx
         txin.outpointValue = utxo.val
         thePyTx.inputs.append(txin)

      return self.createFromPyTx(thePyTx, pubKeyMap, txMap, p2shMap)


   #############################################################################
   def createFromUnsignedTxInputSelection(self, ustxiList, scriptValuePairs,
                                                    p2shMap=None, lockTime=0):
      dtxoList = []
      if p2shMap is None:
         p2shMap = {}

      for scr,val in scriptValuePairs:
         p2sh = p2shMap.get(script_to_scrAddr(scr))   # Returns None if DNE
         dtxoList.append(DecoratedTxOut(scr,val,p2sh))

      return self.createFromUnsignedTxIO(ustxiList, dtxoList, lockTime)

   #############################################################################
   def createFromTxSigCollect(self, txSigCollect):
      if self.signer != None:
         raise Exception("Initialized signer, cannot overwrite")

      self.signer = SignerObject()
      self.signer.setup()
      self.signer.fromTxSigCollect(txSigCollect)
      self.pytxObj = self.signer.getUnsignedTx()

      return self.createFromPyTx(self.pytxObj)

   #############################################################################
   def calculateFee(self):
      totalIn  = sum([ustxi.value for ustxi in self.ustxInputs ])
      totalOut = sum([dtxo.value  for dtxo  in self.decorTxOuts])
      return totalIn-totalOut


   #############################################################################
   def serialize(self, ustxType=USTX_TYPE_MODERN):
      """
      TODO:  We should consider the idea that we don't even need to serialize
             the pytxObj at all... it seems there should only be a single,
             canonical way to construct the tx.
      """
      if self.pytxObj==UNINITIALIZED:
         LOGERROR('Cannot serialize an uninitialized tx')
         return None

      return self.signer.toTxSigCollect(ustxType)


   #############################################################################
   def unserialize(self, txSigCollect):
      self.createFromTxSigCollect(txSigCollect)
      return self


   #############################################################################
   def toJSONMap(self, lite=False):

      if self.pytxObj==UNINITIALIZED:
         LOGERROR('Cannot serialize an uninitialized tx')
         raise ValueError('Cannot serialize an uninitialized tx')

      outjson = {}
      outjson['version'] = self.version
      outjson['magicbytes'] = binary_to_hex(MAGIC_BYTES)
      outjson['id'] = self.uniqueIDB58
      outjson['locktimeint'] = self.lockTime
      if self.lockTime < 500000000:
         outjson['locktimeblock'] = self.lockTime
         outjson['locktimedate']  = ''
      else:
         outjson['locktimeblock'] = -1
         outjson['locktimedate']  = unixTimeToFormatStr(self.lockTime)
   
      outjson['numinputs'] = len(self.ustxInputs)
      outjson['numoutputs'] = len(self.decorTxOuts)

      totalIn  = sum([ustxi.value for ustxi in self.ustxInputs ])
      totalOut = sum([dtxo.value  for dtxo  in self.decorTxOuts])
      totalFee = totalIn-totalOut

      outjson['suminputs']  = totalIn
      outjson['sumoutputs'] = totalOut
      outjson['fee']        = totalFee

      if lite:
         return outjson

      outjson['inputs'] = []
      for ustxi in self.ustxInputs:
         outjson['inputs'].append(ustxi.toJSONMap())

      outjson['outputs'] = []
      for dtxo in self.decorTxOuts:
         outjson['outputs'].append(dtxo.toJSONMap())
      
      return outjson


   #############################################################################
   def fromJSONMap(self, jsonMap, skipMagicCheck=False):
      

      if not 'inputs' in jsonMap:
         raise UnserializeError('Incomplete unsigned transaction map')

      ver   = jsonMap['version'] 
      magic = hex_to_binary(jsonMap['magicbytes'])
      uniq  = jsonMap['id']
      tlock = jsonMap['locktimeint'] 
   
      # Issue a warning if the versions don't match
      if not ver == UNSIGNED_TX_VERSION:
         LOGWARN('Unserialing USTX of different version')
         LOGWARN('   USTX    Version: %d' % ver)
         LOGWARN('   Armory  Version: %d' % UNSIGNED_TX_VERSION)

      # Check the magic bytes of the lockbox match
      if not magic == MAGIC_BYTES and not skipMagicCheck:
         LOGERROR('Wrong network!')
         LOGERROR('    USTX    Magic: ' + binary_to_hex(magic))
         LOGERROR('    Armory  Magic: ' + binary_to_hex(MAGIC_BYTES))
         raise NetworkIDError('Network magic bytes mismatch')

      ustxiList = []
      count=0
      for ustxi in jsonMap['inputs']:
         ustxiList.append(UnsignedTxInput(inputID=count).fromJSONMap(ustxi, skipMagicCheck))
         count = count + 1

      dtxoList = []
      for dtxo in jsonMap['outputs']:
         dtxoList.append(DecoratedTxOut().fromJSONMap(dtxo, skipMagicCheck))

      self.createFromUnsignedTxIO(ustxiList, dtxoList, tlock)

      return self


   #############################################################################
   def evaluateSigningStatus(self):
      txSigStat = TxSigningStatus()
      txSigStat.numInputs = len(self.ustxInputs)
      txSigStat.statusList = \
         [ustxi.evaluateSigningStatus(self.signer) for ustxi in self.ustxInputs]

      txSigStat.canBroadcast   = True
      txSigStat.wltCanSign     = False
      txSigStat.wltIsRelevant  = False
      txSigStat.wltCanComplete = True
      txSigStat.wltAlreadySigned = False
      #txSigStat.wltPartialSigned = False

      for inputStat in txSigStat.statusList:
         if not inputStat.allSigned:
            txSigStat.canBroadcast = False

         if inputStat.wltCanSign:
            txSigStat.wltCanSign = True

         if inputStat.wltIsRelevant:
            txSigStat.wltIsRelevant = True

         if not inputStat.wltCanComplete:
            txSigStat.wltCanComplete = False

         for statCode in inputStat.statusN:
            # WLT_CAN_SIGN is true only if it's not signed yet.  
            # Therefore, if we run into with other WLT_ALREADY_SIGNED
            # that means we're in a partial state (not sure how that happens)
            #wltCanSign = statCode == TXIN_SIGSTAT.WLT_CAN_SIGN
            wltAlready = statCode == TXIN_SIGSTAT.WLT_ALREADY_SIGNED
            #if (wltCanSign and txSigStat.wltAlreadySigned) or \
               #(wltAlready and txSigStat.wltCanSign):
               #txSigStat.wltPartialSigned = True
               #break

            if wltAlready: 
               txSigStat.wltAlreadySigned = True
               break

      return txSigStat

   #############################################################################
   def verifySigsAllInputs(self):
      for ustxi in self.ustxInputs:
         if not ustxi.verifyAllSignatures(self.signer):
            return False
      return True
      
   #############################################################################
   def getUnsignedPyTx(self, doVerifySigs=True):
      return self.pytxObj.copy()

   #############################################################################
   def getPyTxSignedIfPossible(self, doVerifySigs=True):
      if self.evaluateSigningStatus().canBroadcast:
         return self.getSignedPyTx()
      else:
         return self.getUnsignedPyTx()


   #############################################################################
   def isSigValidForInput(self, txInIndex, sigStr, pubKey=None):
      """
      This returns the multi-sig index
      """

      if txInIndex >= len(self.ustxInputs):
         raise SignatureError('TxIn index is out of range for this USTX')

      ustxi = self.ustxInputs[txInIndex]
      return ustxi.verifyTxSignature(self.pytxObj, sigStr, pubKey)


   #############################################################################
   def createAndInsertSignatureForInput(self, txInIndex, sbdPrivKey, hashcode=1):
      if txInIndex >= len(self.ustxInputs):
         raise SignatureError('TxIn index is out of range for this USTX')

      ustxi = self.ustxInputs[txInIndex]
      ustxi.createAndInsertSignature(self.pytxObj, sbdPrivKey, hashcode)


   #############################################################################
   def insertSignatureForInput(self, txInIndex, sigStr, pubKey=None):
      ustxi = self.ustxInputs[txInIndex]
      sigIndex = ustxi.getValidIndexForSignature(self.pytxObj, sigStr, pubKey)
      if sigIndex >= 0:
         ustxi.setSignature(sigIndex, sigStr)
         return sigIndex

      return -1

   #############################################################################
   def insertSignature(self, sigStr, pubKey=None):
      if pubKey is None or len(self.ustxInputs)>5:
         LOGWARN('Inserting sig without input index and/or pubkey will be SLOW!')

      for iin,ustxi in enumerate(self.ustxInputs):
         msIdx = ustxi.insertSignature(sigStr, pubKey)
         if msIdx >= 0:
            return msIdx

      return -1

   #############################################################################
   def getSignedPyTx(self):
      return self.signer.getSignedTx()

   #############################################################################
   def getBroadcastTxIfReady(self):
      try:
         return self.getSignedPyTx()
      except SignatureError as msg:
         return None
      except KeyError:
         return None

   #############################################################################
   def pprint(self, indent=3):
      ind = ' '*indent
      tx = self.pytxObj
      txHash = hash256(tx.serialize())
      print(ind+'UnsignedTx ID: ', self.uniqueIDB58)
      print(ind+'Curr TxID    : ', binary_to_hex(txHash, BIGENDIAN))
      print(ind+'Version      : ', tx.version)
      print(ind+'Lock Time    : ', tx.lockTime)
      print(ind+'Fee (BTC)    : ', coin2strNZS(self.calculateFee()))
      print(ind+'#Inputs      : ', len(tx.inputs))

      for i,ustxi in enumerate(self.ustxInputs):
         prevHash  = binary_to_hex(ustxi.outpoint.txHash, BIGENDIAN)[:8]
         prevIdx   = ustxi.outpoint.txOutIndex
         typeName  = CPP_TXOUT_SCRIPT_NAMES[ustxi.scriptType]
         value     = coin2str(ustxi.value).lstrip().rjust(12)
         M,N       = ustxi.sigsNeeded, ustxi.keysListed
         contrib   = '(%s)'%ustxi.contribID if ustxi.contribID else ''
         pubKeySz  = '(' + ' '.join([str(len(s)) for s in ustxi.pubKeys]) + ')'

         printStr  = ' '*2*indent
         printStr += '%(prevHash)s:%(prevIdx)d / ' % locals()
         printStr += '(M=%(M)d, N=%(N)d) / '  % locals()
         printStr += '%(value)s / %(contrib)s'  % locals()
         printStr += 'PubSz: ' + pubKeySz
         print(printStr)

      print(ind+'#Outputs     : ', len(tx.outputs))
      for i,txout in enumerate(tx.outputs):
         dtxo = self.decorTxOuts[i]
         addrDisp = getTxOutScriptDisplayStr(txout.binScript)
         valDisp = coin2str(txout.value, maxZeros=2)
         print(' '*2*indent + 'Recip:', addrDisp.ljust(35), end=' ')
         print(valDisp, 'BTC', end=' ')
         print(('(%s)' % dtxo.contribID) if dtxo.contribID else '')

   #############################################################################
   def isSegWit(self):
      for ustxi in self.ustxInputs:
         if ustxi.isSegWit:
            return True
         
      return False
         
   #############################################################################
   def getSigCount(self):
      signStat = self.evaluateSigningStatus()

      # Now check that all the raw signatures are actually value
      numSig = 0  # we'll double check sufficient sigs
      for inputStatus in signStat.statusList:
         for i in range(inputStatus.N):
            if inputStatus.statusN[i] in [TXIN_SIGSTAT.ALREADY_SIGNED, \
                                    TXIN_SIGSTAT.WLT_ALREADY_SIGNED]:
               numSig +=1
      return numSig
   
   #############################################################################
   def addOpReturnOutput(self, msgBin):
      if self.getSigCount() > 0:
         raise Exception("Tx is already Signed")
      
      #op_return
      bp = BinaryPacker()
      bp.put(UINT8, OP_RETURN)
      
      #op_pushdata and message
      msgLen = len(msgBin)
      if msgLen > 80:
         raise InvalidScriptError("msg for OP_RETURN cannot exceed 80 bytes")
      elif msgLen > 0 and msgLen < 76:
         bp.put(UINT8, msgLen)
         bp.put(BINARY_CHUNK, msgBin)
      elif msgLen > 75:
         bp.put(UINT8, OP_PUSHDATA1)
         bp.put(UINT8, msgLen)
         bp.put(BINARY_CHUNK, msgBin)
         
      binScript = bp.getBinaryString()
      
      #add to PyTx object
      iout = len(self.pytxObj.outputs)
      self.pytxObj.outputs.append(PyTxOut())
      self.pytxObj.outputs[iout].value     = 0
      self.pytxObj.outputs[iout].binScript = binScript

      #add to UnsignedTransaction object
      self.decorTxOuts.append(DecoratedTxOut(script=binScript, value=0))
      
      #update USTX id
      rawTxNoSigs = self.pytxObj.serialize()
      self.uniqueIDB58 = self.computeUniqueIDB58(rawTxNoSigs)
      self.asciiID = self.uniqueIDB58

   #############################################################################
   def resolveSigner(self, wltId):
      self.signer.resolve(wltId)

   #############################################################################
   def signTx(self, wltId, callback, parentDlg=None):
      self.signer.signTx(wltId, callback, parentDlg)

#############################################################################
def getFeeForTx(txHash):
   if TheBDM.getState()==BDM_BLOCKCHAIN_READY:
      try:
         tx = PyTx().unserialize(TheBridge.service.getTxByHash(txHash).raw)
         if not tx.isInitialized():
            LOGERROR('Attempted to get fee for tx we don\'t have...?  %s', \
                                                binary_to_hex(txHash,BIGENDIAN))
            return 0, 0
         valIn, valOut = 0,0
         for i in range(len(tx.inputs)):
            outpoint = tx.inputs[i].getOutPoint()
            parentTx = PyTx().unserialize(TheBridge.getTxByHash(outpoint.txHash).raw)
            if not parentTx.isInitialized():
               LOGERROR('[getFeeForTx] Couldnt resolve parent')
               return 0, 0
            valIn += parentTx.outputs[outpoint.txOutIndex].getValue()
         for i in range(len(tx.outputs)):
            valOut += tx.outputs[i].getValue()
         fee = valIn - valOut
         txWeight = tx.getTxWeight()
         fee_byte = fee / float(txWeight)
         return fee, fee_byte
      except:
         LOGERROR('Couldn\'t get tx fee. Ignore this message in Fullnode') 
         return 0, 0

#############################################################################
def determineSentToSelfAmt(le, wlt):
   """
   NOTE:  this method works ONLY because we always generate a new address
          whenever creating a change-output, which means it must have a
          higher chainIndex than all other addresses.  If you did something
          creative with this tx, this may not actually work.
   """
   amt = 0
   if le.sent_to_self:
      txProto = TheBridge.service.getTxByHash(le.hash)
      if txProto == None:
         return (0, 0)

      pytx = PyTx().unserialize(txProto.raw)
      if pytx.getNumTxOut()==1:
         return (pytx.outputs[0].getValue(), -1)
      maxChainIndex = -5
      txOutChangeVal = 0
      changeIndex = -1
      valSum = 0
      for i in range(pytx.getNumTxOut()):
         valSum += pytx.outputs[i].getValue()
         addr = wlt.getAddrByString(pytx.outputs[i].getScrAddressStr())
         if addr and addr.chainIndex > maxChainIndex:
            maxChainIndex = addr.chainIndex
            txOutChangeVal = pytx.outputs[i].getValue()
            changeIndex = i

      amt = valSum - txOutChangeVal
   return (amt, changeIndex)


################################################################################
#def getUnspentTxOutsForAddrList(addr160List, utxoType='Sweep', startBlk=-1, \
def getUnspentTxOutsForAddr160List(addr160List):
   '''
   You have a list of addresses (or just one) and you want to get all the
   unspent TxOuts for it.  This can either be for computing its balance, or
   for sweeping the address(es).

   This will return a list of pairs of UTXOs
   This isn't the most efficient method for producing the pairs

   NOTE:  At the moment, this only gets STANDARD TxOuts... non-std uses
          a different BDM call

   This method will return empty list if the BDM is currently in the
   middle of a scan.  You can use waitAsLongAsNecessary=True if you
   want to wait for the previous scan AND the next scan.  Otherwise,
   you can check for bal==-1 and then try again later...
   
   //note for the new backend:
      This call has no scalability whatsoever. Unless you want UTXOs for a small
      list of arbitrary addresses with limited history, do not resort to this call!
      
      Instead, register a wallet and pull the UTXOs from there.
      
      Also, no ZC
   '''
   
   if TheBDM.getState()==BDM_BLOCKCHAIN_READY:
      if not isinstance(addr160List, (list,tuple)):
         addr160List = [addr160List]

      scrAddrList = []

      for addr in addr160List:
         if isinstance(addr, PyBtcAddress):
            scrAddrList.append(ADDRBYTE + addr.getAddr160())
         else:
            # Have to Skip ROOT
            if addr!='ROOT':
               scrAddrList.append(ADDRBYTE + addr)
      

      from armoryengine.CoinSelection import PyUnspentTxOut
      utxoList = TheBDM.bdv().getUtxosForAddrVec(scrAddrList)
      pyUtxoList = []
      for utxo in utxoList:
         pyUtxoList.append(PyUnspentTxOut().createFromCppUtxo(utxo))

      return pyUtxoList
   
   else:
      return []

################################################################################
def pprintLedgerEntry(le, indent=''):
   if len(le.getScrAddr())==21:
      hash160 = CheckHash160(le.getScrAddr())
      addrStr = hash160_to_addrStr(hash160)[:12]
   else:
      addrStr = ''

   leVal = coin2str(le.getValue(), maxZeros=1)
   txType = ''
   if le.isSentToSelf():
      txType = 'ToSelf'
   else:
      txType = 'Recv' if le.getValue()>0 else 'Sent'

   blkStr = str(le.getBlockNum())
   print(indent + 'LE %s %s %s %s' % \
            (addrStr.ljust(15), leVal, txType.ljust(8), blkStr.ljust(8)))