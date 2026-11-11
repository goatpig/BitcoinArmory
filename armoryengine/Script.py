from __future__ import (absolute_import, division,
                        print_function, unicode_literals)
################################################################################
#                                                                              #
# Copyright (C) 2011-2015, Armory Technologies, Inc.                           #
# Distributed under the GNU Affero General Public License (AGPL v3)            #
# See LICENSE or http://www.gnu.org/licenses/agpl.html                         #
#                                                                              #
################################################################################
################################################################################
#
# SCRIPTING!
#
################################################################################
from armoryengine.BinaryPacker import UINT8, BINARY_CHUNK, UINT16, UINT32
from armoryengine.BinaryUnpacker import BinaryUnpacker
from armoryengine.Timer import TimeThisFunction

################################################################################
# Identify all the codes/strings that are needed for dealing with scripts
################################################################################
# Start list of OP codes
opnames = ['']*256
opnames[0] =   'OP_0'
for i in range(1,76):
   opnames[i] ='OP_PUSHDATA'
opnames[76] =   'OP_PUSHDATA1'
opnames[77] =   'OP_PUSHDATA2'
opnames[78] =   'OP_PUSHDATA4'
opnames[79] =   'OP_1NEGATE'
opnames[81] =  'OP_1'
opnames[81] =   'OP_TRUE'
for i in range(1,17):
   opnames[80+i] = 'OP_' + str(i)
opnames[97] =   'OP_NOP'
opnames[99] =   'OP_IF'
opnames[100] =   'OP_NOTIF'
opnames[103] = 'OP_ELSE'
opnames[104] = 'OP_ENDIF'
opnames[105] =   'OP_VERIFY'
opnames[106] = 'OP_RETURN'
opnames[107] =   'OP_TOALTSTACK'
opnames[108] =   'OP_FROMALTSTACK'
opnames[115] =   'OP_IFDUP'
opnames[116] =   'OP_DEPTH'
opnames[117] =   'OP_DROP'
opnames[118] =   'OP_DUP'
opnames[119] =   'OP_NIP'
opnames[120] =   'OP_OVER'
opnames[121] =   'OP_PICK'
opnames[122] =   'OP_ROLL'
opnames[123] =   'OP_ROT'
opnames[124] =   'OP_SWAP'
opnames[125] =   'OP_TUCK'
opnames[109] =   'OP_2DROP'
opnames[110] =   'OP_2DUP'
opnames[111] =   'OP_3DUP'
opnames[112] =   'OP_2OVER'
opnames[113] =   'OP_2ROT'
opnames[114] =   'OP_2SWAP'
opnames[126] =   'OP_CAT'
opnames[127] = 'OP_SUBSTR'
opnames[128] =   'OP_LEFT'
opnames[129] =   'OP_RIGHT'
opnames[130] =   'OP_SIZE'
opnames[131] =   'OP_INVERT'
opnames[132] =   'OP_AND'
opnames[133] =   'OP_OR'
opnames[134] = 'OP_XOR'
opnames[135] = 'OP_EQUAL'
opnames[136] =   'OP_EQUALVERIFY'
opnames[139] =   'OP_1ADD'
opnames[140] =   'OP_1SUB'
opnames[141] =   'OP_2MUL'
opnames[142] =   'OP_2DIV'
opnames[143] =   'OP_NEGATE'
opnames[144] =   'OP_ABS'
opnames[145] =   'OP_NOT'
opnames[146] =   'OP_0NOTEQUAL'
opnames[147] =   'OP_ADD'
opnames[148] =   'OP_SUB'
opnames[149] =   'OP_MUL'
opnames[150] =   'OP_DIV'
opnames[151] =   'OP_MOD'
opnames[152] =   'OP_LSHIFT'
opnames[153] =   'OP_RSHIFT'
opnames[154] =   'OP_BOOLAND'
opnames[155] =   'OP_BOOLOR'
opnames[156] =   'OP_NUMEQUAL'
opnames[157] =   'OP_NUMEQUALVERIFY'
opnames[158] =   'OP_NUMNOTEQUAL'
opnames[159] =   'OP_LESSTHAN'
opnames[160] =   'OP_GREATERTHAN'
opnames[161] =   'OP_LESSTHANOREQUAL'
opnames[162] = 'OP_GREATERTHANOREQUAL'
opnames[163] =   'OP_MIN'
opnames[164] =   'OP_MAX'
opnames[165] = 'OP_WITHIN'
opnames[166] =   'OP_RIPEMD160'
opnames[167] =   'OP_SHA1'
opnames[168] =   'OP_SHA256'
opnames[169] =   'OP_HASH160'
opnames[170] =   'OP_HASH256'
opnames[171] =   'OP_CODESEPARATOR'
opnames[172] =   'OP_CHECKSIG'
opnames[173] =   'OP_CHECKSIGVERIFY'
opnames[174] =   'OP_CHECKMULTISIG'
opnames[175] =   'OP_CHECKMULTISIGVERIFY'

