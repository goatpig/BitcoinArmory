import hashlib
import binascii
from struct import pack

def sha1(bits):
   return hashlib.new('sha1', bits).digest()
def sha256(bits):
   h = hashlib.new('sha256')
   h.update(bits)
   return h.digest()
def sha512(bits):
   return hashlib.new('sha512', bits).digest()
def hash256(s):
   """ Double-SHA256 """
   return sha256(sha256(s))

def HMAC(key, msg, hashfunc=sha512, hashsz=None):
   """ This is intended to be simple, not fast.  For speed, use HDWalletCrypto() """
   hashsz = len(hashfunc(b'')) if hashsz==None else hashsz
   key = (hashfunc(key) if len(key)>hashsz else key)
   key = key.ljust(hashsz, b'\x00')
   okey = b''.join([pack('B', ord(b'\x5c')^(ord(c) if isinstance(c, str) else c)) for c in key])
   print ("okey: " + str(binascii.hexlify(okey)))
   ikey = b''.join([pack('B', ord(b'\x36')^(ord(c) if isinstance(c, str) else c)) for c in key])
   print ("ikey: " + str(binascii.hexlify(ikey)))
   ihash = hashfunc(ikey + msg)
   print ("ihash: " + str(binascii.hexlify(ihash)))
   return hashfunc( okey + ihash )


HMAC256 = lambda key,msg: HMAC(key, msg, sha256, 32)
HMAC512 = lambda key,msg: HMAC(key, msg, sha512, 64)

hash_abcd = sha256(b'abcd')
hash_efgh = sha256(b'efgh')
print (binascii.hexlify(hash_abcd))
print (binascii.hexlify(hash_efgh))

hmac_abcd_efgh = HMAC256(b'abcd', b'efgh')
print (binascii.hexlify(hmac_abcd_efgh))