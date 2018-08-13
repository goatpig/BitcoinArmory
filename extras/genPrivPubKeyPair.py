################################################################################
#                                                                              #
# Copyright (C) 2018 goatpig                                                   #
#                                                                              #
################################################################################
from __future__ import print_function
import sys
import os
import ipaddress
sys.path.append(os.path.join(os.path.dirname(__file__), '..'))
from CppBlockUtils import SecureBinaryData, CryptoECDSA

# A simple program that generates a secp256k1 private key and accompanying
# compressed public key, and outputs the hex strings to the terminal.
print('Generate private and compressed public keys (secp256k1 curve)')
privKey = SecureBinaryData().GenerateRandom(32)
print('Private key: ', privKey.toHexStr())
pubKey = CryptoECDSA().ComputePublicKey(privKey)
print('Public key:  ', CryptoECDSA().CompressPoint(pubKey).toHexStr())

# Use the lines below if you want to generate the key from a pre-determined key
# privKeyStr = "000102030405060708090a0b0c0d0e0f000102030405060708090a0b0c0d0e0f"
# privKey = SecureBinaryData(hexstr.decode("hex"))
