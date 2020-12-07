from __future__ import (absolute_import, division,
                        print_function, unicode_literals)
#interface with cpp code over os pipe
import errno
import socket
from armoryengine import ClientProto_pb2
from armoryengine.ArmoryUtils import LOGDEBUG, LOGERROR
from armoryengine.BDM import TheBDM
from armoryengine.BinaryPacker import BinaryPacker, UINT32, BINARY_CHUNK, VAR_INT
from struct import unpack
import atexit
import threading
import binascii
import subprocess

from concurrent.futures import ThreadPoolExecutor

from armoryengine.ArmoryUtils import PassphraseError

CPP_BDM_NOTIF_ID = 2**32 -1
CPP_PROGRESS_NOTIF_ID = 2**32 -2
CPP_PROMPT_USER_ID = 2**32 -3

#################################################################################
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


#################################################################################
class CppBridge(object):

   #############################################################################
   def __init__(self):
      self.blockTimeByHeightCache = {}
      pass

   #############################################################################
   def start(self, listArgs):
      self.run = True
      self.rwLock = threading.Lock()

      self.idCounter = 0
      self.responseDict = {}

      #dtor emulation to gracefully close sockets
      atexit.register(self.shutdown)

      self.executor = ThreadPoolExecutor(max_workers=2)
      listenFut = self.executor.submit(self.listenOnBridge)
      self.processFut = self.executor.submit(self.spawnBridge, listArgs)

      self.clientSocket = listenFut.result()
      self.clientFut = self.executor.submit(self.readBridgeSocket)

   #############################################################################
   def listenOnBridge(self):
      #setup listener

      portNumber = 46122
      self.listenSocket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
      self.listenSocket.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR, 1)
      self.listenSocket.bind(("127.0.0.1", portNumber))
      self.listenSocket.listen()

      clientSocket, clientIP = self.listenSocket.accept()
      return clientSocket

   #############################################################################
   def spawnBridge(self, listArgs):
      subprocess.run(["./CppBridge"] + listArgs)

   #############################################################################
   def sendToBridge(self, msg, needsReply=True, callback=None, cbArgs=[]):
      #grab id from msg counter
      if self.run == False:
         return
      
      msg.payloadId = self.idCounter
      self.idCounter = self.idCounter + 1

      #serialize payload
      payload = msg.SerializeToString()
      bp = BinaryPacker()
      bp.put(UINT32, len(payload))
      bp.put(BINARY_CHUNK, payload)

      #grab read write lock
      self.rwLock.acquire(True)

      if needsReply:
         #instantiate prom/future object and set in response dict
         fut = PyPromFut()
         self.responseDict[msg.payloadId] = fut

      elif callback != None:
         #set callable in response dict
         self.responseDict[msg.payloadId] = [callback, cbArgs]


      #send over the wire
      self.clientSocket.sendall(bp.getBinaryString())

      #return future to caller
      self.rwLock.release()

      if needsReply:
         return fut

   #############################################################################
   def readBridgeSocket(self):
      while self.run is True:
         response = bytearray()

         #wait for data on the socket
         try:
            response += self.clientSocket.recv(4)
            if len(response) < 4:
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

         #grab & check size header
         packetLen = unpack('<I', response[:4])[0]
         if packetLen < 4:
            continue

         self.clientSocket.setblocking(0)
         #grab full packet
         while len(response) < packetLen + 4:
            try:
               packet = self.clientSocket.recv(packetLen)
               response += packet
               if len(response) < packetLen + 4:
                  continue
               break
            except socket.error as e:
               err = e.args[0]
               if err == errno.EAGAIN or err == errno.EWOULDBLOCK:
                  LOGDEBUG('No data available from socket.')
                  continue
               else:
                  LOGERROR("Socket error: %s" % str(e))
                  break

         #grab packet id
         packetId = unpack('<I', response[4:8])[0]
         if packetId == CPP_BDM_NOTIF_ID:
            self.pushNotification(response[8:])
            continue
         elif packetId == CPP_PROGRESS_NOTIF_ID:
            self.pushProgressNotification(response[8:])
            continue
         elif packetId == CPP_PROMPT_USER_ID:
            self.promptUser(response[8:])
            continue

         #lock and look for future object in response dict
         self.rwLock.acquire(True)
         if packetId not in self.responseDict:
            self.rwLock.release()
            continue

         #grab the future, delete it from dict
         replyObj = self.responseDict[packetId]
         del self.responseDict[packetId]

         self.clientSocket.setblocking(1)

         #fill the promise & release lock
         self.rwLock.release()

         if isinstance(replyObj, PyPromFut):
            replyObj.setVal(response[8:])

         elif replyObj != None and replyObj[0] != None:
            replyObj[0](response[8:], replyObj[1])
        
   #############################################################################
   def pushNotification(self, data):
      payload = ClientProto_pb2.CppBridgeCallback()
      payload.ParseFromString(data)

      notifThread = threading.Thread(\
         group=None, target=TheBDM.pushNotification, \
         name=None, args=[payload], kwargs={})
      notifThread.start()

   #############################################################################
   def pushProgressNotification(self, data):
      payload = ClientProto_pb2.CppProgressCallback()
      payload.ParseFromString(data)

      notifThread = threading.Thread(\
         group=None, target=TheBDM.reportProgress, \
         name=None, args=[payload], kwargs={})
      notifThread.start()

   #############################################################################
   def promptUser(self, data):
      payload = ClientProto_pb2.OpaquePayload()
      payload.ParseFromString(data)

      TheBDM.pushFromBridge(\
         payload.payloadType, payload.payload, payload.uniqueId, payload.intId)

   #############################################################################
   def returnPassphrase(self, id, passphrase):
      packet = ClientProto_pb2.ClientCommand()
      packet.method = ClientProto_pb2.returnPassphrase

      packet.stringArgs.append(id)
      packet.stringArgs.append(passphrase)

      self.sendToBridge(packet, False)

   #############################################################################
   def loadWallets(self, func):
      #create protobuf packet
      packet = ClientProto_pb2.ClientCommand()
      packet.method = ClientProto_pb2.loadWallets
        
      #send to the socket
      self.sendToBridge(packet, False, self.walletsLoaded, [func])

   #############################################################################
   def walletsLoaded(self, socketResponse, args):
      #deser protobuf reply
      response = ClientProto_pb2.WalletPayload()
      response.ParseFromString(socketResponse)

      #fire callback
      callbackThread = threading.Thread(\
         group=None, target=args[0], \
         name=None, args=[response], kwargs={})
      callbackThread.start()

   #############################################################################
   def shutdown(self):
      packet = ClientProto_pb2.ClientCommand()
      packet.method = ClientProto_pb2.shutdown
      self.sendToBridge(packet)

      self.rwLock.acquire(True)

      self.run = False
      self.clientSocket.close()
      self.listenSocket.close()

      self.rwLock.release()
      self.clientFut.result()

   #############################################################################
   def setupDB(self):
      packet = ClientProto_pb2.ClientCommand()
      packet.method = ClientProto_pb2.setupDB
        
      self.sendToBridge(packet, False)

   #############################################################################
   def registerWallets(self):
      packet = ClientProto_pb2.ClientCommand()
      packet.method = ClientProto_pb2.registerWallets
        
      self.sendToBridge(packet, False)

   #############################################################################
   def goOnline(self):
      packet = ClientProto_pb2.ClientCommand()
      packet.method = ClientProto_pb2.goOnline
        
      self.sendToBridge(packet, False)

   #############################################################################
   def getLedgerDelegateIdForWallets(self):
      packet = ClientProto_pb2.ClientCommand()
      packet.method = ClientProto_pb2.getLedgerDelegateIdForWallets

      fut = self.sendToBridge(packet)
      socketResponse = fut.getVal()

      response = ClientProto_pb2.ReplyStrings()
      response.ParseFromString(socketResponse)

      if len(response.reply) != 1:
         raise Exception("invalid reply")

      return response.reply[0]

   #############################################################################
   def updateWalletsLedgerFilter(self, ids):
      packet = ClientProto_pb2.ClientCommand()
      packet.method = ClientProto_pb2.updateWalletsLedgerFilter
        
      for id in ids:
         packet.stringArgs.append(id)

      self.sendToBridge(packet, False)

   #############################################################################
   def getHistoryPageForDelegate(self, delegateId, pageId):
      packet = ClientProto_pb2.ClientCommand()
      packet.method = ClientProto_pb2.getHistoryPageForDelegate

      packet.stringArgs.append(delegateId)
      packet.intArgs.append(pageId)

      fut = self.sendToBridge(packet)
      socketResponse = fut.getVal()

      response = ClientProto_pb2.BridgeLedgers()
      response.ParseFromString(socketResponse)

      return response

   #############################################################################
   def getNodeStatus(self):
      packet = ClientProto_pb2.ClientCommand()
      packet.method = ClientProto_pb2.getNodeStatus

      fut = self.sendToBridge(packet)
      socketResponse = fut.getVal()

      response = ClientProto_pb2.BridgeNodeStatus()
      response.ParseFromString(socketResponse)

      return response

   #############################################################################
   def getBalanceAndCount(self, wltId):
      packet = ClientProto_pb2.ClientCommand()
      packet.method = ClientProto_pb2.getBalanceAndCount
      packet.stringArgs.append(wltId)

      fut = self.sendToBridge(packet)
      socketResponse = fut.getVal()

      response = ClientProto_pb2.BridgeBalanceAndCount()
      response.ParseFromString(socketResponse)

      return response

   #############################################################################
   def getAddrCombinedList(self, wltId):
      packet = ClientProto_pb2.ClientCommand()
      packet.method = ClientProto_pb2.getAddrCombinedList
      packet.stringArgs.append(wltId)

      fut = self.sendToBridge(packet)
      socketResponse = fut.getVal()

      response = ClientProto_pb2.BridgeMultipleBalanceAndCount()
      response.ParseFromString(socketResponse)

      return response

   #############################################################################
   def getHighestUsedIndex(self, wltId):
      packet = ClientProto_pb2.ClientCommand()
      packet.method = ClientProto_pb2.getHighestUsedIndex
      packet.stringArgs.append(wltId)

      fut = self.sendToBridge(packet)
      socketResponse = fut.getVal()

      response = ClientProto_pb2.ReplyNumbers()
      response.ParseFromString(socketResponse)

      return response.ints[0]

   #############################################################################
   def getTxByHash(self, hashVal):
      packet = ClientProto_pb2.ClientCommand()
      packet.method = ClientProto_pb2.getTxByHash
      packet.byteArgs.append(hashVal)

      fut = self.sendToBridge(packet)
      socketResponse = fut.getVal()

      response = ClientProto_pb2.BridgeTx()
      response.ParseFromString(socketResponse)
      
      return response

   #############################################################################
   def getTxOutScriptType(self, script):
      packet = ClientProto_pb2.ClientCommand()
      packet.method = ClientProto_pb2.getTxOutScriptType
      packet.byteArgs.append(script)

      fut = self.sendToBridge(packet)
      socketResponse = fut.getVal()

      response = ClientProto_pb2.ReplyNumbers()
      response.ParseFromString(socketResponse)
      
      return response.ints[0]
   
   #############################################################################
   def getTxInScriptType(self, script, hashVal):
      packet = ClientProto_pb2.ClientCommand()
      packet.method = ClientProto_pb2.getTxInScriptType
      packet.byteArgs.append(script)
      packet.byteArgs.append(hashVal)

      fut = self.sendToBridge(packet)
      socketResponse = fut.getVal()

      response = ClientProto_pb2.ReplyNumbers()
      response.ParseFromString(socketResponse)
      
      return response.ints[0]

   #############################################################################
   def getLastPushDataInScript(self, script):
      packet = ClientProto_pb2.ClientCommand()
      packet.method = ClientProto_pb2.getLastPushDataInScript
      packet.byteArgs.append(script)

      fut = self.sendToBridge(packet)
      socketResponse = fut.getVal()

      response = ClientProto_pb2.ReplyBinary()
      response.ParseFromString(socketResponse)
      
      if len(response.reply) > 0:
         return response.reply[0]
      else:
         return ""

   #############################################################################
   def getTxOutScriptForScrAddr(self, script):
      packet = ClientProto_pb2.ClientCommand()
      packet.method = ClientProto_pb2.getTxOutScriptForScrAddr
      packet.byteArgs.append(script)

      fut = self.sendToBridge(packet)
      socketResponse = fut.getVal()

      response = ClientProto_pb2.ReplyBinary()
      response.ParseFromString(socketResponse)
      
      return response.reply[0]

   #############################################################################
   def getHeaderByHeight(self, height):
      packet = ClientProto_pb2.ClientCommand()
      packet.method = ClientProto_pb2.getHeaderByHeight
      packet.intArgs.append(height)

      fut = self.sendToBridge(packet)
      socketResponse = fut.getVal()

      response = ClientProto_pb2.ReplyBinary()
      response.ParseFromString(socketResponse)
      
      return response.reply[0]

   #############################################################################
   def getScrAddrForScript(self, script):
      packet = ClientProto_pb2.ClientCommand()
      packet.method = ClientProto_pb2.getScrAddrForScript
      packet.byteArgs.append(script)

      fut = self.sendToBridge(packet)
      socketResponse = fut.getVal()

      response = ClientProto_pb2.ReplyBinary()
      response.ParseFromString(socketResponse)
      
      return response.reply[0]
   
   #############################################################################
   def getAddrStrForScrAddr(self, scrAddr):
      packet = ClientProto_pb2.ClientCommand()
      packet.method = ClientProto_pb2.getAddrStrForScrAddr
      packet.byteArgs.append(scrAddr)

      fut = self.sendToBridge(packet)
      socketResponse = fut.getVal()
   
      response = ClientProto_pb2.ReplyStrings()
      if response.ParseFromString(socketResponse) == False:
         errorResponse = ClientProto_pb2.ReplyError()
         if errorResponse.ParseFromString(socketResponse) == False:
            raise Exception("unkonwn error in getAddrStrForScrAddr")
         raise Exception("error in getAddrStrForScrAddr: " + errorResponse.error())

      return response.reply[0]

   #############################################################################
   def initCoinSelectionInstance(self, wltId, height):
      packet = ClientProto_pb2.ClientCommand()
      packet.method = ClientProto_pb2.setupNewCoinSelectionInstance
      packet.stringArgs.append(wltId)
      packet.intArgs.append(height)
   
      fut = self.sendToBridge(packet)
      socketResponse = fut.getVal()

      response = ClientProto_pb2.ReplyStrings()
      response.ParseFromString(socketResponse)
      
      return response.reply[0]

   #############################################################################
   def destroyCoinSelectionInstance(self, csId):
      packet = ClientProto_pb2.ClientCommand()
      packet.method = ClientProto_pb2.destroyCoinSelectionInstance
      packet.stringArgs.append(csId)

      self.sendToBridge(packet, False)

   #############################################################################
   def setCoinSelectionRecipient(self, csId, addrStr, value, recId):
      packet = ClientProto_pb2.ClientCommand()
      packet.method = ClientProto_pb2.setCoinSelectionRecipient
      packet.stringArgs.append(csId)
      packet.stringArgs.append(addrStr)
      packet.intArgs.append(recId)
      packet.longArgs.append(value)

      fut = self.sendToBridge(packet)
      socketResponse = fut.getVal()

      response = ClientProto_pb2.ReplyNumbers()
      response.ParseFromString(socketResponse)
      
      if response.ints[0] == 0:
         raise RuntimeError("setCoinSelectionRecipient failed")

   #############################################################################
   def resetCoinSelection(self, csId):
      packet = ClientProto_pb2.ClientCommand()
      packet.method = ClientProto_pb2.resetCoinSelection
      packet.stringArgs.append(csId)

      self.sendToBridge(packet, False)
 
   #############################################################################
   def cs_SelectUTXOs(self, csId, fee, feePerByte, processFlags):
      packet = ClientProto_pb2.ClientCommand()
      packet.method = ClientProto_pb2.cs_SelectUTXOs
      packet.stringArgs.append(csId)
      packet.longArgs.append(fee)
      packet.floatArgs.append(feePerByte)
      packet.intArgs.append(processFlags)

      fut = self.sendToBridge(packet)
      socketResponse = fut.getVal()

      response = ClientProto_pb2.ReplyNumbers()
      response.ParseFromString(socketResponse)
      
      if response.ints[0] == 0:
         raise RuntimeError("selectUTXOs failed")
      
   #############################################################################
   def cs_getUtxoSelection(self, csId):
      packet = ClientProto_pb2.ClientCommand()
      packet.method = ClientProto_pb2.cs_getUtxoSelection
      packet.stringArgs.append(csId)

      fut = self.sendToBridge(packet)
      socketResponse = fut.getVal()

      response = ClientProto_pb2.BridgeUtxoList()
      response.ParseFromString(socketResponse)
      
      return response.data

   #############################################################################
   def cs_getFlatFee(self, csId):
      packet = ClientProto_pb2.ClientCommand()
      packet.method = ClientProto_pb2.cs_getFlatFee
      packet.stringArgs.append(csId)

      fut = self.sendToBridge(packet)
      socketResponse = fut.getVal()

      response = ClientProto_pb2.ReplyNumbers()
      response.ParseFromString(socketResponse)
      
      return response.longs[0]

   #############################################################################
   def cs_getFeeByte(self, csId):
      packet = ClientProto_pb2.ClientCommand()
      packet.method = ClientProto_pb2.cs_getFeeByte
      packet.stringArgs.append(csId)

      fut = self.sendToBridge(packet)
      socketResponse = fut.getVal()

      response = ClientProto_pb2.ReplyNumbers()
      response.ParseFromString(socketResponse)
      
      return response.floats[0]

   #############################################################################
   def cs_ProcessCustomUtxoList(self, csId, \
      utxoList, fee, feePerByte, processFlags):

      packet = ClientProto_pb2.ClientCommand()
      packet.method = ClientProto_pb2.cs_ProcessCustomUtxoList
      packet.stringArgs.append(csId)
      packet.longArgs.append(fee)
      packet.floatArgs.append(feePerByte)
      packet.intArgs.append(processFlags)

      for utxo in utxoList:
         bridgeUtxo = utxo.toBridgeUtxo()
         packet.byteArgs.append(bridgeUtxo.SerializeToString())

      fut = self.sendToBridge(packet)
      socketResponse = fut.getVal()

      response = ClientProto_pb2.ReplyNumbers()
      response.ParseFromString(socketResponse)
      
      response = ClientProto_pb2.ReplyNumbers()
      response.ParseFromString(socketResponse)
      
      if response.ints[0] == 0:
         raise RuntimeError("ProcessCustomUtxoList failed")

   #############################################################################
   def generateRandomHex(self, size):
      packet = ClientProto_pb2.ClientCommand()
      packet.method = ClientProto_pb2.generateRandomHex
      packet.intArgs.append(size)
   
      fut = self.sendToBridge(packet)
      socketResponse = fut.getVal()

      response = ClientProto_pb2.ReplyStrings()
      response.ParseFromString(socketResponse)
      
      return response.reply[0]

   #############################################################################
   def createAddressBook(self, wltId):
      packet = ClientProto_pb2.ClientCommand()
      packet.method = ClientProto_pb2.createAddressBook
      packet.stringArgs.append(wltId)

      fut = self.sendToBridge(packet)
      socketResponse = fut.getVal()

      response = ClientProto_pb2.BridgeAddressBook()
      response.ParseFromString(socketResponse)
      
      return response.data

   #############################################################################
   def getUtxosForValue(self, wltId, value):
      packet = ClientProto_pb2.ClientCommand()
      packet.method = ClientProto_pb2.getUtxosForValue
      packet.stringArgs.append(wltId)
      packet.longArgs.append(value)

      fut = self.sendToBridge(packet)
      socketResponse = fut.getVal()

      response = ClientProto_pb2.BridgeUtxoList()
      response.ParseFromString(socketResponse)
      
      return response.data

   #############################################################################
   def getSpendableZCList(self, wltId):
      packet = ClientProto_pb2.ClientCommand()
      packet.method = ClientProto_pb2.getSpendableZCList
      packet.stringArgs.append(wltId)

      fut = self.sendToBridge(packet)
      socketResponse = fut.getVal()

      response = ClientProto_pb2.BridgeUtxoList()
      response.ParseFromString(socketResponse)
      
      return response.data

   #############################################################################
   def getRBFTxOutList(self, wltId):
      packet = ClientProto_pb2.ClientCommand()
      packet.method = ClientProto_pb2.getRBFTxOutList
      packet.stringArgs.append(wltId)

      fut = self.sendToBridge(packet)
      socketResponse = fut.getVal()

      response = ClientProto_pb2.BridgeUtxoList()
      response.ParseFromString(socketResponse)
      
      return response.data

   #############################################################################
   def getNewAddress(self, wltId, addrType):
      packet = ClientProto_pb2.ClientCommand()
      packet.method = ClientProto_pb2.getNewAddress
      packet.stringArgs.append(wltId)
      packet.intArgs.append(addrType)

      fut = self.sendToBridge(packet)
      socketResponse = fut.getVal()

      response = ClientProto_pb2.WalletAsset()
      response.ParseFromString(socketResponse)
      
      return response

   #############################################################################
   def getNewChangeAddr(self, wltId, addrType):
      packet = ClientProto_pb2.ClientCommand()
      packet.method = ClientProto_pb2.getNewChangeAddr
      packet.stringArgs.append(wltId)
      packet.intArgs.append(addrType)

      fut = self.sendToBridge(packet)
      socketResponse = fut.getVal()

      response = ClientProto_pb2.WalletAsset()
      response.ParseFromString(socketResponse)
      
      return response

   #############################################################################
   def peekChangeAddress(self, wltId, addrType):
      packet = ClientProto_pb2.ClientCommand()
      packet.method = ClientProto_pb2.peekChangeAddress
      packet.stringArgs.append(wltId)
      packet.intArgs.append(addrType)

      fut = self.sendToBridge(packet)
      socketResponse = fut.getVal()

      response = ClientProto_pb2.WalletAsset()
      response.ParseFromString(socketResponse)
      
      return response

   #############################################################################
   def getHash160(self, data):
      packet = ClientProto_pb2.ClientCommand()
      packet.method = ClientProto_pb2.getHash160
      packet.byteArgs.append(data)

      fut = self.sendToBridge(packet)
      socketResponse = fut.getVal()

      response = ClientProto_pb2.ReplyBinary()
      response.ParseFromString(socketResponse)
      
      return response.reply[0]

   #############################################################################
   def initNewSigner(self):
      packet = ClientProto_pb2.ClientCommand()
      packet.method = ClientProto_pb2.initNewSigner

      fut = self.sendToBridge(packet)
      socketResponse = fut.getVal()

      response = ClientProto_pb2.ReplyStrings()
      response.ParseFromString(socketResponse)
      
      return response.reply[0]

   #############################################################################
   def destroySigner(self, sId):
      packet = ClientProto_pb2.ClientCommand()
      packet.method = ClientProto_pb2.destroySigner
      packet.stringArgs.append(sId)

      self.sendToBridge(packet, False)    

   #############################################################################
   def signer_SetVersion(self, sId, version):
      packet = ClientProto_pb2.ClientCommand()
      packet.method = ClientProto_pb2.signer_SetVersion
      packet.stringArgs.append(sId)
      packet.intArgs.append(version)

      fut = self.sendToBridge(packet)
      socketResponse = fut.getVal()

      response = ClientProto_pb2.ReplyNumbers()
      response.ParseFromString(socketResponse)
      
      if response.ints[0] == 0:
         raise Exception("error in signer_SetVersion")

   #############################################################################
   def signer_SetLockTime(self, sId, locktime):
      packet = ClientProto_pb2.ClientCommand()
      packet.method = ClientProto_pb2.signer_SetLockTime
      packet.stringArgs.append(sId)
      packet.intArgs.append(locktime)

      fut = self.sendToBridge(packet)
      socketResponse = fut.getVal()

      response = ClientProto_pb2.ReplyNumbers()
      response.ParseFromString(socketResponse)
      
      if response.ints[0] == 0:
         raise Exception("error in signer_SetLockTime")

   #############################################################################
   def signer_addSpenderByOutpoint(self, sId, hashVal, txoutid, seq, value):
      packet = ClientProto_pb2.ClientCommand()
      packet.method = ClientProto_pb2.signer_addSpenderByOutpoint
      packet.stringArgs.append(sId)
      packet.byteArgs.append(hashVal)
      packet.intArgs.append(txoutid)
      packet.intArgs.append(seq)
      packet.longArgs.append(value)

      fut = self.sendToBridge(packet)
      socketResponse = fut.getVal()

      response = ClientProto_pb2.ReplyNumbers()
      response.ParseFromString(socketResponse)
      
      if response.ints[0] == 0:
         raise Exception("error in signer_addSpenderByOutpoint")

   #############################################################################
   def signer_populateUtxo(self, sId, hashVal, txoutid, value, script):
      packet = ClientProto_pb2.ClientCommand()
      packet.method = ClientProto_pb2.signer_populateUtxo
      packet.stringArgs.append(sId)
      packet.byteArgs.append(hashVal)
      packet.intArgs.append(txoutid)
      packet.longArgs.append(value)
      packet.byteArgs.append(script)

      fut = self.sendToBridge(packet)
      socketResponse = fut.getVal()

      response = ClientProto_pb2.ReplyNumbers()
      response.ParseFromString(socketResponse)
      
      if response.ints[0] == 0:
         raise Exception("error in signer_addSpenderByOutpoint")

   #############################################################################
   def signer_addRecipient(self, sId, value, script):
      packet = ClientProto_pb2.ClientCommand()
      packet.method = ClientProto_pb2.signer_addRecipient
      packet.stringArgs.append(sId)
      packet.byteArgs.append(script)
      packet.longArgs.append(value)

      fut = self.sendToBridge(packet)
      socketResponse = fut.getVal()

      response = ClientProto_pb2.ReplyNumbers()
      response.ParseFromString(socketResponse)
      
      if response.ints[0] == 0:
         raise Exception("error in signer_addRecipient")

   #############################################################################
   def signer_getSerializedState(self, sId):
      packet = ClientProto_pb2.ClientCommand()
      packet.method = ClientProto_pb2.signer_getSerializedState
      packet.stringArgs.append(sId)

      fut = self.sendToBridge(packet)
      socketResponse = fut.getVal()

      response = ClientProto_pb2.ReplyBinary()
      response.ParseFromString(socketResponse)

      return response.reply[0]

   #############################################################################
   def signer_unserializeState(self, sId, state):
      packet = ClientProto_pb2.ClientCommand()
      packet.method = ClientProto_pb2.signer_unserializeState
      packet.stringArgs.append(sId)
      packet.byteArgs.append(state)

      fut = self.sendToBridge(packet)
      socketResponse = fut.getVal()

      response = ClientProto_pb2.ReplyNumbers()
      response.ParseFromString(socketResponse)

      if response.ints[0] == 0:
         raise Exception("error in signer_unserializeState")

   #############################################################################
   def signer_resolve(self, state, wltId):
      packet = ClientProto_pb2.ClientCommand()
      packet.method = ClientProto_pb2.signer_resolve
      packet.stringArgs.append(wltId)
      packet.byteArgs.append(state)

      fut = self.sendToBridge(packet)
      socketResponse = fut.getVal()

      response = ClientProto_pb2.ReplyBinary()
      response.ParseFromString(socketResponse)

      return response.reply[0]

   #############################################################################
   def signer_signTx(self, sId, wltId, callback, args):
      packet = ClientProto_pb2.ClientCommand()
      packet.method = ClientProto_pb2.signer_signTx
      packet.stringArgs.append(sId)
      packet.stringArgs.append(wltId)

      callbackArgs = [callback]
      callbackArgs.extend(args)
      self.sendToBridge(packet, False, self.signer_signTxCallback, callbackArgs)

   #############################################################################
   def signer_signTxCallback(self, socketResponse, args):
      response = ClientProto_pb2.ReplyNumbers()
      response.ParseFromString(socketResponse)

      callbackArgs = [response.ints[0]]
      callbackArgs.extend(args[1:])
      callbackThread = threading.Thread(\
         group=None, target=args[0], \
         name=None, args=callbackArgs, kwargs={})
      callbackThread.start()

   #############################################################################
   def signer_getSignedTx(self, sId):
      packet = ClientProto_pb2.ClientCommand()
      packet.method = ClientProto_pb2.signer_getSignedTx
      packet.stringArgs.append(sId)

      fut = self.sendToBridge(packet)
      socketResponse = fut.getVal()

      response = ClientProto_pb2.ReplyBinary()
      response.ParseFromString(socketResponse)

      return response.reply[0]

   #############################################################################
   def signer_getSignedStateForInput(self, sId, inputId):
      packet = ClientProto_pb2.ClientCommand()
      packet.method = ClientProto_pb2.signer_getSignedStateForInput
      packet.stringArgs.append(sId)
      packet.intArgs.append(inputId)

      fut = self.sendToBridge(packet)
      socketResponse = fut.getVal()

      response = ClientProto_pb2.BridgeInputSignedState()
      response.ParseFromString(socketResponse)

      return response     

   #############################################################################
   def broadcastTx(self, rawTxList):
      packet = ClientProto_pb2.ClientCommand()
      packet.method = ClientProto_pb2.broadcastTx
      packet.byteArgs.extend(rawTxList)

      self.sendToBridge(packet, False)

   #############################################################################
   def extendAddressPool(self, wltId, count, callback):
      packet = ClientProto_pb2.ClientCommand()
      packet.method = ClientProto_pb2.extendAddressPool
      packet.stringArgs.append(wltId)
      packet.intArgs.append(count)

      self.sendToBridge(packet, False, self.finishExtendAddressPool, [callback])

   #############################################################################
   def finishExtendAddressPool(self, socketResponse, args):
      response = ClientProto_pb2.WalletData()
      response.ParseFromString(socketResponse)

      #fire callback
      callbackThread = threading.Thread(\
         group=None, target=args[0], \
         name=None, args=[response], kwargs={})
      callbackThread.start()

   #############################################################################
   def createWallet(self, addrPoolSize, passphrase, controlPassphrase, \
      shortLabel, longLabel, extraEntropy):
      walletCreationStruct = ClientProto_pb2.BridgeCreateWalletStruct()
      walletCreationStruct.lookup = addrPoolSize
      walletCreationStruct.passphrase = passphrase
      walletCreationStruct.controlPassphrase = controlPassphrase
      walletCreationStruct.label = shortLabel
      walletCreationStruct.description = longLabel
      
      if extraEntropy is not None:
         walletCreationStruct.extraEntropy = extraEntropy

      packet = ClientProto_pb2.ClientCommand()
      packet.method = ClientProto_pb2.createWallet
      packet.byteArgs.append(walletCreationStruct.SerializeToString())

      fut = self.sendToBridge(packet)
      socketResponse = fut.getVal()

      response = ClientProto_pb2.ReplyStrings()
      response.ParseFromString(socketResponse)
      
      if len(response.reply) != 1:
         raise Exception("string reply count mismatch")

      return response.reply[0]

   #############################################################################
   def deleteWallet(self, wltId):
      packet = ClientProto_pb2.ClientCommand()
      packet.method = ClientProto_pb2.deleteWallet
      packet.stringArgs.append(wltId)

      fut = self.sendToBridge(packet)
      socketResponse = fut.getVal()

      response = ClientProto_pb2.ReplyNumbers()
      response.ParseFromString(socketResponse)

      return response.ints[0]     

   #############################################################################
   def getWalletData(self, wltId):
      packet = ClientProto_pb2.ClientCommand()
      packet.method = ClientProto_pb2.getWalletData
      packet.stringArgs.append(wltId)

      fut = self.sendToBridge(packet)
      socketResponse = fut.getVal()

      response = ClientProto_pb2.WalletData()
      response.ParseFromString(socketResponse)

      return response    

   #############################################################################
   def registerWallet(self, walletId, isNew):
      packet = ClientProto_pb2.ClientCommand()
      packet.method = ClientProto_pb2.registerWallet
      packet.stringArgs.append(walletId)
      packet.intArgs.append(isNew)

      self.sendToBridge(packet, False)
     
   #############################################################################
   def getBlockTimeByHeight(self, height):
      if height in self.blockTimeByHeightCache:
         return self.blockTimeByHeightCache[height]
      
      packet = ClientProto_pb2.ClientCommand()
      packet.method = ClientProto_pb2.getBlockTimeByHeight
      packet.intArgs.append(height)

      fut = self.sendToBridge(packet)
      socketResponse = fut.getVal()

      response = ClientProto_pb2.ReplyNumbers()
      response.ParseFromString(socketResponse)
      
      blockTime = response.ints[0]

      if blockTime == 2**32 - 1:
         raise

      self.blockTimeByHeightCache[height] = blockTime
      return blockTime

   #############################################################################
   def createBackupStringForWalletCallback(self, socketResponse, args):
      rootData = ClientProto_pb2.BridgeBackupString()
      rootData.ParseFromString(socketResponse)

      callbackArgs = [rootData]
      callbackThread = threading.Thread(\
         group=None, target=args[0], \
         name=None, args=callbackArgs, kwargs={})
      callbackThread.start()

   #############################################################################
   def createBackupStringForWallet(self, wltId, callback):
      packet = ClientProto_pb2.ClientCommand()
      packet.method = ClientProto_pb2.createBackupStringForWallet
      packet.stringArgs.append(wltId)

      callbackArgs = [callback]
      self.sendToBridge(\
         packet, False, self.createBackupStringForWalletCallback, callbackArgs)

   #############################################################################
   def restoreWallet(self, root, chaincode, sppass, callbackId):
      opaquePayload = ClientProto_pb2.RestoreWalletPayload()
      opaquePayload.root.extend(root)
      opaquePayload.secondary.extend(chaincode)
      opaquePayload.spPass = sppass

      packet = ClientProto_pb2.ClientCommand()
      packet.method = ClientProto_pb2.methodWithCallback
      packet.methodWithCallback = ClientProto_pb2.restoreWallet
      
      packet.byteArgs.append(callbackId)
      packet.byteArgs.append(opaquePayload.SerializeToString())

      self.sendToBridge(packet, False)

   #############################################################################
   def callbackFollowUp(self, payload, callbackId, callerId):
      packet = ClientProto_pb2.ClientCommand()
      packet.method = ClientProto_pb2.methodWithCallback
      packet.methodWithCallback = ClientProto_pb2.followUp

      packet.byteArgs.append(callbackId)
      packet.byteArgs.append(payload.SerializeToString())

      packet.intArgs.append(callerId)

      self.sendToBridge(packet, False)

################################################################################
TheBridge = CppBridge()