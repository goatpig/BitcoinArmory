################################################################################
#                                                                              #
# Copyright (C) 2019-2023, goatpig.                                            #
#  Distributed under the MIT license                                           #
#  See LICENSE-MIT or https://opensource.org/licenses/MIT                      #
#                                                                              #
################################################################################

from __future__ import (absolute_import, division, annotations,
                        print_function, unicode_literals)
import os
import errno
import socket
from armoryengine import BridgeProto_pb2
from armoryengine.ArmoryUtils import LOGDEBUG, LOGERROR, LOGWARN, hash256
from armoryengine.BinaryPacker import BinaryPacker, \
   UINT32, UINT8, BINARY_CHUNK, VAR_INT
from struct import unpack
import atexit
import threading
import base64
import subprocess

from concurrent.futures import ThreadPoolExecutor

from armoryengine.ArmoryUtils import PassphraseError
from armoryengine.BIP15x import \
    BIP15xConnection, AEAD_THRESHOLD_BEGIN, AEAD_Error, \
    CHACHA20POLY1305MAXPACKETSIZE
BRIDGE_CLIENT_HEADER = 1

import capnp
capnp.remove_import_hook()
Bridge = capnp.load("./cppForSwig/capnp/Bridge.capnp")
Types = capnp.load("./cppForSwig/capnp/Types.capnp")

################################################################################
##
#### Exceptions
##
################################################################################
class BridgeError(Exception):
   pass

################################################################################
class BridgeSignerError(Exception):
   pass

################################################################################
##
#### Tools
##
################################################################################
class PyPromFut(object):

   #############################################################################
   def __init__(self):

      self.data = None
      self.has = False
      self.cv = threading.Condition()

   #############################################################################
   def setVal(self, val):
      self.cv.acquire()
      self.data = val
      self.has = True
      self.cv.notify()
      self.cv.release()

   #############################################################################
   def getVal(self):
      self.cv.acquire()
      while self.has is False:
         self.cv.wait()
      self.cv.release()
      return self.data