OP_0 = 0
OP_FALSE = 0
OP_PUSHDATA1 = 76
OP_PUSHDATA2 = 77
OP_PUSHDATA4 = 78
OP_1NEGATE = 79
OP_1 = 81
OP_TRUE = 81
OP_2 = 82
OP_3 = 83
OP_4 = 84
OP_5 = 85
OP_6 = 86
OP_7 = 87
OP_8 = 88
OP_9 = 89
OP_10 = 90
OP_11 = 91
OP_12 = 92
OP_13 = 93
OP_14 = 94
OP_15 = 95
OP_16 = 96
OP_NOP = 97
OP_IF = 99
OP_NOTIF = 100
OP_ELSE = 103
OP_ENDIF = 104
OP_VERIFY = 105
OP_RETURN = 106
OP_TOALTSTACK = 107
OP_FROMALTSTACK = 108
OP_IFDUP = 115
OP_DEPTH = 116
OP_DROP = 117
OP_DUP = 118
OP_NIP = 119
OP_OVER = 120
OP_PICK = 121
OP_ROLL = 122
OP_ROT = 123
OP_SWAP = 124
OP_TUCK = 125
OP_2DROP = 109
OP_2DUP = 110
OP_3DUP = 111
OP_2OVER = 112
OP_2ROT = 113
OP_2SWAP = 114
OP_CAT = 126
OP_SUBSTR = 127
OP_LEFT = 128
OP_RIGHT = 129
OP_SIZE = 130
OP_INVERT = 131
OP_AND = 132
OP_OR = 133
OP_XOR = 134
OP_EQUAL = 135
OP_EQUALVERIFY = 136
OP_1ADD = 139
OP_1SUB = 140
OP_2MUL = 141
OP_2DIV = 142
OP_NEGATE = 143
OP_ABS = 144
OP_NOT = 145
OP_0NOTEQUAL = 146
OP_ADD = 147
OP_SUB = 148
OP_MUL = 149
OP_DIV = 150
OP_MOD = 151
OP_LSHIFT = 152
OP_RSHIFT = 153
OP_BOOLAND = 154
OP_BOOLOR = 155
OP_NUMEQUAL = 156
OP_NUMEQUALVERIFY = 157
OP_NUMNOTEQUAL = 158
OP_LESSTHAN = 159
OP_GREATERTHAN = 160
OP_LESSTHANOREQUAL = 161
OP_GREATERTHANOREQUAL = 162
OP_MIN = 163
OP_MAX = 164
OP_WITHIN = 165
OP_RIPEMD160 = 166
OP_SHA1 = 167
OP_SHA256 = 168
OP_HASH160 = 169
OP_HASH256 = 170
OP_CODESEPARATOR = 171
OP_CHECKSIG = 172
OP_CHECKSIGVERIFY = 173
OP_CHECKMULTISIG = 174
OP_CHECKMULTISIGVERIFY = 175

opCodeLookup = {}
opCodeLookup['OP_FALSE'] = 0
opCodeLookup['OP_0']     = 0
opCodeLookup['OP_PUSHDATA1'] =   76
opCodeLookup['OP_PUSHDATA2'] =   77
opCodeLookup['OP_PUSHDATA4'] =   78
opCodeLookup['OP_1NEGATE'] =   79
opCodeLookup['OP_1'] =  81
for i in range(1,17):
   opCodeLookup['OP_'+str(i)] =  80+i
