################################################################################
#                                                                              #
# Copyright (C) 2020-2021, goatpig.                                            #
#  Distributed under the MIT license                                           #
#  See LICENSE-MIT or https://opensource.org/licenses/MIT                      #
#                                                                              #
################################################################################

BIP15X_UNINITIALIZED = "uninitialized"
BIP15X_ERROR         = "error"
BIP15X_ENCRYPTED     = "encrypted"
BIP15X_READY         = "ready"

#CHACHA20POLY1305MAXBYTESSENT = 1000000000
CHACHA20POLY1305MAXBYTESSENT = 1200
CHACHA20POLY1305MAXPACKETSIZE = 1024 * 1024 * 1024 #1MB

import sys
from .c20p1305 import lib, ffi

AEAD_THRESHOLD_BEGIN      = 100,
AEAD_START                = 101,
AEAD_PRESENTPUBKEY        = 102,
AEAD_PRESENTPUBKEYCHILD   = 103, #unused

AEAD_THRESHOLD_ENC        = 110,
AEAD_ENCINIT              = 111,
AEAD_ENCACK               = 112,
AEAD_REKEY                = 113,

AEAD_THRESHOLD_AUTH       = 130,
AEAD_CHALLENGE            = 131,
AEAD_REPLY                = 132,
AEAD_PROPOSE              = 133,

AEAD_THRESHOLD_END        = 150

################################################################################
class AEAD_Error(Exception):
   pass

################################################################################
class BIP15xChannel(object):
   def __init__(self, incoming):
      self.incoming = incoming

      #generate a private key for shared secret setup
      self.channel = lib.bip151_channel_makenew()
      self.bytesOnKey = 0

   #############################################################################
   def __del__(self):
      lib.freeBuffer(self.channel)
      self.channel = None

   #############################################################################
   def getEncInit(self):
      encinit = lib.bip151_channel_getencinit(self.channel)
      pyencinit = ffi.buffer(encinit, 34)
      pyencinit = bytes(AEAD_ENCINIT) + pyencinit

      lib.freeBuffer(encinit)
      return pyencinit

   #############################################################################
   def getEncAck(self):
      encack = lib.bip151_channel_getencack(self.channel)
      pyencack = ffi.buffer(encack, 33)
      pyencack = bytes(AEAD_ENCACK) + pyencack

      lib.freeBuffer(encack)
      return pyencack

   #############################################################################
   def processEncInit(self, counterparty):
      if lib.bip151_channel_processencinit(\
         self.channel, counterparty, len(counterparty)) == False:
         raise AEAD_Error("failed to generate shared secret")

   #############################################################################
   def processEncAck(self, counterparty):
      if lib.bip151_channel_processencack(\
         self.channel, counterparty, len(counterparty)) == False:
         raise AEAD_Error("failed to generate shared secret")