################################################################################
##
#### bridge socket
##
################################################################################
class BridgeSocket(object):
   recvLen = 4

   #############################################################################
   ## setup
   def __init__(self):
      self.idCounter = 0
      self.responseDict = {}
      self.callbackDict = {}
      self.bip15xConnection = BIP15xConnection(self.sendToBridgeRaw)
      self.run = False
      self.rwLock = None

   ####
   def setCallback(self, key, func):
      self.callbackDict[key] = func

   def unsetCallback(self, key):
      del self.callbackDict[key]

   #############################################################################
   ## listen socket setup
   def start(self, stringArgs, notifyReadyLbd):
      self.bip15xConnection.setNotifyReadyLbd(notifyReadyLbd)

      self.run = True
      self.rwLock = threading.Lock()

      self.executor = ThreadPoolExecutor(max_workers=2)
      listenFut = self.executor.submit(self.listenOnBridge)

      #append gui pubkey to arg list and spawn bridge
      os.environ['SERVER_PUBKEY'] = self.bip15xConnection.getPubkeyHex()
      self.processFut = self.executor.submit(self.spawnBridge, stringArgs)

      #block until listen socket receives bridge connection
      self.clientSocket = listenFut.result()

      #start socket read thread
      self.clientFut = self.executor.submit(self.readBridgeSocket)

      #initiate AEAD handshake (server has to start it)
      self.bip15xConnection.serverStartHandshake()

   ####
   def stop(self):
      self.rwLock.acquire(True)

      self.run = False
      self.clientSocket.close()
      self.listenSocket.close()

      self.rwLock.release()
      self.clientFut.result()

   #############################################################################
   ## bridge management
   def listenOnBridge(self):
      #setup listener

      portNumber = 46122
      self.listenSocket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
      self.listenSocket.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR, 1)
      self.listenSocket.bind(("127.0.0.1", portNumber))
      self.listenSocket.listen()

      clientSocket, clientIP = self.listenSocket.accept()
      return clientSocket

   ####
   def spawnBridge(self, stringArgs):
      subprocess.run(["./build/CppBridge", stringArgs])

   #############################################################################
   ## socket write
   def encryptPayload(self, clearText):
      if not self.bip15xConnection.encrypted():
         raise AEAD_Error("channel is not encrypted")

      cipherText = []
      if self.bip15xConnection.needsRekey(len(clearText)):
         cipherText.append(self.bip15xConnection.getRekeyPayload())
      cipherText.append(\
         self.bip15xConnection.encrypt(clearText, len(clearText)))

      return cipherText

   ####
   def sendToBridgeProto(self, msg, needsReply: bool,
      callbackFunc, callbackArgs, msgType):

      print("="*10 + "\n", msg, "\n" + "="*10)

      msg.referenceId = self.idCounter
      self.idCounter = self.idCounter + 1

      payload = msg.to_bytes_packed()
      result = self.sendToBridgeBinary(payload, msg.referenceId,
         needsReply, callbackFunc, callbackArgs, msgType)

      if needsReply:
         return result

   ####
   def sendToBridgeBinary(self, payload, payloadId,
      needsReply=True, callback=None, cbArgs=[],
      msgType = BRIDGE_CLIENT_HEADER):

      #grab id from msg counter
      if self.run == False:
         return

      #serialize payload
      bp = BinaryPacker()

      #payload type header
      bp.put(UINT8, msgType)

      #serialized protobuf message
      bp.put(BINARY_CHUNK, payload)

      #grab read write lock
      self.rwLock.acquire(True)

      #encrypt
      encryptedPayloads = self.encryptPayload(bp.getBinaryString())

      if callback != None:
         #set callable in response dict
         wrapper = CallbackWrapper(callback, cbArgs)
         self.responseDict[payloadId] = wrapper

      elif needsReply:
         #instantiate prom/future object and set in response dict
         fut = PyPromFut()
         self.responseDict[payloadId] = fut

      #send over the wire, may have 2 payloads if we triggered a rekey
      for p in encryptedPayloads:
         self.clientSocket.sendall(p)

      #return future to caller
      self.rwLock.release()

      if callback == None and needsReply:
         return fut

   ####
   def sendToBridgeRaw(self, msg):
      self.rwLock.acquire(True)
      self.clientSocket.sendall(msg)
      self.rwLock.release()

   #############################################################################
   ## socket read
   def pollRecv(self, payloadSize):
      payload = bytearray()
      fullSize = payloadSize
      while len(payload) < fullSize:
         try:
            payload += self.clientSocket.recv(payloadSize)
            payloadSize = fullSize - len(payload)
         except socket.error as e:
            err = e.args[0]
            if err == errno.EAGAIN or err == errno.EWOULDBLOCK:
               LOGDEBUG('No data available from socket.')
               continue
            else:
               LOGERROR("Socket error: %s" % str(e))
               break

      return payload

   ####
   def readBridgeSocket(self):
      while self.run is True:
         #wait for data on the socket
         try:
            response = self.clientSocket.recv(self.recvLen)

            if len(response) < self.recvLen:
               break
         except socket.error as e:
            err = e.args[0]
            if err == errno.EAGAIN or err == errno.EWOULDBLOCK:
               LOGDEBUG('No data available from socket.')
               continue
            else:
               LOGERROR("Socket error: %s" % str(e))
               self.run = False
               break

         #if channel is established, incoming data is encrypted
         if self.bip15xConnection.encrypted():
            payloadSize = self.bip15xConnection.decodeSize(response[:4])
            if payloadSize > CHACHA20POLY1305MAXPACKETSIZE:
               LOGERROR("Invalid encrypted packet size: " + str(payloadSize))
               self.run = False
               break

            #grab the payload
            payload = response
            payload += self.pollRecv(\
                payloadSize + self.bip15xConnection.getMacLen())

            #decrypt it
            response = self.bip15xConnection.decrypt(\
               payload, payloadSize)


         #check header
         header = unpack('<B', response[:1])[0]
         if header > AEAD_THRESHOLD_BEGIN[0]:
            #get expected packet size for this payload from the socket
            payloadSize = self.bip15xConnection.getAEADPacketSize(header)

            payload = response[1:]
            if len(payload) < payloadSize:
                payload += self.pollRecv(payloadSize - len(payload))

            try:
               self.bip15xConnection.serverHandshake(header, payload)
            except AEAD_Error as aeadError:
               print (aeadError)
               return

            #handshake packets are not to be processed as user data
            continue

         if not self.bip15xConnection.ready():
            #non AEAD data is only tolerated after channels are setup
            raise BridgeError("Received user data before AEAD is ready")

         #grab packet id
         fullPacket = response[1:]

         #deser protobuf reply
         protoPayload = Bridge.FromBridge.new_message()
         if not protoPayload.from_bytes_packed(fullPacket):
            raise BridgeError("failed to parse proto payload")

         #payloads are either replies or callbacks
         if protoPayload.which() == "reply":
            reply = protoPayload.reply
            referenceId = reply.referenceId

            #lock and look for future object in response dict
            self.rwLock.acquire(True)
            if referenceId not in self.responseDict:
               LOGWARN(f"unknown reply referenceId: {referenceId}")
               self.rwLock.release()
               continue

            #TODO: general error handling on reply.success == False

            #grab the future, delete it from dict
            replyHandler = self.responseDict[referenceId]
            del self.responseDict[referenceId]

            #fill the promise & release lock
            self.rwLock.release()

            if isinstance(replyHandler, PyPromFut):
               replyHandler.setVal(reply)

            elif isinstance(replyHandler, CallbackWrapper):
               replyHandler.execute(reply)

         elif protoPayload.which() == "notification":
            callbackData = protoPayload.notification
            callbackId = callbackData.callbackId

            #find the callback listener
            self.rwLock.acquire(True)
            if callbackId not in self.callbackDict:
               LOGWARN(f"ignoring callback id: {callbackId}")
               self.rwLock.release()
               continue

            #call it with the payload
            callbackFunc = self.callbackDict[callbackId]
            self.rwLock.release()
            callbackFunc.process(callbackData)

