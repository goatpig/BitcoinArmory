################################################################################
#                                                                              #
# Copyright (C) 2019-2024, goatpig.                                            #
#  Distributed under the MIT license                                           #
#  See LICENSE-MIT or https://opensource.org/licenses/MIT                      #
#                                                                              #
################################################################################

from __future__ import (absolute_import, division, annotations,
                        print_function, unicode_literals)
import os
import errno
import socket
from armoryengine.ArmoryUtils import LOGDEBUG, LOGERROR, LOGWARN, hash256, LOGINFO
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

import sys
sys.path.append("cppForSwig/capnp")

import capnp
import Bridge_capnp as Bridge
import Types_capnp as Types

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
   def getVal(self, nothrow=False):
      self.cv.acquire()
      while self.has is False:
         self.cv.wait()
      self.cv.release()
      if self.data.success == False and not nothrow:
         raise Exception(self.data.error)
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

      import random
      self.port = random.randint(50000, 60000)

   ####
   def setCallback(self, key, callback):
      self.callbackDict[key] = callback

   def unsetCallback(self, key):
      if key in self.callbackDict:
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
      os.environ['BRIDGE_PORT'] = str(self.port)
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
      self.listenSocket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
      self.listenSocket.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR, 1)
      self.listenSocket.bind(("127.0.0.1", self.port))
      self.listenSocket.listen()

      clientSocket, clientIP = self.listenSocket.accept()
      return clientSocket

   ####
   def spawnBridge(self, args: list):
      subprocess.run(["./build/CppBridge", *args])

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
      callbackFunc: callable, callbackArgs: list, msgType):
      msg.referenceId = self.idCounter
      self.idCounter = self.idCounter + 1

      payload = msg.to_bytes()
      result = self.sendToBridgeBinary(payload, msg.referenceId,
         needsReply, callbackFunc, callbackArgs, msgType)

      if needsReply:
         return result

   ####
   def sendToBridgeBinary(self, payload, payloadId,
      needsReply=True, callback: callable=None, cbArgs: list=[],
      msgType = BRIDGE_CLIENT_HEADER):

      #grab id from msg counter
      if self.run == False:
         return

      #serialize payload
      bp = BinaryPacker()

      #payload type header
      bp.put(UINT8, msgType)

      #serialized proto message
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
            payload += self.pollRecv(
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

         #grab full packet
         fullPacket = response[4:]

         #deser proto reply
         with Bridge.FromBridge.from_bytes(fullPacket) as protoPayload:
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

               #grab the future, delete it from dict
               replyHandler = self.responseDict[referenceId]
               del self.responseDict[referenceId]

               #release lock & fill the promise
               self.rwLock.release()
               if isinstance(replyHandler, PyPromFut):
                  replyHandler.setVal(reply)
               elif isinstance(replyHandler, CallbackWrapper):
                  replyHandler.execute(reply)
               else:
                  LOGWARN(f"unexpected callback type: {type(replyHandler)}")

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
      packet.init("service").getLedgerDelegateId = None

      fut = self.send(packet)
      response = fut.getVal()
      return response.service.getLedgerDelegateId

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
      packet.init("service").getNodeStatus = None

      fut = self.send(packet)
      reply = fut.getVal()
      return reply.service.getNodeStatus

   ####
   def registerWallet(self, walletId: str, accountId: str, isNew: bool):
      packet = Bridge.ToBridge.new_message()
      packetMethod = packet.init("service").init("registerWallet")
      packetMethod.walletId = walletId
      packetMethod.accountId = accountId
      packetMethod.isNew = isNew
      self.send(packet, needsReply=False)

   ####
   def getHistoryPageForDelegate(self, delegateId: str, pageId: int):
      packet = Bridge.ToBridge.new_message()
      # TODO: Incomplete: HistoryPageRequest is unused
      request = packet.init("delegate")
      request.id = delegateId
      pages = request.init("getPages")
      pages.first = pageId

      fut = self.send(packet)
      reply = fut.getVal()
      return reply.delegate.getPages

   ####
   def getTxsByHash(self, hashVals: list[bytes]):
      packet = Bridge.ToBridge.new_message()
      serviceReq = packet.init("service")
      packet.service.init("getTxsByHash", len(hashVals))
      for i, hashVal in enumerate(hashVals):
         if not hashVal:
            LOGWARN("trying to fetch tx by empty hash")
            continue
         packet.service.getTxsByHash[i] = hashVal

      fut = self.send(packet)
      reply = fut.getVal()
      if reply.success == False:
         return None
      txList = reply.service.getTxsByHash
      txDict = {}
      for tx in txList:
         txDict[tx.hash] = tx
      return txDict

   ####
   def getHeadersByHeight(self, heights: list[int]):
      packet = Bridge.ToBridge.new_message()
      packetHeights = packet.init("service").init("getHeadersByHeight", len(heights))
      for i, height in enumerate(heights):
         packetHeights[i] = height

      fut = self.send(packet)
      reply = fut.getVal()
      return reply.service.getHeadersByHeight

   ####
   def getFeeSchedule(self, strat):
      packet = Bridge.ToBridge.new_message()
      packet.init("service").getFeeSchedule = strat

      fut = self.send(packet)
      reply = fut.getVal()
      if reply.success == False:
         raise Exception(reply.error)
      return reply.service.getFeeSchedule

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
      addrTypeStr = reply.utils.getNameForAddrType
      self.addrTypeStrByType[addrType] = addrTypeStr
      return addrTypeStr

   ####
   def getHash160(self, data: bytes):
      packet = Bridge.ToBridge.new_message()
      packet.init("utils").getHash160 = data

      fut = self.send(packet)
      reply = fut.getVal()
      return reply.utils.getHash160

   ####
   def generateRandomHex(self, size: int):
      packet = Bridge.ToBridge.new_message()
      packet.init("utils").generateRandomHex = size

      fut = self.send(packet)
      reply = fut.getVal()
      return reply.utils.generateRandomHex

   #############################################################################
   def createWallet(self, addrPoolSize: int,
      shortLabel: str, longLabel: str, extraEntropy: bytes,
      callbackId: str, successCb: callable):
      packet = Bridge.ToBridge.new_message()

      method = packet.init("utils").init("createWallet")
      method.callbackId = callbackId
      method.lookup = addrPoolSize
      method.label = shortLabel
      method.description = longLabel
      if extraEntropy is not None:
         method.extraEntropy = extraEntropy
      self.send(packet, callback=successCb)

   #############################################################################
   def restoreWallet(self, root: list[str], chaincode: list[str], spPass: str,
      callbackId: str, successCb: callable):
      restoreStruct = Bridge.UtilsRequest.RestoreWalletStruct.new_message()
      restoreStruct.callbackId = callbackId

      #root
      if len(root) > 0:
         payloadRoot = restoreStruct.init("root", len(root))
         for i, r in enumerate(root):
            payloadRoot[i] = r

      #chaincode
      if len(chaincode) > 0:
         payloadChaincode = restoreStruct.init("chaincode", len(chaincode))
         for i, c in enumerate(chaincode):
            payloadChaincode[i] = c

      #passphrase
      if spPass:
         restoreStruct.spPass = spPass

      packet = Bridge.ToBridge.new_message()
      utilsRequest = packet.init("utils")
      utilsRequest.restoreWallet = restoreStruct
      self.send(packet, callback=successCb)

   #############################################################################
   def importWallet(self, walletPath):
      packet = Bridge.ToBridge.new_message()
      utilsRequest = packet.init("utils")
      utilsRequest.importWallet = walletPath

      fut = self.send(packet)
      reply = fut.getVal()
      return reply.utils.importWallet

################################################################################
class WalletManagerWrapper(ProtoWrapper):
   #############################################################################
   ## setup ##
   def __init__(self, bridgeSocket):
      super().__init__(bridgeSocket)

   #############################################################################
   ## commands ##
   def listWallets(self):
      packet = Bridge.ToBridge.new_message()
      packet.init("walletManager").listWallets = None

      fut = self.send(packet)
      reply = fut.getVal()
      return reply.walletManager.listWallets

   ####
   def stageWallet(self, walletId: str, stage: bool):
      packet = Bridge.ToBridge.new_message()
      request = packet.init("walletManager").init("stageWallet")
      request.walletId = walletId
      request.stage = stage

      fut = self.send(packet)
      reply = fut.getVal(nothrow=True)
      return reply.success

   ####
   def loadWallets(self):
      packet = Bridge.ToBridge.new_message()
      packet.init("walletManager").loadWallets = None

      fut = self.send(packet)
      reply = fut.getVal(nothrow=True)
      return reply

   ####
   def migrateWallet(self, walletPath: str, callbackId: str, callbackFunc: callable):
      packet = Bridge.ToBridge.new_message()
      request = packet.init("walletManager").init("migrateWallet")
      request.walletPath = walletPath
      request.callbackId = callbackId
      self.send(packet, callback=callbackFunc)

   ####
   def deleteWallet(self, walletId):
      packet = Bridge.ToBridge.new_message()
      request = packet.init("walletManager")
      request.deleteWallet = walletId

      fut = self.send(packet)
      reply = fut.getVal(nothrow=True)
      return reply.success

   ####
   def unlockControlHeader(self, 
      walletPath: str, callbackId: str, callbackFunc: callable):
      """Unlock wallet using proper unlock control header pattern."""
      packet = Bridge.ToBridge.new_message()
      request = packet.init("walletManager").init("unlockControlHeader")
      request.walletPath = walletPath
      request.callbackId = callbackId
      self.send(packet, callback=callbackFunc)

################################################################################
class BridgeWalletWrapper(ProtoWrapper):
   #############################################################################
   ## setup ##
   def __init__(self, walletId, accountId):
      super().__init__(TheBridge.bridgeSocket)
      self.walletId = walletId
      self.accountId = accountId

   ####
   def _getPacket(self):
      packet = Bridge.ToBridge.new_message()
      wltCapn = packet.init("wallet")
      wltCapn.walletId  = self.walletId
      if self.accountId:
         wltCapn.accountId = self.accountId
      return packet

   #############################################################################
   ## commands ##
   def getBalanceAndCount(self):
      packet = self._getPacket()
      packet.wallet.getBalanceAndCount = None

      fut = self.send(packet)
      reply = fut.getVal()
      if reply.success == False:
         raise Exception(f"getBalanceAndCount failed with error: {reply.error}")
      return reply.wallet.getBalanceAndCount

   ####
   def getAddrCombinedList(self):
      packet = self._getPacket()
      packet.wallet.getAddrCombinedList = None

      fut = self.send(packet)
      reply = fut.getVal()
      return reply.wallet.getAddrCombinedList

   ####
   def getHighestUsedIndex(self):
      packet = self._getPacket()
      packet.wallet.getHighestUsedIndex = None

      fut = self.send(packet)
      reply = fut.getVal()
      return reply.wallet.getHighestUsedIndex

   ####
   def extendAddressPool(self, count: int,
      isNew: bool, progressId: str, callback: callable):
      packet = self._getPacket()
      method = packet.wallet.init("extendAddressPool")
      method.count = count
      method.isNew = isNew
      method.callbackId = progressId
      self.send(packet, callback=callback)

   ####
   def createBackupStringForWallet(self,
      callbackFunc, passphrase, serverPushObj):
      packet = self._getPacket()
      req = packet.wallet.init("createBackupString")
      if passphrase:
         req.passphrase = passphrase
      elif serverPushObj:
         req.callbackId = serverPushObj.callbackId
      else:
         raise Exception("[createBackupStringForWallet] invalid args")
      self.send(packet, callback=callbackFunc)

   ####
   def setAddressTypeFor(self, assetId, addrType):
      packet = self._getPacket()
      method = packet.wallet.init("setAddressTypeFor")
      method.assetId = assetId
      method.addressType = addrType

      fut = self.send(packet)
      reply = fut.getVal()
      return reply.wallet.setAddressTypeFor

   ####
   def setComment(self, key, val):
      packet = self._getPacket()
      method = packet.wallet.init("setComment")
      method.key = key
      method.comment = val
      self.send(packet, False)

   ####
   def setLabels(self, title, desc):
      packet = self._getPacket()
      method = packet.wallet.init("setLabels")
      method.title = title
      method.description = desc
      self.send(packet, False)

   ####
   def initCoinSelectionInstance(self, height):
      packet = self._getPacket()
      packet.wallet.setupNewCoinSelectionInstance = height

      fut = self.send(packet)
      reply = fut.getVal()
      return BridgeCoinSelectionWrapper(
         reply.wallet.setupNewCoinSelectionInstance)

   ####
   def createAddressBook(self):
      packet = self._getPacket()
      packet.wallet.createAddressBook = None

      fut = self.send(packet)
      reply = fut.getVal()
      return reply.wallet.createAddressBook

   ####
   def getUtxos(self, value=0, zc=False, rbf=False):
      packet = self._getPacket()
      request = packet.wallet.init("getUtxos")
      if value > 0:
         request.value = value
      elif zc == True:
         request.zc = None
      elif rbf == True:
         request.rbf = None

      fut = self.send(packet)
      reply = fut.getVal()
      return reply.wallet.getUtxos

   ####
   def getNewAddress(self, addrType):
      packet = self._getPacket()
      request = packet.wallet.init("getAddress")
      request.type = addrType
      request.new = None

      fut = self.send(packet)
      reply = fut.getVal()
      return reply.wallet.getAddress

   ####
   def getChangeAddr(self, addrType):
      packet = self._getPacket()
      request = packet.wallet.init("getAddress")
      request.type = addrType
      request.change = None

      fut = self.send(packet)
      reply = fut.getVal()
      return reply.wallet.getAddress

   ####
   def peekChangeAddress(self, addrType):
      packet = self._getPacket()
      request = packet.wallet.init("getAddress")
      request.type = addrType
      request.peekChange = None

      fut = self.send(packet)
      reply = fut.getVal()
      return reply.wallet.getAddress

   ####
   def getData(self):
      packet = self._getPacket()

      walletData = Bridge.WalletData.new_message()
      method = packet.wallet.getData = None

      fut = self.send(packet)
      reply = fut.getVal()
      return reply.wallet.getData

   ####
   def getLedgerDelegateIdForScrAddr(self, scrAddr: bytes):
      packet = self._getPacket()
      packet.wallet.getLedgerDelegateIdForScrAddr = scrAddr

      fut = self.send(packet)
      reply = fut.getVal()
      return reply.wallet.getLedgerDelegateIdForScrAddr

   ####
   def getUnlockTime(self, callback: callable):
      packet = self._getPacket()
      packet.wallet.getUnlockTime = None
      self.send(packet, callback=callback)

   ####
   def changePassphrase(self, isPriv: bool,
      callbackId: str, callback: callable):
      packet = self._getPacket()
      changeRequest = packet.wallet.init('changePassphrase')
      changeRequest.callbackId = callbackId
      if isPriv:
         changeRequest.private = None
      else:
         changeRequest.control = None
      self.send(packet, callback=callback)

################################################################################
class BridgeCoinSelectionWrapper(ProtoWrapper):
   #############################################################################
   ## setup ##
   def __init__(self, csId: str):
      super().__init__(TheBridge.bridgeSocket)
      self.coinSelectionId = csId

   ####
   def _getPacket(self):
      packet = Bridge.ToBridge.new_message()
      packet.init("coinSelection").id = self.coinSelectionId
      return packet

   #############################################################################
   ## commands ##
   def destroyCoinSelectionInstance(self):
      packet = self._getPacket()
      packet.coinSelection.cleanup = None
      self.send(packet, False)

   #############################################################################
   def setCoinSelectionRecipient(self, addrStr: str, value: int, recId: int):
      packet = self._getPacket()
      method = packet.coinSelection.init("setRecipient")
      method.address = addrStr
      method.value = value
      method.id = recId

      fut = self.send(packet)
      reply = fut.getVal(nothrow=True)
      if reply.success == False:
         raise BridgeError(
            f"[setCoinSelectionRecipient] failed with error: {reply.error}")

   #############################################################################
   def reset(self):
      packet = self._getPacket()
      packet.coinSelection.reset = None
      self.send(packet, False)

   #############################################################################
   def selectUTXOs(self, fee: int, feePerByte: float, processFlags: int):
      packet = self._getPacket()
      method = packet.coinSelection.init("selectUtxos")
      method.flags = processFlags

      if fee != 0:
         method.flatFee = fee
      else:
         method.feeByte = feePerByte

      fut = self.send(packet)
      reply = fut.getVal(nothrow=True)
      if reply.success == False:
         raise BridgeError(f"[selectUTXOs] failed with error: {reply.error}")

   #############################################################################
   def getUtxoSelection(self):
      packet = self._getPacket()
      packet.coinSelection.getUtxoSelection = None

      fut = self.send(packet)
      reply = fut.getVal()
      return reply.coinSelection.getUtxoSelection

   #############################################################################
   def getFlatFee(self):
      packet = self._getPacket()
      packet.coinSelection.getFlatFee = None

      fut = self.send(packet)
      reply = fut.getVal()
      return reply.coinSelection.getFlatFee

   #############################################################################
   def getFeeByte(self):
      packet = self._getPacket()
      packet.coinSelection.getFeeByte = None

      fut = self.send(packet)
      reply = fut.getVal()
      return reply.coinSelection.getFeeByte

   #############################################################################
   def getSizeEstimate(self):
      packet = self._getPacket()
      packet.coinSelection.getSizeEstimate = None

      fut = self.send(packet)
      reply = fut.getVal()
      return reply.coinSelection.getSizeEstimate

   #############################################################################
   def processCustomUtxoList(self, utxoList: list, fee: int, feePerByte: float, processFlags: int):
      packet = self._getPacket()
      request = packet.coinSelection.init("processCustomUtxoList")
      request.flags = processFlags

      capnUtxos = request.init("utxos", len(utxoList))
      for utxo, capnUtxo in zip(utxoList, capnUtxos):
         utxo.toBridgeUtxo(capnUtxo)

      if fee != 0:
         request.flatFee = fee
      else:
         request.feeByte = feePerByte

      fut = self.send(packet)
      reply = fut.getVal(nothrow=True)
      if reply.success == False:
         raise BridgeError("ProcessCustomUtxoList failed")

   #############################################################################
   def getFeeForMaxVal(self, feePerByte: float):
      packet = self._getPacket()
      method = packet.coinSelection.init("getFeeForMaxVal")
      method.feeByte = feePerByte

      fut = self.send(packet)
      reply = fut.getVal()
      return reply.coinSelection.getFeeForMaxVal

   #############################################################################
   def getFeeForMaxValUtxoVector(self, utxoList: list, feePerByte: float):
      packet = self._getPacket()
      request = packet.coinSelection.init("getFeeForMaxVal")
      request.feeByte = feePerByte

      capnUtxos = request.init("utxos", len(utxoList))
      for utxo, capnUtxo in zip(utxoList, capnUtxos):
         utxo.toBridgeUtxo(capnUtxo)

      fut = self.send(packet)
      reply = fut.getVal()
      return reply.coinSelection.getFeeForMaxVal

################################################################################
class ScriptUtils(ProtoWrapper):
   #############################################################################
   ## setup ##
   def __init__(self, bridgeSocket: BridgeSocket):
      super().__init__(bridgeSocket)

   ####
   def _getPacket(self, script: bytes):
      packet = Bridge.ToBridge.new_message()
      packet.init("scriptUtils").script = script
      return packet

   #############################################################################
   ## commands ##
   def getTxOutScriptType(self, script: bytes):
      packet = self._getPacket(script)
      packet.scriptUtils.getTxOutScriptType = None

      fut = self.send(packet)
      reply = fut.getVal()
      return reply.scriptUtils.getTxOutScriptType

   ####
   def getTxInScriptType(self, script: bytes, hashVal: bytes):
      packet = self._getPacket(script)
      packet.scriptUtils.getTxInScriptType = hashVal

      fut = self.send(packet)
      reply = fut.getVal()
      return reply.scriptUtils.getTxInScriptType

   ####
   def getLastPushDataInScript(self, script: bytes):
      packet = self._getPacket(script)
      packet.scriptUtils.getLastPushDataInScript = None

      fut = self.send(packet)
      reply = fut.getVal()
      if reply.success == False:
         return b''
      return reply.scriptUtils.getLastPushDataInScript

   ####
   def getTxOutScriptForScrAddr(self, script: bytes):
      packet = self._getPacket(script)
      packet.scriptUtils.getTxOutScriptForScrAddr = None

      fut = self.send(packet)
      reply = fut.getVal()
      return reply.scriptUtils.getTxOutScriptForScrAddr

   ####
   def getScrAddrForScript(self, script: bytes):
      packet = self._getPacket(script)
      packet.scriptUtils.getScrAddrForScript = None

      fut = self.send(packet)
      reply = fut.getVal()
      return reply.scriptUtils.getScrAddrForScript

   ####
   def getAddrStrForScrAddr(self, scrAddr):
      packet = self._getPacket(scrAddr)
      packet.scriptUtils.getAddrStrForScrAddr = None

      fut = self.send(packet)
      reply = fut.getVal(nothrow=True)

      if reply.success == False:
         raise BridgeError(f"error in getAddrStrForScrAddr: {reply.error}")
      else:
         return reply.scriptUtils.getAddrStrForScrAddr

   def getScrAddrForAddrStr(self, addrStr):
      packet = Bridge.ToBridge.new_message()
      request = packet.init("scriptUtils")
      request.getScrAddrForAddrStr = addrStr

      fut = self.send(packet)
      reply = fut.getVal(nothrow=True)

      if reply.success == False:
         raise BridgeError(f"error in getAddrStrForScrAddr: {reply.error}")
      else:
         return reply.scriptUtils.getScrAddrForAddrStr

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
   def _getPacket(self):
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
      packetSigner.getNew = None

      fut = self.send(packet)
      reply = fut.getVal()
      if reply.success == False:
         raise Exception("failed to init new signer!")
      self.signerId = reply.signer.getNew

   #############################################################################
   def cleanup(self):
      if not self.signerId:
         return
      packet = self._getPacket()
      packet.signer.cleanup = None

      self.send(packet, False)
      self.signerId = None

   #############################################################################
   def setVersion(self, version: int):
      packet = self._getPacket()
      packet.signer.setVersion = version

      fut = self.send(packet)
      reply = fut.getVal()
      if reply.success == False:
         raise BridgeSignerError(
            f"[setVersion] failed with error: {reply.error}")

   #############################################################################
   def setLockTime(self, locktime: int):
      packet = self._getPacket()
      packet.signer.setLockTime = locktime

      fut = self.send(packet)
      reply = fut.getVal()
      if reply.success == False:
         raise BridgeSignerError(
            f"[setLockTime] failed with error: {reply.error}")

   #############################################################################
   def addSpenderByOutpoint(self, hashVal: bytes, txOutId: int, seq: int):
      packet = self._getPacket()
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
      packet = self._getPacket()
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
      packet = self._getPacket()
      packet.signer.addSupportingTx = rawTxData

      fut = self.send(packet)
      reply = fut.getVal()
      if reply.success == False:
         raise BridgeSignerError(
            f"[addSupportingTx] failed with error: {reply.error}")

   #############################################################################
   def addRecipient(self, value: int, script: bytes):
      packet = self._getPacket()
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
      packet = self._getPacket()
      packet.signer.toTxSigCollect = ustxType

      fut = self.send(packet)
      reply = fut.getVal()
      return reply.signer.toTxSigCollect

   #############################################################################
   def fromTxSigCollect(self, txSigCollect: str):
      packet = self._getPacket()
      packet.signer.fromTxSigCollect = txSigCollect

      fut = self.send(packet)
      reply = fut.getVal()
      if reply.success == False:
         raise BridgeSignerError(
            f"[fromTxSigCollect] failed with error: {reply.error}")

   #############################################################################
   def resolve(self, wltId: str):
      packet = self._getPacket()
      packet.signer.resolve = wltId

      fut = self.send(packet)
      reply = fut.getVal()
      if reply.success == False:
         raise BridgeSignerError(f"[resolve] failed with error: {reply.error}")

   #############################################################################
   def signTx(self, wltId: str, serverPushObj, callbackFunc, callbackArgs=[]):
      packet = self._getPacket()
      method = packet.signer.init("signTx")
      method.walletId = wltId
      method.callbackId = serverPushObj.callbackId
      self.send(packet, callback=callbackFunc, cbArgs=callbackArgs)

   #############################################################################
   def getSignedTx(self):
      packet = self._getPacket()
      packet.signer.getSignedTx = None

      fut = self.send(packet)
      reply = fut.getVal()
      if reply.success == False:
         return None
      return reply.signer.getSignedTx

   #############################################################################
   def getUnsignedTx(self):
      packet = self._getPacket()
      packet.signer.getUnsignedTx = None

      fut = self.send(packet)
      reply = fut.getVal()
      if reply.success == False:
         return None
      return reply.signer.getUnsignedTx

   #############################################################################
   def getSignedStateForInput(self, inputId: int):
      packet = self._getPacket()
      packet.signer.getSignedStateForInput = inputId

      fut = self.send(packet)
      reply = fut.getVal()
      return reply.signer.getSignedStateForInput

   #############################################################################
   def fromType(self):
      packet = self._getPacket()
      packet.signer.fromType = None

      fut = self.send(packet)
      reply = fut.getVal()
      return reply.signer.fromType

   #############################################################################
   def canLegacySerialize(self):
      packet = self._getPacket()
      packet.signer.canLegacySerialize = None

      fut = self.send(packet)
      reply = fut.getVal()
      return reply.success

################################################################################
class ArmoryBridge(object):
   def __init__(self):
      self.blockTimeByHeightCache = {}
      self.bridgeSocket = BridgeSocket()

      self.service      = BlockchainService(self.bridgeSocket)
      self.utils        = BlockchainUtils(self.bridgeSocket)
      self.scriptUtils  = ScriptUtils(self.bridgeSocket)
      self.wltManager   = WalletManagerWrapper(self.bridgeSocket)

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

      if not callbackId:
         self.callbackId = base64.b16encode(os.urandom(10)).decode('utf-8')
      else:
         self.callbackId = callbackId
      self.bridgeSocket.setCallback(self.callbackId, self)

      self.refId = 0
      self.packet = None

   def __del__(self):
      self.bridgeSocket.unsetCallback(self.callbackId)

   def parseProtoPacket(self, protoPacket):
      raise Exception("override me")

   def process(self, protoPacket):
      self.refId = protoPacket.counter
      self.parseProtoPacket(protoPacket)

   def getNewPacket(self):
      self.packet = Bridge.ToBridge.new_message()
      notif = self.packet.init('notification')
      notif.counter = self.refId
      self.refId = 0
      return self.packet.notification

   def reply(self):
      self.send(self.packet, needsReply=False)
      self.packet = None

####
TheBridge = ArmoryBridge()