opCodeLookup['OP_TRUE'] =   81
opCodeLookup['OP_NOP'] =   97
opCodeLookup['OP_IF'] =   99
opCodeLookup['OP_NOTIF'] =   100
opCodeLookup['OP_ELSE'] = 103
opCodeLookup['OP_ENDIF'] = 104
opCodeLookup['OP_VERIFY'] =   105
opCodeLookup['OP_RETURN'] = 106
opCodeLookup['OP_TOALTSTACK'] =   107
opCodeLookup['OP_FROMALTSTACK'] =   108
opCodeLookup['OP_IFDUP'] =   115
opCodeLookup['OP_DEPTH'] =   116
opCodeLookup['OP_DROP'] =   117
opCodeLookup['OP_DUP'] =   118
opCodeLookup['OP_NIP'] =   119
opCodeLookup['OP_OVER'] =   120
opCodeLookup['OP_PICK'] =   121
opCodeLookup['OP_ROLL'] =   122
opCodeLookup['OP_ROT'] =   123
opCodeLookup['OP_SWAP'] =   124
opCodeLookup['OP_TUCK'] =   125
opCodeLookup['OP_2DROP'] =   109
opCodeLookup['OP_2DUP'] =   110
opCodeLookup['OP_3DUP'] =   111
opCodeLookup['OP_2OVER'] =   112
opCodeLookup['OP_2ROT'] =   113
opCodeLookup['OP_2SWAP'] =   114
opCodeLookup['OP_CAT'] =   126
opCodeLookup['OP_SUBSTR'] = 127
opCodeLookup['OP_LEFT'] =   128
opCodeLookup['OP_RIGHT'] =   129
opCodeLookup['OP_SIZE'] =   130
opCodeLookup['OP_INVERT'] =   131
opCodeLookup['OP_AND'] =   132
opCodeLookup['OP_OR'] =   133
opCodeLookup['OP_XOR'] = 134
opCodeLookup['OP_EQUAL'] = 135
opCodeLookup['OP_EQUALVERIFY'] =   136
opCodeLookup['OP_1ADD'] =   139
opCodeLookup['OP_1SUB'] =   140
opCodeLookup['OP_2MUL'] =   141
opCodeLookup['OP_2DIV'] =   142
opCodeLookup['OP_NEGATE'] =   143
opCodeLookup['OP_ABS'] =   144
opCodeLookup['OP_NOT'] =   145
opCodeLookup['OP_0NOTEQUAL'] =   146
opCodeLookup['OP_ADD'] =   147
opCodeLookup['OP_SUB'] =   148
opCodeLookup['OP_MUL'] =   149
opCodeLookup['OP_DIV'] =   150
opCodeLookup['OP_MOD'] =   151
opCodeLookup['OP_LSHIFT'] =   152
opCodeLookup['OP_RSHIFT'] =   153
opCodeLookup['OP_BOOLAND'] =   154
opCodeLookup['OP_BOOLOR'] =   155
opCodeLookup['OP_NUMEQUAL'] =   156
opCodeLookup['OP_NUMEQUALVERIFY'] =   157
opCodeLookup['OP_NUMNOTEQUAL'] =   158
opCodeLookup['OP_LESSTHAN'] =   159
opCodeLookup['OP_GREATERTHAN'] =   160
opCodeLookup['OP_LESSTHANOREQUAL'] =   161
opCodeLookup['OP_GREATERTHANOREQUAL'] = 162
opCodeLookup['OP_MIN'] =   163
opCodeLookup['OP_MAX'] =   164
opCodeLookup['OP_WITHIN'] = 165
opCodeLookup['OP_RIPEMD160'] =   166
opCodeLookup['OP_SHA1'] =   167
opCodeLookup['OP_SHA256'] =   168
opCodeLookup['OP_HASH160'] =   169
opCodeLookup['OP_HASH256'] =   170
opCodeLookup['OP_CODESEPARATOR'] =   171
opCodeLookup['OP_CHECKSIG'] =   172
opCodeLookup['OP_CHECKSIGVERIFY'] =   173
opCodeLookup['OP_CHECKMULTISIG'] =   174
opCodeLookup['OP_CHECKMULTISIGVERIFY'] =   175

################################################################################
def getOpCode(name):
   return int_to_binary(opCodeLookup[name], widthBytes=1)

################################################################################
def convertScriptToOpStrings(binScript):
   from armoryengine.ArmoryUtils import binary_to_hex, binary_to_int
   opList = []

   i = 0
   sz = len(binScript)
   error = False
   while i < sz:
      nextOp = binScript[i]
      if nextOp == 0:
         opList.append("0")
         i+=1
      elif nextOp < 76:
         opList.append('PUSHDATA(%s)' % str(nextOp))
         binObj = binScript[i+1:i+1+nextOp]
         opList.append('['+binary_to_hex(binObj)+']')
         i += nextOp+1
      elif nextOp == 76:
         nb = binary_to_int(binScript[i+1:i+2])
         if i+1+1+nb > sz:
            error = True
            break
         binObj = binScript[i+2:i+2+nb]
         opList.append('OP_PUSHDATA1(%s)' % str(nb))
         opList.append('['+binary_to_hex(binObj)+']')
         i += nb+2
      elif nextOp == 77:
         nb = binary_to_int(binScript[i+1:i+3])
         if i+1+2+nb > sz:
            error = True
            break
         nbprint = min(nb,256)
         binObj = binScript[i+3:i+3+nbprint]
         opList.append('OP_PUSHDATA2(%s)' % str(nb))
         opList.append('['+binary_to_hex(binObj)[:512] + '...]')
         i += nb+3
      elif nextOp == 78:
         nb = binary_to_int(binScript[i+1:i+5])
         if i+1+4+nb > sz:
            error = True
            break
         nbprint = min(nb,256)
         binObj = binScript[i+5:i+5+nbprint]
         opList.append('[OP_PUSHDATA4(%s)]' % str(nb))
         opList.append('['+binary_to_hex(binObj)[:512] + '...]')
         i += nb+5
      else:
         opList.append(opnames[nextOp])
         i += 1

   if error:
      opList.append("ERROR PROCESSING SCRIPT")

   return opList