################################################################################
##
#### proto wrappers
##
################################################################################
class ProtoWrapper(object):
   ##
   def __init__(self, bridgeSocket: BridgeSocket):
      self.bridgeSocket = bridgeSocket

   ##
   def send(self, msg, needsReply=True, callback=None, cbArgs=[],
      msgType=BRIDGE_CLIENT_HEADER):
      return self.bridgeSocket.sendToBridgeProto(msg,
         needsReply, callback, cbArgs, msgType)

################################################################################
class BlockchainService(ProtoWrapper):
   #############################################################################
   ## setup ##
   def __init__(self, bridgeSocket):
      super().__init__(bridgeSocket)

   #############################################################################
   ## commands ##
   def loadWallets(self, callbackFunc, pushObj):
      packet = Bridge.ToBridge.new_message()
      packet.init("service").loadWallets = pushObj.callbackId

      self.send(packet, callback=callbackFunc)

   ####
   def shutdown(self):
      packet = Bridge.ToBridge.new_message()
      packet.init("service").shutdown = None
      self.send(packet)
      self.bridgeSocket.stop()

   ####
   def setupDB(self):
      packet = Bridge.ToBridge.new_message()
      packet.init("service").setupDb = None
      self.send(packet, needsReply=False)

   ####
   def registerWallets(self):
      packet = Bridge.ToBridge.new_message()
      packet.init("service").registerWallets = None
      self.send(packet, needsReply=False)

   ####
   def goOnline(self):
      packet = Bridge.ToBridge.new_message()
      packet.init("service").goOnline = None
      self.send(packet, needsReply=False)

   ####
   def getLedgerDelegateIdForWallets(self):
      packet = Bridge.ToBridge.new_message()
      packet.init("service").getLedgerDelegate = None

      fut = self.send(packet)
      response = fut.getVal()
      return response.service.ledgerDelegateId

   ####
   def updateWalletsLedgerFilter(self, ids: list[str]):
      packet = Bridge.ToBridge.new_message()

      packetIds = packet.init("service").init("updateWalletsLedgerFilter", len(ids))
      for i, id_ in enumerate(ids):
         packetIds[i] = id_

      self.send(packet, needsReply=False)

   ####
   def getNodeStatus(self):
      packet = Bridge.ToBridge.new_message()
      packet.init("service").getNodeStatus = Types.NodeStatus.new_message()

      fut = self.send(packet)
      reply = fut.getVal()
      return reply.service.nodeStatus

   ####
   def registerWallet(self, walletId: str, isNew: bool):
      packet = Bridge.ToBridge.new_message()
      packetMethod = packet.init("service").init("registerWallet")
      packetMethod.id = walletId
      packetMethod.isNew = isNew

      self.send(packet, needsReply=False)

   ####
   def getHistoryPageForDelegate(self, delegateId: str, pageId: int):
      packet = Bridge.ToBridge.new_message()
      # TODO: Incomplete: HistoryPageRequest is unused
      method = packet.service.get_history_page_for_delegate
      method.delegate_id = delegateId
      method.page_id = pageId

      fut = self.send(packet)
      reply = fut.getVal()
      return reply.service.ledgerHistory

   ####
   def getTxsByHash(self, hashVals: list[bytes]):
      packet = Bridge.ToBridge.new_message()
      packetHashes = packet.init("service").init("getTxsByHash", len(hashVals))
      for i, hash in enumerate(hashVals):
         packetHashes[i] = hash

      fut = self.send(packet)
      reply = fut.getVal()
      if reply.success == False:
         return None
      return reply.service.tx

   ####
   def getHeadersByHeight(self, heights: list[int]):
      packet = Bridge.ToBridge.new_message()
      packetHeights = packet.init("service").init("getHeadersByHeight", len(heights))
      for i, height in enumerate(heights):
         packetHeights[i] = height

      fut = self.send(packet)
      reply = fut.getVal()
      return reply.service.headerData

   ####
   def getFeeSchedule(self, blocks, strat):
      packet = Bridge.ToBridge.new_message()
      # TODO: BlockChainServiceRequest.getFeeSchedule takes 1 text argument
      packet.init("service").getFeeSchedule = blocks

      fut = self.send(packet)
      reply = fut.getVal()
      if reply.success == False:
         raise Exception(reply.error)
      return reply.service.feeSchedule

   ####
   def broadcastTx(self, rawTxs: list[bytes]):
      packet = Bridge.ToBridge.new_message()
      packetTxs = packet.init("service").init("broadcastTx", len(rawTxs))
      for i, tx in enumerate(rawTxs):
         packetTxs[i] = tx

      self.send(packet, needsReply=False)