################################################################################
class BIP15xConnection(object):
   def __init__(self, sendToBridgeLbd):
      self.state = BIP15X_UNINITIALIZED
      self.step = 0
      self.macLen = lib.bip15x_init_lib()

      self.inSession = BIP15xChannel(True)
      self.outSession = BIP15xChannel(False)

      self.sendToBridgeLbd = sendToBridgeLbd
      self.notifyReadyLbd = None

      self.privkey = lib.get_new_privkey()
      self.pubkey = lib.compute_pubkey(self.privkey)

   #############################################################################
   def getPubkeyHex(self):
      pubkeyPy = ffi.buffer(self.pubkey, 33)
      return bytes(pubkeyPy).hex()

   #############################################################################
   def __del__(self):
      lib.freeBuffer(self.privkey)
      lib.freeBuffer(self.pubkey)

      self.privkey = None
      self.pubkey = None

      self.inSession = None
      self.outSession = None

   #############################################################################
   def setNotifyReadyLbd(self, notifyReadyLbd):
      self.notifyReadyLbd = notifyReadyLbd

   #############################################################################
   def getRandomBytes(self, count):
      val = lib.generate_random(count)
      valPy = bytes(ffi.buffer(val, count))
      return valPy

   #############################################################################
   def getMacLen(self):
      return self.macLen

   #############################################################################
   def encrypted(self):
      return self.state == BIP15X_ENCRYPTED or self.state == BIP15X_READY

   #############################################################################
   def ready(self):
      return self.state == BIP15X_READY

   #############################################################################
   def serverStartHandshake(self):
      #load client cookie
      with open("./client_cookie", "rb") as cookieFile:
         fileData = cookieFile.read()
         self.clientPubkey = bytes(fileData[0:33])

      #get EncInit packet
      self.step = AEAD_START
      encInitPacket = self.outSession.getEncInit()

      #send to client
      self.sendToBridgeLbd(encInitPacket)

   #############################################################################
   def getAEADPacketSize(self, header):
      if header == AEAD_ENCACK[0]:
         return 33

      elif header == AEAD_ENCINIT[0]:
         return 34

      elif header == AEAD_CHALLENGE[0]:
         return 32

      elif header == AEAD_PROPOSE[0]:
         return 32

      elif header == AEAD_REPLY[0]:
         return 64

      elif header == AEAD_REKEY[0]:
         return 33

      raise AEAD_Error("unknown AEAD header byte")

   #############################################################################
   def serverHandshake(self, header, payload):
      if header == AEAD_ENCACK[0]:
         #sanity check & status update
         if self.step != AEAD_START:
            raise AEAD_Error("invalid state")
         self.step = AEAD_ENCACK

         #generate outgoing channel shared secret
         self.outSession.processEncAck(payload)

      elif header == AEAD_ENCINIT[0]:
         #sanity check & status update
         if self.step != AEAD_ENCACK:
            raise AEAD_Error("invalid state")
         self.step = AEAD_ENCINIT

         #generate incoming channel secret
         self.inSession.processEncInit(payload)

         #grab the public key
         encAck = self.inSession.getEncAck()

         #channel is now encrypted
         self.state = BIP15X_ENCRYPTED

         #send pubkey to client
         self.sendToBridgeLbd(encAck)

      elif header == AEAD_CHALLENGE[0]:
         #sanity check & status update
         if self.step != AEAD_ENCINIT or self.state != BIP15X_ENCRYPTED:
            raise AEAD_Error("invalid state")
         self.step = AEAD_CHALLENGE

         #check challenge
         if not lib.bip150_check_authchallenge(\
            payload, len(payload), \
            self.inSession.channel, self.pubkey):
            raise AEAD_Error("invalid challenge")

         #get auth reply
         authReply = lib.bip150_get_authreply(\
            self.outSession.channel, self.privkey)

         #sanity check
         if lib.isNull(authReply):
             raise AEAD_Error("auth reply failure")

         #append header
         authReplyPy = ffi.buffer(authReply, 64)
         authReplyPy = bytes(AEAD_REPLY) + bytes(authReplyPy)

         #cleanup C buffer
         lib.freeBuffer(authReply)

         #encrypt & send to client
         encrPayload = self.encrypt(authReplyPy, 65)
         self.sendToBridgeLbd(encrPayload)

      elif header == AEAD_PROPOSE[0]:
         if self.step != AEAD_CHALLENGE or self.state != BIP15X_ENCRYPTED:
            raise AEAD_Error("invalid state")
         self.step = AEAD_PROPOSE

         #check propose
         if not lib.bip150_check_authpropose(\
            payload, len(payload), \
            self.inSession.channel, self.clientPubkey):
            raise AEAD_Error("invalid propose")

         #return auth challenge
         authChallenge = lib.bip150_get_authchallenge(\
            self.outSession.channel, self.clientPubkey)

         #sanity check
         if lib.isNull(authChallenge):
             raise AEAD_Error("auth reply failure")

         #append header
         authChallengePy = ffi.buffer(authChallenge, 32)
         authChallengePy = bytes(AEAD_CHALLENGE) + bytes(authChallengePy)

         #cleanup C buffer
         lib.freeBuffer(authChallenge)

         #encrypt & send to client
         encrPayload = self.encrypt(authChallengePy, 33)
         self.sendToBridgeLbd(encrPayload)

      elif header == AEAD_REPLY[0]:
         if self.step != AEAD_PROPOSE or self.state != BIP15X_ENCRYPTED:
            raise AEAD_Error("invalid state")
         self.step = AEAD_REPLY

         #check reply
         if not lib.bip150_check_authreply(\
            payload, len(payload), \
            self.inSession.channel, self.clientPubkey):
            raise AEAD_Error("invalid auth reply")

         #rekey
         lib.bip151_channel_initial_rekey(\
            self.inSession.channel, self.outSession.channel, \
            self.pubkey, self.clientPubkey)
         self.outSession.bytesOnKey = 0

         #mark connection as ready
         self.state = BIP15X_READY
         self.notifyReadyLbd()

      elif header == AEAD_REKEY[0]:
         if self.state != BIP15X_READY:
            raise AEAD_Error("invalid rekey request")

         if not lib.bip151_isrekeymsg(payload, len(payload)):
            raise AEAD_Error("invalid rekey message")

         lib.bip151_channel_rekey(self.inSession.channel)

      else:
         raise AEAD_Error("invalid handshake header")

   #############################################################################
   def decodeSize(self, payload):
      return lib.bip15x_get_length(self.inSession.channel, payload)

   #############################################################################
   def decrypt(self, payload, payloadSize):
      clearText = ffi.new("uint8_t[" + str(payloadSize + 4) + "]")

      decryptionResult = lib.bip15x_decrypt(\
         self.inSession.channel, payload, payloadSize, clearText)
      if decryptionResult == False:
         raise AEAD_Error("failed to decrypt payload: " + str(decryptionResult))

      return bytes(clearText)[4:]

   #############################################################################
   def encrypt(self, payload, payloadSize):
      packetSize = payloadSize + 4 + self.macLen
      cipherText = ffi.new("uint8_t[" + str(packetSize) + "]")

      if lib.bip15x_encrypt(self.outSession.channel, \
         payload, payloadSize, cipherText) == False:
         raise AEAD_Error("failed to encrypt packet")

      self.outSession.bytesOnKey += payloadSize + 4
      return bytes(cipherText)

   #############################################################################
   def needsRekey(self, packetSize):
      return self.outSession.bytesOnKey + packetSize + 4 + self.macLen >= \
         CHACHA20POLY1305MAXBYTESSENT

   #############################################################################
   def getRekeyPayload(self):
      rekeyPayload = bytes(AEAD_REKEY) + bytearray(33)
      encryptedPayload = self.encrypt(rekeyPayload, 34)

      #rekey the channel
      lib.bip151_channel_rekey(self.outSession.channel)
      self.outSession.bytesOnKey = 0

      return encryptedPayload