def pprintScript(binScript, nIndent=0):
   indstr = indent*nIndent
   print(indstr + 'Script:')
   opList = convertScriptToOpStrings(binScript)
   for op in opList:
      print(indstr + indent + op)

def scriptPushData(binObj):
   sz = len(binObj) 
   if sz <= 76:
      lenByte = int_to_binary(sz, widthBytes=1)
      return lenByte+binObj
   elif sz <= 256:
      lenByte = int_to_binary(sz, widthBytes=1)
      return b'\x4c' + lenByte + binObj
   elif sz <= 65536:
      lenBytes = int_to_binary(sz, widthBytes=2)
      return b'\x4d' + lenBytes + binObj
   else:
      InvalidScriptError('Cannot use PUSHDATA for len(obj)>65536')

class ScriptBuilder(object):
   def __init__(self):
      self.opList = []

   def addOpCode(self, opStr):
      self.opList.append(getOpCode(opStr))
      
   def pushData(self, data):
      if data.startswith('OP_'):
         LOGWARN('Looks like you accidentally called pushData instead of addOpCode')
         LOGWARN('Pushing data: ' + data)
      self.opList.append(scriptPushData(data))

   def getBinaryScript(self):
      return ''.join(self.opList)

   def getHexScript(self):
      from armoryengine.ArmoryUtils import binary_to_hex
      return binary_to_hex(''.join(self.opList))

   def getHumanScript(self):
      # Human-readable
      return ' '.join(convertScriptToOpStrings(self.getBinaryScript()))
      

TX_INVALID = 0
OP_NOT_IMPLEMENTED = 1
OP_DISABLED = 2
SCRIPT_STACK_SIZE_ERROR = 3
SCRIPT_ERROR = 4
SCRIPT_NO_ERROR = 5