################################################################################
class BlockchainUtils(ProtoWrapper):
   #############################################################################
   ## setup ##
   def __init__(self, bridgeSocket):
      super().__init__(bridgeSocket)
      self.addrTypeStrByType = {}

   #############################################################################
   ## commands ##
   def getNameForAddrType(self, addrType: int):
      if addrType in self.addrTypeStrByType:
         return self.addrTypeStrByType[addrType]

      packet = Bridge.ToBridge.new_message()
      packet.init("utils").getNameForAddrType = addrType

      fut = self.send(packet)
      reply = fut.getVal()
      if not reply.success:
         raise BridgeError(
            f"[getNameForAddrType] failed with error: {reply.error}")

      addrTypeStr = reply.utils.address_type_name
      self.addrTypeStrByType[addrType] = addrTypeStr
      return addrTypeStr

   ####
   def getHash160(self, data: bytes):
      packet = Bridge.ToBridge.new_message()
      packet.init("utils").getHash160 = data

      fut = self.send(packet)
      reply = fut.getVal()
      return reply.utils.hash

   ####
   def generateRandomHex(self, size: int):
      packet = Bridge.ToBridge.new_message()
      packet.init("utils").generateRandomHex = size

      fut = self.send(packet)
      reply = fut.getVal()
      return reply.utils.randomHex

   ####
   def createWallet(self, addrPoolSize: int, passphrase: str, controlPassphrase: str,
      shortLabel: str, longLabel: str, extraEntropy: bytes):
      packet = Bridge.ToBridge.new_message()
      method = packet.init("utils").init("createWallet")

      method.lookup = addrPoolSize
      method.passphrase = passphrase
      method.controlPassphrase = controlPassphrase
      method.label = shortLabel
      method.description = longLabel

      if extraEntropy is not None:
         method.extraEntropy = extraEntropy

      fut = self.send(packet)
      reply = fut.getVal()
      if reply.success == False:
         raise BridgeError(
            f"[createWallet] failed with error: {reply.error}")
      return reply.utils.walletId

################################################################################
class BridgeWalletWrapper(ProtoWrapper):
   #############################################################################
   ## setup ##
   def __init__(self, walletId):
      super().__init__(TheBridge.bridgeSocket)
      self.walletId = walletId

   ####
   def getPacket(self):
      packet = Bridge.ToBridge.new_message()
      packet.init("wallet").id = self.walletId
      return packet

   #############################################################################
   ## commands ##
   def getBalanceAndCount(self):
      packet = self.getPacket()
      packet.wallet.getBalanceAndCount = None

      fut = self.send(packet)
      reply = fut.getVal()
      return reply.wallet.balanceAndCount

   ####
   def getAddrCombinedList(self):
      packet = self.getPacket()
      packet.wallet.getAddrCombinedList = None

      fut = self.send(packet)
      reply = fut.getVal()
      return reply.wallet.addressAndBalanceData

   ####
   def getHighestUsedIndex(self):
      packet = self.getPacket()
      packet.wallet.getHighestUsedIndex = None

      fut = self.send(packet)
      reply = fut.getVal()
      return reply.wallet.highestUsedIndex

   ####
   def extendAddressPool(self, progressId, count, callback):
      def finishExtension(reply, args):
         callbackThread = threading.Thread(
            group=None, target=args[0],
            name=None, args=[reply], kwargs={})
         callbackThread.start()

      packet = self.getPacket()
      method = packet.wallet.init("extendAddressPool")
      method.count = count
      method.callbackId = progressId
      self.send(packet, False, finishExtension, [callback])

   ####
   def createBackupStringForWallet(self,
      serverPushObj, callbackFunc, callbackArgs=[]):
      packet = self.getPacket()
      packet.wallet.createBackupString = serverPushObj.callbackId
      self.send(packet, callback=callbackFunc, cbArgs=callbackArgs)

   ####
   def setAddressTypeFor(self, assetId, addrType):
      packet = self.getPacket()
      method = packet.wallet.init("setAddressTypeFor")
      method.assetId = assetId
      method.addressType = addrType

      fut = self.send(packet)
      reply = fut.getVal()
      return reply.wallet.addressData

   ####
   def setComment(self, key, val):
      packet = self.getPacket()
      method = packet.wallet.init("setComment")
      method.hashKey = key
      method.comment = val
      self.send(packet, False)

   ####
   def setLabels(self, title, desc):
      packet = self.getPacket()
      method = packet.wallet.init("setLabels")
      method.title = title
      method.description = desc
      self.send(packet, False)

   ####
   def initCoinSelectionInstance(self, height):
      packet = self.getPacket()
      packet.wallet.setupNewCoinSelectionInstance = height

      fut = self.send(packet)
      reply = fut.getVal()
      return BridgeCoinSelectionWrapper(reply.wallet.coinSelectionId)

   ####
   def createAddressBook(self):
      packet = self.getPacket()
      packet.wallet.createAddressBook = None

      fut = self.send(packet)
      reply = fut.getVal()
      return reply.wallet.addressBook

   ####
   def getUtxos(self, value):
      packet = self.getPacket()
      method = packet.wallet.init("getUtxos")
      method.value = value

      fut = self.send(packet)
      reply = fut.getVal()
      return reply.wallet.utxoList

   ####
   def getNewAddress(self):
      packet = self.getPacket()
      packet.wallet.init("getAddress").new = None

      fut = self.send(packet)
      reply = fut.getVal()
      return reply.wallet.addressData

   ####
   def getChangeAddr(self):
      packet = self.getPacket()
      packet.wallet.init("getAddress").change = None

      fut = self.send(packet)
      reply = fut.getVal()
      return reply.wallet.addressData

   ####
   def peekChangeAddress(self):
      packet = self.getPacket()
      packet.wallet.init("getAddress").peekChange = None

      fut = self.send(packet)
      reply = fut.getVal()
      return reply.wallet.addressData

   ####
   def getData(self):
      packet = self.getPacket()
      
      walletData = Bridge.WalletData.new_message()
      method = packet.wallet.init("getData")
      method = walletData

      fut = self.send(packet)
      reply = fut.getVal()
      return reply.wallet.walletData

   ####
   def delete(self):
      packet = self.getPacket()
      packet.wallet.delete = None

      fut = self.send(packet)
      reply = fut.getVal()
      return reply.success

   ####
   def getLedgerDelegateIdForScrAddr(self, scrAddr: bytes):
      packet = self.getPacket()
      packet.wallet.getLedgerDelegateIdForSrcAddr = scrAddr

      fut = self.send(packet)
      reply = fut.getVal()
      return reply.wallet.ledgerDelegateId

################################################################################
class BridgeCoinSelectionWrapper(ProtoWrapper):
   #############################################################################
   ## setup ##
   def __init__(self, csId: str):
      super().__init__(TheBridge.bridgeSocket)
      self.coinSelectionId = csId

   ####
   def getPacket(self):
      packet = Bridge.ToBridge.new_message()
      packet.init("coinSelection").id = self.coinSelectionId
      return packet

   #############################################################################
   ## commands ##
   def destroyCoinSelectionInstance(self):
      packet = self.getPacket()
      packet.coinSelection.cleanup = None
      self.send(packet, False)

   #############################################################################
   def setCoinSelectionRecipient(self, addrStr: str, value: int, recId: int):
      packet = self.getPacket()
      method = packet.coinSelection.init("setRecipient")
      method.address = addrStr
      method.value = value
      method.id = recId

      fut = self.send(packet)
      reply = fut.getVal()
      if reply.success == False:
         raise BridgeError(
            f"[setCoinSelectionRecipient] failed with error: {reply.error}")

   #############################################################################
   def reset(self):
      packet = self.getPacket()
      packet.coinSelection.reset = None
      self.send(packet, False)

   #############################################################################
   def selectUTXOs(self, fee: int, feePerByte: float, processFlags: int):
      packet = self.getPacket()
      method = packet.coinSelection.init("selectUtxos")
      method.flags = processFlags

      if fee != 0:
         method.flatFee = fee
      else:
         method.feeByte = feePerByte

      fut = self.send(packet)
      reply = fut.getVal()
      if reply.success == False:
         raise BridgeError(f"[selectUTXOs] failed with error: {reply.error}")

   #############################################################################
   def getUtxoSelection(self):
      packet = self.getPacket()
      packet.coinSelection.getUtxoSelection = None

      fut = self.send(packet)
      reply = fut.getVal()
      return reply.coinSelection.utxoList

   #############################################################################
   def getFlatFee(self):
      packet = self.getPacket()
      packet.coinSelection.getFlatFee = None

      fut = self.send(packet)
      reply = fut.getVal()
      return reply.coinSelection.flatFee

   #############################################################################
   def getFeeByte(self):
      packet = self.getPacket()
      packet.coinSelection.getFeeByte = None

      fut = self.send(packet)
      reply = fut.getVal()
      return reply.coinSelection.feeByte

   #############################################################################
   def getSizeEstimate(self):
      packet = self.getPacket()
      packet.coinSelection.getSizeEstimate = None

      fut = self.send(packet)
      reply = fut.getVal()
      return reply.coinSelection.sizeEstimate

   #############################################################################
   def processCustomUtxoList(self, utxoList: list, fee: int, feePerByte: float, processFlags: int):
      packet = self.getPacket()
      method = packet.coinSelection.init("processCustomUtxoList")
      method.flags = processFlags

      methodUtxos = method.init("utxos", len(utxoList))
      for i, utxo in enumerate(utxoList):
         methodUtxos[i] = utxo.toBridgeUtxo()

      if fee != 0:
         method.flatFee = fee
      else:
         method.feeByte = feePerByte

      fut = self.send(packet)
      reply = fut.getVal()
      if reply.success == False:
         raise BridgeError("ProcessCustomUtxoList failed")

   #############################################################################
   def getFeeForMaxVal(self, feePerByte: float):
      packet = self.getPacket()
      method = packet.coinSelection.init("getFeeForMaxVal")
      method.feeByte = feePerByte

      fut = self.send(packet)
      reply = fut.getVal()
      return reply.coinSelection.flatFee

   #############################################################################
   def getFeeForMaxValUtxoVector(self, utxoList: list, feePerByte: float):
      packet = self.getPacket()
      method = packet.coinSelection.init("getFeeForMaxVal")
      method.feeByte = feePerByte

      methodUtxos = method.init("utxos", len(utxoList))
      for i, utxo in enumerate(utxoList):
         methodUtxos.append(utxo.toBridgeUtxo())

      fut = self.send(packet)
      reply = fut.getVal()
      return reply.coinSelection.flatFee