class PyScriptProcessor(object):
   """
   Use this class to evaluate a script.  This method is more complicated
   than some might expect, due to the fact that any OP_CHECKSIG or
   OP_CHECKMULTISIG code requires the full transaction of the TxIn script
   and also needs the TxOut script being spent.  Since nearly every useful
   script will have one of these operations, this class/method assumes
   that all that data will be supplied.

   To simply execute a script not requiring any crypto operations:

      scriptIsValid = PyScriptProcessor().executeScript(binScript)
   """

   def __init__(self, txOldData=None, txNew=None, txInIndex=None):
      self.stack   = []
      self.txNew   = None
      self.script1 = None
      self.script2 = None
      if txOldData and txNew and not txInIndex==None:
         self.setTxObjects(txOldData, txNew, txInIndex)


   def setTxObjects(self, txOldData, txNew, txInIndex):
      """
      The minimal amount of data necessary to evaluate a script that
      has an signature check is the TxOut script that is being spent
      and the entire Tx of the TxIn that is spending it.  Thus, we
      must supply at least the txOldScript, and a txNew with its
      TxIn index (so we know which TxIn is spending that TxOut).
      It is acceptable to pass in the full TxOut or the tx of the
      TxOut instead of just the script itself.
      """
      self.txNew = PyTx().unserialize(txNew.serialize())
      self.script1 = str(txNew.inputs[txInIndex].binScript) # copy
      self.txInIndex  = txInIndex
      self.txOutIndex = txNew.inputs[txInIndex].outpoint.txOutIndex
      self.txHash  = txNew.inputs[txInIndex].outpoint.txHash

      if isinstance(txOldData, PyTx):
         if not self.txHash == hash256(txOldData.serialize()):
            LOGERROR('*** Supplied incorrect pair of transactions!')
         self.script2 = str(txOldData.outputs[self.txOutIndex].binScript)
      elif isinstance(txOldData, PyTxOut):
         self.script2 = str(txOldData.binScript)
      elif isinstance(txOldData, str):
         self.script2 = str(txOldData)

   @TimeThisFunction
   def verifyTransactionValid(self, txOldData=None, txNew=None, txInIndex=-1):
      if txOldData and txNew and txInIndex != -1:
         self.setTxObjects(txOldData, txNew, txInIndex)
      else:
         txOldData = self.script2
         txNew = self.txNew
         txInIndex = self.txInIndex

      if self.script1==None or self.txNew==None:
         raise VerifyScriptError('Cannot verify transactions, without setTxObjects call first!')

      # Execute TxIn script first
      self.stack = []
      exitCode1 = self.executeScript(self.script1, self.stack)

      if not exitCode1 == SCRIPT_NO_ERROR:
         raise VerifyScriptError('First script failed!  Exit Code: ' + str(exitCode1))

      exitCode2 = self.executeScript(self.script2, self.stack)

      if not exitCode2 == SCRIPT_NO_ERROR:
         raise VerifyScriptError('Second script failed!  Exit Code: ' + str(exitCode2))

      return self.stack[-1]==1


   def executeScript(self, binaryScript, stack=[]):
      self.stack = stack
      self.stackAlt  = []
      scriptData = BinaryUnpacker(binaryScript)
      self.lastOpCodeSepPos = None

      while scriptData.getRemainingSize() > 0:
         opcode = scriptData.get(UINT8)
         exitCode = self.executeOpCode(opcode, scriptData, self.stack, self.stackAlt)
         if not exitCode == SCRIPT_NO_ERROR:
            if exitCode==OP_NOT_IMPLEMENTED:
               LOGERROR('***ERROR: OpCodes OP_IF, OP_NOTIF, OP_ELSE, OP_ENDIF,')
               LOGERROR('          have not been implemented, yet.  This script')
               LOGERROR('          could not be evaluated.')
            if exitCode==OP_DISABLED:
               LOGERROR('***ERROR: This script included an op code that has been')
               LOGERROR('          disabled for security reasons.  Script eval')
               LOGERROR('          failed.')
            return exitCode

      return SCRIPT_NO_ERROR


   # Implementing this method exactly as in the client because it looks like
   # there could be some subtleties with how it determines "true"
   def castToBool(self, binData):
      if isinstance(binData, int):
         binData = int_to_binary(binData)

      for i,byte in enumerate(binData):
         if not byte == 0:
            # This looks like it's assuming LE encoding (?)
            if (i == len(binData)-1) and (byte==0x80):
               return False
            return True
      return False


   def checkSig(self,binSig, binPubKey, txOutScript, txInTx, txInIndex, lastOpCodeSep=None):
      """
      Generic method for checking Bitcoin tx signatures.  This needs to be used for both
      OP_CHECKSIG and OP_CHECKMULTISIG.  Step 1 is to pop signature and public key off
      the stack, which must be done outside this method and passed in through the argument
      list.  The remaining steps do not require access to the stack.
      """

      # 2. Subscript is from latest OP_CODESEPARATOR until end... if DNE, use whole script
      subscript = txOutScript
      if lastOpCodeSep:
         subscript = subscript[lastOpCodeSep:]

      # 3. Signature is deleted from subscript
      #    I'm not sure why this line is necessary - maybe for non-standard scripts?
      lengthInBinary = int_to_binary(len(binSig))
      subscript = subscript.replace( lengthInBinary + binSig, "")

      # 4. Hashtype is popped and stored
      hashtype = binary_to_int(binSig[-1])
      justSig = binSig[:-1]

      if not hashtype == 1:
         LOGERROR('Non-unity hashtypes not implemented yet! (hashtype = %d)', hashtype)
         assert(False)

      # 5. Make a copy of the transaction -- we will be hashing a modified version
      txCopy = PyTx().unserialize( txInTx.serialize() )

      # 6. Remove all OP_CODESEPARATORs
      subscript.replace( int_to_binary(OP_CODESEPARATOR), '')

      # 7. All the TxIn scripts in the copy are blanked (set to empty string)
      for txin in txCopy.inputs:
         txin.binScript = ''

      # 8. Script for the current input in the copy is set to subscript
      txCopy.inputs[txInIndex].binScript = subscript

      # 9. Prepare the signature and public key
      senderAddr = PyBtcAddress().createFromPublicKey(binPubKey)
      binHashCode = int_to_binary(hashtype, widthBytes=4)
      toHash = txCopy.serialize() + binHashCode

      # Hashes are computed as part of CppBlockUtils::CryptoECDSA methods
      ##hashToVerify = hash256(toHash)
      ##hashToVerify = binary_switchEndian(hashToVerify)

      # 10. Apply ECDSA signature verification
      if senderAddr.verifyDERSignature(toHash, justSig):
         return True
      else:
         return False




   def executeOpCode(self, opcode, scriptUnpacker, stack, stackAlt):
      """
      Executes the next OP_CODE given the current state of the stack(s)
      """

      # TODO: Gavin clarified the effects of OP_0, and OP_1-OP_16.
      #       OP_0 puts an empty string onto the stack, which evaluates to
      #            false and is plugged into HASH160 as ''
      #       OP_X puts a single byte onto the stack, 0x01 to 0x10
      #
      #       I haven't implemented it this way yet, because I'm still missing
      #       some details.  Since this "works" for available scripts, I'm going
      #       to leave it alone for now.

      ##########################################################################
      ##########################################################################
      ### This block produces very nice debugging output for script eval!
      #def pr(s):
         #if isinstance(s,int):
            #return str(s)
         #elif isinstance(s,str):
            #if len(s)>8:
               #return binary_to_hex(s)[:8]
            #else:
               #return binary_to_hex(s)

      #print '  '.join([pr(i) for i in stack])
      #print opnames[opcode][:12].ljust(12,' ') + ':',
      ##########################################################################
      ##########################################################################


      stackSizeAtLeast = lambda n: (len(self.stack) >= n)

      if   opcode == OP_FALSE:
         stack.append(0)
      elif 0 < opcode < 76:
         stack.append(scriptUnpacker.get(BINARY_CHUNK, opcode))
      elif opcode == OP_PUSHDATA1:
         nBytes = scriptUnpacker.get(UINT8)
         stack.append(scriptUnpacker.get(BINARY_CHUNK, nBytes))
      elif opcode == OP_PUSHDATA2:
         nBytes = scriptUnpacker.get(UINT16)
         stack.append(scriptUnpacker.get(BINARY_CHUNK, nBytes))
      elif opcode == OP_PUSHDATA4:
         nBytes = scriptUnpacker.get(UINT32)
         stack.append(scriptUnpacker.get(BINARY_CHUNK, nBytes))
      elif opcode == OP_1NEGATE:
         stack.append(-1)
      elif opcode == OP_TRUE:
         stack.append(1)
      elif 81 < opcode < 97:
         stack.append(opcode-80)
      elif opcode == OP_NOP:
         pass

      # TODO: figure out the conditional op codes...
      elif opcode == OP_IF:
         return OP_NOT_IMPLEMENTED
      elif opcode == OP_NOTIF:
         return OP_NOT_IMPLEMENTED
      elif opcode == OP_ELSE:
         return OP_NOT_IMPLEMENTED
      elif opcode == OP_ENDIF:
         return OP_NOT_IMPLEMENTED

      elif opcode == OP_VERIFY:
         if not self.castToBool(stack.pop()):
            stack.append(0)
            return TX_INVALID
      elif opcode == OP_RETURN:
         return TX_INVALID
      elif opcode == OP_TOALTSTACK:
         stackAlt.append( stack.pop() )
      elif opcode == OP_FROMALTSTACK:
         stack.append( stackAlt.pop() )

      elif opcode == OP_IFDUP:
         # Looks like this method duplicates the top item if it's not zero
         if not stackSizeAtLeast(1): return SCRIPT_STACK_SIZE_ERROR
         if self.castToBool(stack[-1]):
            stack.append(stack[-1]);

      elif opcode == OP_DEPTH:
         stack.append( len(stack) )
      elif opcode == OP_DROP:
         stack.pop()
      elif opcode == OP_DUP:
         stack.append( stack[-1] )
      elif opcode == OP_NIP:
         if not stackSizeAtLeast(2): return SCRIPT_STACK_SIZE_ERROR
         del stack[-2]
      elif opcode == OP_OVER:
         if not stackSizeAtLeast(2): return SCRIPT_STACK_SIZE_ERROR
         stack.append(stack[-2])
      elif opcode == OP_PICK:
         n = stack.pop()
         if not stackSizeAtLeast(n): return SCRIPT_STACK_SIZE_ERROR
         stack.append(stack[-n])
      elif opcode == OP_ROLL:
         n = stack.pop()
         if not stackSizeAtLeast(n): return SCRIPT_STACK_SIZE_ERROR
         stack.append(stack[-(n+1)])
         del stack[-(n+2)]
      elif opcode == OP_ROT:
         if not stackSizeAtLeast(3): return SCRIPT_STACK_SIZE_ERROR
         stack.append( stack[-3] )
         del stack[-4]
      elif opcode == OP_SWAP:
         if not stackSizeAtLeast(2): return SCRIPT_STACK_SIZE_ERROR
         x2 = stack.pop()
         x1 = stack.pop()
         stack.extend([x2, x1])
      elif opcode == OP_TUCK:
         if not stackSizeAtLeast(2): return SCRIPT_STACK_SIZE_ERROR
         x2 = stack.pop()
         x1 = stack.pop()
         stack.extend([x2, x1, x2])
      elif opcode == OP_2DROP:
         if not stackSizeAtLeast(2): return SCRIPT_STACK_SIZE_ERROR
         stack.pop()
         stack.pop()
      elif opcode == OP_2DUP:
         if not stackSizeAtLeast(2): return SCRIPT_STACK_SIZE_ERROR
         stack.append( stack[-2] )
         stack.append( stack[-2] )
      elif opcode == OP_3DUP:
         if not stackSizeAtLeast(3): return SCRIPT_STACK_SIZE_ERROR
         stack.append( stack[-3] )
         stack.append( stack[-3] )
         stack.append( stack[-3] )
      elif opcode == OP_2OVER:
         if not stackSizeAtLeast(4): return SCRIPT_STACK_SIZE_ERROR
         stack.append( stack[-4] )
         stack.append( stack[-4] )
      elif opcode == OP_2ROT:
         if not stackSizeAtLeast(6): return SCRIPT_STACK_SIZE_ERROR
         stack.append( stack[-6] )
         stack.append( stack[-6] )
      elif opcode == OP_2SWAP:
         if not stackSizeAtLeast(4): return SCRIPT_STACK_SIZE_ERROR
         x4 = stack.pop()
         x3 = stack.pop()
         x2 = stack.pop()
         x1 = stack.pop()
         stack.extend( [x3, x4, x1, x2] )
      elif opcode == OP_CAT:
         return OP_DISABLED
      elif opcode == OP_SUBSTR:
         return OP_DISABLED
      elif opcode == OP_LEFT:
         return OP_DISABLED
      elif opcode == OP_RIGHT:
         return OP_DISABLED
      elif opcode == OP_SIZE:
         if isinstance(stack[-1], int):
            stack.append(0)
         else:
            stack.append( len(stack[-1]) )
      elif opcode == OP_INVERT:
         return OP_DISABLED
      elif opcode == OP_AND:
         return OP_DISABLED
      elif opcode == OP_OR:
         return OP_DISABLED
      elif opcode == OP_XOR:
         return OP_DISABLED
      elif opcode == OP_EQUAL:
         x1 = stack.pop()
         x2 = stack.pop()
         stack.append( 1 if x1==x2 else 0  )
      elif opcode == OP_EQUALVERIFY:
         x1 = stack.pop()
         x2 = stack.pop()
         if not x1==x2:
            stack.append(0)
            return TX_INVALID


      elif opcode == OP_1ADD:
         stack[-1] += 1
      elif opcode == OP_1SUB:
         stack[-1] -= 1
      elif opcode == OP_2MUL:
         stack[-1] *= 2
         return OP_DISABLED
      elif opcode == OP_2DIV:
         stack[-1] /= 2
         return OP_DISABLED
      elif opcode == OP_NEGATE:
         stack[-1] *= -1
      elif opcode == OP_ABS:
         stack[-1] = abs(stack[-1])
      elif opcode == OP_NOT:
         top = stack.pop()
         if top==0:
            stack.append(1)
         else:
            stack.append(0)
      elif opcode == OP_0NOTEQUAL:
         top = stack.pop()
         if top==0:
            stack.append(0)
         else:
            stack.append(1)
         top = stack.pop()
         if top==0:
            stack.append(1)
         else:
            stack.append(0)
      elif opcode == OP_ADD:
         b = stack.pop()
         a = stack.pop()
         stack.append(a+b)
      elif opcode == OP_SUB:
         b = stack.pop()
         a = stack.pop()
         stack.append(a-b)
      elif opcode == OP_MUL:
         return OP_DISABLED
      elif opcode == OP_DIV:
         return OP_DISABLED
      elif opcode == OP_MOD:
         return OP_DISABLED
      elif opcode == OP_LSHIFT:
         return OP_DISABLED
      elif opcode == OP_RSHIFT:
         return OP_DISABLED
      elif opcode == OP_BOOLAND:
         b = stack.pop()
         a = stack.pop()
         if (not a==0) and (not b==0):
            stack.append(1)
         else:
            stack.append(0)
      elif opcode == OP_BOOLOR:
         b = stack.pop()
         a = stack.pop()
         stack.append( 1 if (self.castToBool(a) or self.castToBool(b)) else 0 )
      elif opcode == OP_NUMEQUAL:
         b = stack.pop()
         a = stack.pop()
         stack.append( 1 if a==b else 0 )
      elif opcode == OP_NUMEQUALVERIFY:
         b = stack.pop()
         a = stack.pop()
         if not a==b:
            stack.append(0)
            return TX_INVALID
      elif opcode == OP_NUMNOTEQUAL:
         b = stack.pop()
         a = stack.pop()
         stack.append( 1 if not a==b else 0 )
      elif opcode == OP_LESSTHAN:
         b = stack.pop()
         a = stack.pop()
         stack.append( 1 if a<b else 0)
      elif opcode == OP_GREATERTHAN:
         b = stack.pop()
         a = stack.pop()
         stack.append( 1 if a>b else 0)
      elif opcode == OP_LESSTHANOREQUAL:
         b = stack.pop()
         a = stack.pop()
         stack.append( 1 if a<=b else 0)
      elif opcode == OP_GREATERTHANOREQUAL:
         b = stack.pop()
         a = stack.pop()
         stack.append( 1 if a>=b else 0)
      elif opcode == OP_MIN:
         b = stack.pop()
         a = stack.pop()
         stack.append( min(a,b) )
      elif opcode == OP_MAX:
         b = stack.pop()
         a = stack.pop()
         stack.append( max(a,b) )
      elif opcode == OP_WITHIN:
         xmax = stack.pop()
         xmin = stack.pop()
         x    = stack.pop()
         stack.append( 1 if (xmin <= x < xmax) else 0 )

      elif opcode == OP_RIPEMD160:
         bits = stack.pop()
         stack.append( ripemd160(bits) )
      elif opcode == OP_SHA1:
         bits = stack.pop()
         stack.append( sha1(bits) )
      elif opcode == OP_SHA256:
         bits = stack.pop()
         stack.append( sha256(bits) )
      elif opcode == OP_HASH160:
         bits = stack.pop()
         if isinstance(bits, int):
            bits = ''
         stack.append( hash160(bits) )
      elif opcode == OP_HASH256:
         bits = stack.pop()
         if isinstance(bits, int):
            bits = ''
         stack.append( sha256(sha256(bits) ) )
      elif opcode == OP_CODESEPARATOR:
         self.lastOpCodeSepPos = scriptUnpacker.getPosition()
      elif opcode == OP_CHECKSIG or opcode == OP_CHECKSIGVERIFY:

         # 1. Pop key and sig from the stack
         binPubKey = stack.pop()
         binSig    = stack.pop()

         # 2-10. encapsulated in sep method so CheckMultiSig can use it too
         txIsValid = self.checkSig(  binSig, \
                                     binPubKey, \
                                     scriptUnpacker.getBinaryString(), \
                                     self.txNew, \
                                     self.txInIndex, \
                                     self.lastOpCodeSepPos)
         stack.append(1 if txIsValid else 0)
         if opcode==OP_CHECKSIGVERIFY:
            verifyCode = self.executeOpCode(OP_VERIFY)
            if verifyCode == TX_INVALID:
               return TX_INVALID

      elif opcode == OP_CHECKMULTISIG or opcode == OP_CHECKMULTISIGVERIFY:
         # OP_CHECKMULTISIG procedure ported directly from Satoshi client code
         # Location:  bitcoin-0.4.0-linux/src/src/script.cpp:775
         i=1
         if len(stack) < i:
            return TX_INVALID

         nKeys = int(stack[-i])
         if nKeys < 0 or nKeys > 20:
            return TX_INVALID

         i += 1
         iKey = i
         i += nKeys
         if len(stack) < i:
            return TX_INVALID

         nSigs = int(stack[-i])
         if nSigs < 0 or nSigs > nKeys:
            return TX_INVALID

         iSig = i
         i += 1
         i += nSigs
         if len(stack) < i:
            return TX_INVALID

         stack.pop()

         # Apply the ECDSA verification to each of the supplied Sig-Key-pairs
         enoughSigsMatch = True
         while enoughSigsMatch and nSigs > 0:
            binSig = stack[-iSig]
            binKey = stack[-iKey]

            if( self.checkSig(binSig, \
                              binKey, \
                              scriptUnpacker.getBinaryString(), \
                              self.txNew, \
                              self.txInIndex, \
                              self.lastOpCodeSepPos) ):
               iSig  += 1
               nSigs -= 1

            iKey +=1
            nKeys -=1

            if(nSigs > nKeys):
               enoughSigsMatch = False

         # Now pop the things off the stack, we only accessed in-place before
         while i > 1:
            i -= 1
            stack.pop()


         stack.append(1 if enoughSigsMatch else 0)

         if opcode==OP_CHECKMULTISIGVERIFY:
            verifyCode = self.executeOpCode(OP_VERIFY)
            if verifyCode == TX_INVALID:
               return TX_INVALID

      else:
         return SCRIPT_ERROR

      return SCRIPT_NO_ERROR


# Putting this at the end because of the circular dependency
from armoryengine.PyBtcAddress import PyBtcAddress