################################################################################
class ScriptUtils(ProtoWrapper):
   #############################################################################
   ## setup ##
   def __init__(self, bridgeSocket: BridgeSocket):
      super().__init__(bridgeSocket)

   ####
   def getPacket(self, script: bytes):
      packet = Bridge.ToBridge.new_message()
      packet.init("scriptUtils").script = script
      return packet

   #############################################################################
   ## commands ##
   def getTxOutScriptType(self, script: bytes):
      packet = self.getPacket(script)
      packet.scriptUtils.getTxOutScriptType = None

      fut = self.send(packet)
      reply = fut.getVal()
      return reply.scriptUtils.txOutScriptType

   ####
   def getTxInScriptType(self, script: bytes, hashVal: bytes):
      packet = self.getPacket(script)
      packet.scriptUtils.getTxInScriptType = hashVal

      fut = self.send(packet)
      reply = fut.getVal()
      return reply.scriptUtils.txInScriptType

   ####
   def getLastPushDataInScript(self, script: bytes):
      packet = self.getPacket(script)
      packet.scriptUtils.getLastPushDataInScript = None

      fut = self.send(packet)
      reply = fut.getVal()
      return reply.scriptUtils.pushData

   ####
   def getTxOutScriptForScrAddr(self, script: bytes):
      packet = self.getPacket(script)
      packet.scriptUtils.getTxOutScriptForScrAddr = script

      fut = self.send(packet)
      reply = fut.getVal()
      return reply.scriptUtils.scriptData

   ####
   def getScrAddrForScript(self, script: bytes):
      packet = self.getPacket(script)
      packet.scriptUtils.getSrcAddrForScript = None

      fut = self.send(packet)
      reply = fut.getVal()
      return reply.scriptUtils.scrAddr

   ####
   def getAddrStrForScrAddr(self, scrAddr):
      packet = self.getPacket(scrAddr)
      packet.scriptUtils.getAddrStrForScrAddr = None

      fut = self.send(packet)
      reply = fut.getVal()

      if reply.success == False:
         raise BridgeError(f"error in getAddrStrForScrAddr: {reply.error}")
      else:
         return reply.scriptUtils.addressString

################################################################################
class BridgeSigner(ProtoWrapper):
   #############################################################################
   ## setup ##
   def __init__(self):
      super().__init__(TheBridge.bridgeSocket)
      self.signerId = None

   ####
   def __del__(self):
      self.cleanup()

   ####
   def getPacket(self):
      if self.signerId == None or not self.signerId:
         raise BridgeSignerError("[BridgeSigner] missing signerId")

      packet = Bridge.ToBridge.new_message()
      packet.init("signer").id = self.signerId
      return packet

   #############################################################################
   def initNew(self):
      if self.signerId != None:
         raise BridgeSignerError("[initNew] signer already has an id")

      packet = Bridge.ToBridge.new_message()
      packetSigner = packet.init("signer")
      packetSigner.id = ""
      packetSigner.getNew = None

      fut = self.send(packet)
      reply = fut.getVal()
      self.signerId = reply.signer.signerId

   #############################################################################
   def cleanup(self):
      packet = self.getPacket()
      packet.signer.cleanup = None

      self.send(packet, False)
      self.signerId = None

   #############################################################################
   def setVersion(self, version: int):
      packet = self.getPacket()
      packet.signer.setVersion = version

      fut = self.send(packet)
      reply = fut.getVal()
      if reply.success == False:
         raise BridgeSignerError(
            f"[setVersion] failed with error: {reply.error}")

   #############################################################################
   def setLockTime(self, locktime: int):
      packet = self.getPacket()
      packet.signer.setLockTime = locktime

      fut = self.send(packet)
      reply = fut.getVal()
      if reply.success == False:
         raise BridgeSignerError(
            f"[setLockTime] failed with error: {reply.error}")

   #############################################################################
   def addSpenderByOutpoint(self, hashVal: bytes, txOutId: int, seq: int):
      packet = self.getPacket()
      method = packet.signer.init("addSpenderByOutpoint")
      method.hash = hashVal
      method.txOutId = txOutId
      method.sequence = seq

      fut = self.send(packet)
      reply = fut.getVal()
      if reply.success == False:
         raise BridgeSignerError(
            f"[addSpenderByOutpoint] failed with error: {reply.error}")

   #############################################################################
   def populateUtxo(self, hashVal: bytes, txOutId: int, value: int, script: bytes):
      packet = self.getPacket()
      method = packet.signer.init("populateUtxo")
      method.hash = hashVal
      method.txOutId = txOutId
      method.value = value
      method.script = script

      fut = self.send(packet)
      reply = fut.getVal()
      if reply.success == False:
         raise BridgeSignerError(
            f"[addSpenderByOutpoint] failed with error: {reply.error}")

   #############################################################################
   def addSupportingTx(self, rawTxData: bytes):
      packet = self.getPacket()
      packet.signer.addSupportingTx = rawTxData

      fut = self.send(packet)
      reply = fut.getVal()
      if reply.success == False:
         raise BridgeSignerError(
            f"[addSupportingTx] failed with error: {reply.error}")

   #############################################################################
   def addRecipient(self, value: int, script: bytes):
      packet = self.getPacket()
      method = packet.signer.init("addRecipient")
      method.value = value
      method.script = script

      fut = self.send(packet)
      reply = fut.getVal()
      if reply.success == False:
         raise BridgeSignerError(
            f"[addRecipient] failed with error: {reply.error}")

   #############################################################################
   def toTxSigCollect(self, ustxType: int):
      packet = self.getPacket()
      packet.signer.toTxSigCollect = ustxType

      fut = self.send(packet)
      reply = fut.getVal()
      return reply.signer.txSigCollect

   #############################################################################
   def fromTxSigCollect(self, txSigCollect: str):
      packet = self.getPacket()
      packet.signer.fromTxSigCollect = txSigCollect

      fut = self.send(packet)
      reply = fut.getVal()
      if reply.success == False:
         raise BridgeSignerError(
            f"[fromTxSigCollect] failed with error: {reply.error}")

   #############################################################################
   def resolve(self, wltId: str):
      packet = self.getPacket()
      packet.signer.resolve = wltId

      fut = self.send(packet)
      reply = fut.getVal()
      if reply.success == False:
         raise BridgeSignerError(f"[resolve] failed with error: {reply.error}")

   #############################################################################
   def signTx(self, wltId: str, serverPushObj, callbackFunc, callbackArgs=[]):
      packet = self.getPacket()
      method = packet.signer.init("signTx")
      method.walletId = wltId
      method.callbackId = serverPushObj.callbackId
      self.send(packet, callback=callbackFunc, cbArgs=callbackArgs)

   #############################################################################
   def getSignedTx(self):
      packet = self.getPacket()
      packet.signer.getSignedTx = None

      fut = self.send(packet)
      reply = fut.getVal()
      if reply.success == False:
         return None
      return reply.signer.txData

   #############################################################################
   def getUnsignedTx(self):
      packet = self.getPacket()
      packet.signer.getUnsignedTx = None

      fut = self.send(packet)
      reply = fut.getVal()
      if reply.success == False:
         return None
      return reply.signer.txData

   #############################################################################
   def getSignedStateForInput(self, inputId: int):
      packet = self.getPacket()
      packet.signer.getSignedStateForInput = inputId

      fut = self.send(packet)
      reply = fut.getVal()
      return reply.signer.inputSignedState

   #############################################################################
   def fromType(self):
      packet = self.getPacket()
      packet.signer.fromType = None

      fut = self.send(packet)
      reply = fut.getVal()
      return reply.signer.fromType

   #############################################################################
   def canLegacySerialize(self):
      packet = self.getPacket()
      packet.signer.canLegacySerialize = None

      fut = self.send(packet)
      reply = fut.getVal()
      return reply.success

################################################################################
class ArmoryBridge(object):

   #############################################################################
   def __init__(self):
      self.blockTimeByHeightCache = {}
      self.bridgeSocket = BridgeSocket()

      self.service      = BlockchainService(self.bridgeSocket)
      self.utils        = BlockchainUtils(self.bridgeSocket)
      self.scriptUtils  = ScriptUtils(self.bridgeSocket)

   #############################################################################
   def start(self, stringArgs, notifyReadyLbd):
      self.bridgeSocket.start(stringArgs, notifyReadyLbd)

   #############################################################################
   def send(self, msg, needsReply=True, callback=None, cbArgs=[],
      msgType=BRIDGE_CLIENT_HEADER):
      self.bridgeSocket.sendToBridgeProto(msg,
         needsReply, callback, cbArgs, msgType)

   #############################################################################
   def pushNotification(self, callbackData):
      notifThread = threading.Thread(\
         group=None, target=TheBDM.pushNotification, \
         name=None, args=[callbackData], kwargs={})
      notifThread.start()

   #############################################################################
   def pushProgressNotification(self, data):
      payload = Bridge.Notification.new_message()
      payload.from_bytes_packed()

      TheBDM.reportProgress(payload)

   #############################################################################
   def getBlockTimeByHeight(self, height):
      if height in self.blockTimeByHeightCache:
         return self.blockTimeByHeightCache[height]

      packet = Bridge.ToBridge.new_message()
      packet.init("service").getBlockTimeByHeight = height

      fut = self.send(packet)
      socketResponse = fut.getVal()

      response = BridgeProto_pb2.ReplyNumbers()
      response.ParseFromString(socketResponse)

      blockTime = response.ints[0]

      if blockTime == 2**32 - 1:
         raise BridgeError("invalid block time")

      self.blockTimeByHeightCache[height] = blockTime
      return blockTime

   #############################################################################
   def restoreWallet(self, root: list[str], chaincode: list[str], spPass: str, callbackId):
      opaquePayload = Bridge.RestoreWalletPayload.new_message()
      
      payloadRoot = opaquePayload.init("root", len(root))
      for i, r in enumerate(root):
         payloadRoot[i] = r

      payloadSecondary = opaquePayload.init("secondary", len(chaincode))
      for i, c in enumerate(chaincode):
         payloadSecondary[i] = c

      opaquePayload.spPass = spPass

      packet = Bridge.ToBridge.new_message()
      packet.method = BridgeProto_pb2.methodWithCallback
      packet.methodWithCallback = BridgeProto_pb2.restoreWallet

      packet.byteArgs.append(callbackId)
      packet.byteArgs.append(opaquePayload.SerializeToString())

      self.send(packet, False)

   #############################################################################
   def getHistoryForWalletSelection(self, wltIDList, order):
      packet = Bridge.ToBridge.new_message()
      packet.method = BridgeProto_pb2.getHistoryForWalletSelection
      packet.stringArgs.append(order)
      for wltID in wltIDList:
         packet.stringArgs.append(wltID)

      fut = self.send(packet)
      socketResponse = fut.getVal()

      response = BridgeProto_pb2.BridgeLedgers()
      response.ParseFromString(socketResponse)

      return response

################################################################################
class CallbackWrapper(object):
   def __init__(self, callbackFunc, callbackArgs=[]):
      self.callbackFunc = callbackFunc
      self.callbackArgs = callbackArgs

   def execute(self, replyObj):
      callbackThread = threading.Thread(
         group=None, target=self.callbackFunc,
         name=None, args=[*self.callbackArgs, replyObj], kwargs={})
      callbackThread.start()

################################################################################
class ServerPush(ProtoWrapper):
   def __init__(self, callbackId=""):
      super().__init__(TheBridge.bridgeSocket)

      if len(callbackId) == 0:
         self.callbackId = base64.b16encode(os.urandom(10)).decode('utf-8')
      else:
         self.callbackId = callbackId
      self.bridgeSocket.setCallback(self.callbackId, self)

      self.refId = 0
      self.packet = None

   def parseProtoPacket(self, protoPacket):
      raise Exception("override me")

   def process(self, protoPacket):
      if protoPacket.which() == "cleanup":
         self.bridgeSocket.unsetCallback(self.callbackId)

      self.refId = protoPacket.referenceId
      self.parseProtoPacket(protoPacket)

   def getNewPacket(self):
      self.packet = Bridge.ToBridge.new_message()
      self.packet.callbackReply.referenceId = self.refId
      self.refId = 0
      return self.packet.callbackReply

   def reply(self):
      self.send(self.packet, needsReply=False)
      self.packet = None



####
TheBridge = ArmoryBridge()
