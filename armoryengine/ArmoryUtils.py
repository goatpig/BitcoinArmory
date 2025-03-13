from __future__ import (absolute_import, division,
                        print_function, unicode_literals)
##############################################################################
#                                                                            #
# Copyright (C) 2011-2015, Armory Technologies, Inc.                         #
# Distributed under the GNU Affero General Public License (AGPL v3)          #
# See LICENSE or http://www.gnu.org/licenses/agpl.html                       #
#                                                                            #
# Copyright (C) 2016-2024, goatpig                                           #
#  Distributed under the MIT license                                         #
#  See LICENSE-MIT or https://opensource.org/licenses/MIT                    #
#                                                                            #
##############################################################################

import ast
import codecs
from datetime import datetime
from email.mime.multipart import MIMEMultipart
from email.mime.base import MIMEBase
from email.mime.text import MIMEText
from email.utils import COMMASPACE, formatdate
import hashlib
import inspect
import locale
import logging
import math
import multiprocessing
import optparse
import os
import platform
import random
import signal
import smtplib
from struct import pack, unpack
import sys
import threading
import time
import traceback
import shutil
import base64
import socket
import subprocess
import binascii
import hmac, hashlib

from pathlib import Path

try:
   if os.path.exists('update_version.py') and os.path.exists('.git'):
      subprocess.check_output(["python", "update_version.py"])
except:
   pass

try:
   from armoryengine.ArmoryBuild import BTCARMORY_BUILD
except:
   BTCARMORY_BUILD = None
   
#pass sys.argv to the cpp config file parser, get the fleshed out verison 
#in return

####TODO: flesh out args with content of config file

DEFAULT = 'DEFAULT'
LEVELDB_BLKDATA = 'leveldb_blkdata'
LEVELDB_HEADERS = 'leveldb_headers'

# Version Numbers
BTCARMORY_VERSION    = (0, 96, 99, 0)  # (Major, Minor, Bugfix, AutoIncrement)
PYBTCWALLET_VERSION  = (1, 99,  0, 0)  # (Major, Minor, Bugfix, AutoIncrement)

# ARMORY_DONATION_ADDR = '1ArmoryXcfq7TnCSuZa9fQjRYwJ4bkRKfv'
# ARMORY_DONATION_PUBKEY = ( '04'
#       '11d14f8498d11c33d08b0cd7b312fb2e6fc9aebd479f8e9ab62b5333b2c395c5'
#       'f7437cab5633b5894c4a5c2132716bc36b7571cbe492a7222442b75df75b9a84')
ARMORY_INFO_SIGN_ADDR = '1NWvhByxfTXPYNT4zMBmEY3VL8QJQtQoei'
ARMORY_INFO_SIGN_PUBLICKEY = ('04'
      'af4abc4b24ef57547dd13a1110e331645f2ad2b99dfe1189abb40a5b24e4ebd8'
      'de0c1c372cc46bbee0ce3d1d49312e416a1fa9c7bb3e32a7eb3867d1c6d1f715')


indent = ' '*3
haveGUI = [False, None]

ARMORYDB_DEFAULT_IP = "127.0.0.1"
ARMORYDB_DEFAULT_PORT = "9001"

parser = optparse.OptionParser(usage="%prog [options]\n")
parser.add_option("--settings",        dest="settingsPath",default=DEFAULT, type="str",          help="load Armory with a specific settings file")
parser.add_option("--datadir",         dest="datadir",     default=DEFAULT, type="str",          help="Change the directory that Armory calls home")
parser.add_option("--satoshi-datadir", dest="satoshiHome", default=DEFAULT, type='str',          help="The Bitcoin Core/bitcoind home directory")
parser.add_option("--satoshi-port",    dest="satoshiPort", default=DEFAULT, type="str",          help="For Bitcoin Core instances operating on a non-standard port")
parser.add_option("--satoshi-rpcport", dest="satoshiRpcport",default=DEFAULT,type="str",         help="RPC port Bitcoin Core instances operating on a non-standard port")
#parser.add_option("--bitcoind-path",   dest="bitcoindPath",default='DEFAULT', type="str",         help="Path to the location of bitcoind on your system")
parser.add_option("--dbdir",           dest="armoryDBDir",  default=DEFAULT, type='str',          help="Location to store blocks database (defaults to --datadir)")
parser.add_option("--rpcport",         dest="rpcport",     default=DEFAULT, type="str",          help="RPC port for running armoryd.py")
parser.add_option("--rpcbindaddr",     dest="rpcBindAddr", default="127.0.0.1", type="str",      help="IP Address to bind to for RPC.")
parser.add_option("--testnet",         dest="testnet",     default=False,     action="store_true", help="Use the testnet protocol")
parser.add_option("--regtest",         dest="regtest",     default=False,     action="store_true", help="Use the Regression Test Network protocol")
parser.add_option("--offline",         dest="offline",     default=False,     action="store_true", help="Force Armory to run in offline mode")
parser.add_option("--nettimeout",      dest="nettimeout",  default=2,         type="int",          help="Timeout for detecting internet connection at startup")
parser.add_option("--interport",       dest="interport",   default=-1,        type="int",          help="Port for inter-process communication between Armory instances")
parser.add_option("--debug",           dest="doDebug",     default=False,     action="store_true", help="Increase amount of debugging output")
parser.add_option("--nologging",       dest="logDisable",  default=False,     action="store_true", help="Disable all logging")
parser.add_option("--netlog",          dest="netlog",      default=False,     action="store_true", help="Log networking messages sent and received by Armory")
parser.add_option("--logfile",         dest="logFile",     default=DEFAULT, type='str',          help="Specify a non-default location to send logging information")
parser.add_option("--mtdebug",         dest="mtdebug",     default=False,     action="store_true", help="Log multi-threaded call sequences")
parser.add_option("--skip-online-check",dest="forceOnline", default=False,   action="store_true", help="Go into online mode, even if internet connection isn't detected")
parser.add_option("--tor",             dest="useTorSettings", default=False, action="store_true", help="Enable common settings for when Armory connects through Tor")
parser.add_option("--bip150",          dest="bip150Used",  default=False,     action="store_true", help="Enable BIP 150 and BIP 151 support")
parser.add_option("--bip151",          dest="bip151Used",  default=False,     action="store_true", help="Enable BIP 151 support")
parser.add_option("--keypool",         dest="keypool",     default=100, type="int",                help="Default number of addresses to lookahead in Armory wallets")
parser.add_option("--redownload",      dest="redownload",  default=False,     action="store_true", help="Delete Bitcoin Core/bitcoind databases; redownload")
parser.add_option("--rebuild",         dest="rebuild",     default=False,     action="store_true", help="Rebuild blockchain database and rescan")
parser.add_option("--rescan",          dest="rescan",      default=False,     action="store_true", help="Rescan existing blockchain DB")
parser.add_option("--rescanBalance",   dest="rescanBalance", default=False,     action="store_true", help="Rescan balance")
parser.add_option("--nospendzeroconfchange",dest="ignoreAllZC",default=False, action="store_true", help="All zero-conf funds will be unspendable, including sent-to-self coins")
parser.add_option("--ignore-new-zeroconf",dest="ignoreZC",default=False, action="store_true", help="Do not update wallet balances on zero-conf transactions")
parser.add_option("--multisigfile",  dest="multisigFile",  default=DEFAULT, type='str',          help="File to store information about multi-signature transactions")
parser.add_option("--force-wallet-check", dest="forceWalletCheck", default=False, action="store_true", help="Force the wallet sanity check on startup")
parser.add_option("--disable-wallet-check", dest="disableWalletCheck", default=False, action="store_true", help="Disable the wallet sanity check on startup")
parser.add_option("--disable-modules", dest="disableModules", default=False, action="store_true", help="Disable looking for modules in the execution directory")
parser.add_option("--disable-conf-permis", dest="disableConfPermis", default=False, action="store_true", help="Disable forcing permissions on bitcoin.conf")
parser.add_option("--disable-detsign", dest="enableDetSign", action="store_false", help="Disable Transaction Deterministic Signing (RFC 6979)")
parser.add_option("--enable-detsign", dest="enableDetSign", action="store_true", help="Enable Transaction Deterministic Signing (RFC 6979) - Enabled by default")
parser.add_option("--armorydb-ip", dest="armorydb_ip", default=ARMORYDB_DEFAULT_IP, type="str", help="Set remote DB IP (default: 127.0.0.1)")
parser.add_option("--armorydb-port", dest="armorydb_port", default=ARMORYDB_DEFAULT_PORT, type="str", help="Set remote DB port (default: 9001)")
parser.add_option("--ram-usage", dest="ram_usage", default=-1, type="int", help="Set maximum ram during scans, as 128MB increments. Defaults to 4")
parser.add_option("--thread-count", dest="thread_count", default=-1, type="int", help="Set max thread count during builds and scans. Defaults to CPU total thread count")
parser.add_option("--db-type", dest="db_type", default="DB_FULL", type="str", help="Set db mode, defaults to DB_FULL")
parser.add_option("--language", dest="language", default="en", type="str", help=
   """Set the language for the client to display in. Use the ISO 639-1 language code to choose a language.
      Options are da, de, en, es, el, fr, he, hr, id, ru, sv. Default is en.
   """)

parser.set_defaults(enableDetSign=True)

# Get the host operating system
opsys = platform.system()
OS_WINDOWS = 'win32'  in opsys.lower() or 'windows' in opsys.lower()
OS_LINUX   = 'nix'    in opsys.lower() or 'nux'     in opsys.lower()
OS_MACOSX  = 'darwin' in opsys.lower() or 'osx'     in opsys.lower()

# Pre-10.9 OS X sometimes passes a process serial number as -psn_0_xxxxxx. Nuke!
if OS_MACOSX:
   parser.add_option('-p', '--psn')

# These are arguments passed by running unit-tests that need to be handled
parser.add_option("--port", dest="port", default=None, type="int", help="Unit Test Argument - Do not consume")
parser.add_option("--verbosity", dest="verbosity", default=None, type="int", help="Unit Test Argument - Do not consume")
parser.add_option("--coverage_output_dir", dest="coverageOutputDir", default=None, type="str", help="Unit Test Argument - Do not consume")
parser.add_option("--coverage_include", dest="coverageInclude", default=None, type="str", help="Unit Test Argument - Do not consume")

# Some useful constants to be used throughout everything
BASE16CHARS  = '0123456789abcdefABCDEF'
BASE16CHARS_NOCAPS = '0123456789abcdef'
LITTLEENDIAN  = '<'
BIGENDIAN     = '>'
NETWORKENDIAN = '!'
ONE_BTC       = int(100000000)
DONATION       = int(5000000)
CENT          = int(1000000)
UNINITIALIZED = None
UNKNOWN       = -2
MIN_TX_FEE    = 20000
MIN_RELAY_TX_FEE = 20000
MIN_FEE_BYTE = 200
MT_WAIT_TIMEOUT_SEC = 20
DEFAULT_FEE_TYPE = "Auto"
DEFAULT_CHANGE_TYPE = 'P2PKH'
DEFAULT_RECEIVE_TYPE = 'P2PKH'

UINT8_MAX  = 2**8-1
UINT16_MAX = 2**16-1
UINT32_MAX = 2**32-1
UINT64_MAX = 2**64-1

RightNow = time.time
SECOND   = 1
MINUTE   = 60
HOUR     = 3600
DAY      = 24*HOUR
WEEK     = 7*DAY
MONTH    = 30*DAY
YEAR     = 365*DAY

UNCOMP_PK_LEN = 65
COMP_PK_LEN   = 33

KILOBYTE = 1024.0
MEGABYTE = 1024*KILOBYTE
GIGABYTE = 1024*MEGABYTE
TERABYTE = 1024*GIGABYTE
PETABYTE = 1024*TERABYTE

LB_MAXM = 7
LB_MAXN = 7

MAX_COMMENT_LENGTH = 144

# Set the default-default
DEFAULT_DATE_FORMAT = '%Y-%b-%d %I:%M%p'
FORMAT_SYMBOLS = [ \
   ['%y', 'year, two digit (00-99)'], \
   ['%Y', 'year, four digit'], \
   ['%b', 'month name (abbrev)'], \
   ['%B', 'month name (full)'], \
   ['%m', 'month number (01-12)'], \
   ['%d', 'day of month (01-31)'], \
   ['%H', 'hour 24h (00-23)'], \
   ['%I', 'hour 12h (01-12)'], \
   ['%M', 'minute (00-59)'], \
   ['%p', 'morning/night (am,pm)'], \
   ['%a', 'day of week (abbrev)'], \
   ['%A', 'day of week (full)'], \
   ['%%', 'percent symbol'] ]


class UnserializeError(Exception): pass
class VerifyScriptError(Exception): pass
class FileExistsError(Exception): pass
class ECDSA_Error(Exception): pass
class UnitializedBlockDataError(Exception): pass
class WalletLockError(Exception): pass
class SignatureError(Exception): pass
class KeyDataError(Exception): pass
class ChecksumError(Exception): pass
class WalletAddressError(Exception): pass
class PassphraseError(Exception): pass
class EncryptionError(Exception): pass
class InterruptTestError(Exception): pass
class NetworkIDError(Exception): pass
class UnknownNetworkPayload(Exception): pass
class WalletExistsError(Exception): pass
class WalletUnregisteredError(Exception): pass
class AddressUnregisteredError(Exception): pass
class ConnectionError(Exception): pass
class BlockchainUnavailableError(Exception): pass
class InvalidHashError(Exception): pass
class InvalidScriptError(Exception): pass
class BadURIError(Exception): pass
class CompressedKeyError(Exception): pass
class TooMuchPrecisionError(Exception): pass
class NegativeValueError(Exception): pass
class FiniteFieldError(Exception): pass
class BitcoindError(Exception): pass
class ShouldNotGetHereError(Exception): pass
class BadInputError(Exception): pass
class UstxError(Exception): pass
class P2SHNotSupportedError(Exception): pass
class isMSWallet(Exception): pass
class SignerException(Exception): pass

if getattr(sys, 'frozen', False):
   sys.argv = [arg.decode('utf8') for arg in sys.argv]

CLI_OPTIONS = None
CLI_ARGS = None
(CLI_OPTIONS, CLI_ARGS) = parser.parse_args()


# This is probably an abuse of the CLI_OPTIONS structure, but not
# automatically expanding "~" symbols is killing me
for opt,val in CLI_OPTIONS.__dict__.items():
   if not isinstance(val, (str, bytes)) or not val.startswith('~'):
      continue

   if os.path.exists(os.path.expanduser(val)):
      CLI_OPTIONS.__dict__[opt] = os.path.expanduser(val)
   else:
      # If the path doesn't exist, it still won't exist when we don't
      # modify it, and I'd like to modify as few vars as possible
      pass


# Use CLI args to determine testnet or not
USE_TESTNET = CLI_OPTIONS.testnet

# Use CLI args to determine regtest or not
USE_REGTEST = CLI_OPTIONS.regtest

# Set default port for inter-process communication
if CLI_OPTIONS.interport < 0:
   CLI_OPTIONS.interport = 8223 + (1 if USE_TESTNET else 0) + (1 if USE_REGTEST else 0)


# Pass this bool to all getSpendable* methods, and it will consider
# all zero-conf UTXOs as unspendable, including sent-to-self (change)
IGNOREZC  = CLI_OPTIONS.ignoreAllZC

#db address
ARMORYDB_IP = CLI_OPTIONS.armorydb_ip

usesDefaultDbPort = True
ARMORYDB_PORT = CLI_OPTIONS.armorydb_port
if ARMORYDB_PORT != ARMORYDB_DEFAULT_PORT:
   usesDefaultDbPort = False

ARMORY_RAM_USAGE = CLI_OPTIONS.ram_usage
ARMORY_THREAD_COUNT = CLI_OPTIONS.thread_count

ARMORY_DB_TYPE = CLI_OPTIONS.db_type

# Is deterministic signing enabled?
ENABLE_DETSIGN = CLI_OPTIONS.enableDetSign

# Figure out the default directories for Satoshi client, and BicoinArmory
OS_NAME          = ''
OS_VARIANT       = ''

USER_HOME_DIR    = str(Path.home())
if OS_WINDOWS:
   USER_HOME_DIR = os.getenv("APPDATA")

BTC_HOME_DIR     = ''
ARMORY_HOME_DIR  = ''
ARMORY_DB_DIR    = ''
DEFAULT_ADDR_TYPE= ''
SUBDIR = 'testnet3' if USE_TESTNET else '' + 'regtest' if USE_REGTEST else ''

if not CLI_OPTIONS.satoshiHome==DEFAULT:
   BTC_HOME_DIR = CLI_OPTIONS.satoshiHome
   if BTC_HOME_DIR.endswith('blocks'):
      BTC_HOME_DIR, blocks_suffix = os.path.split(BTC_HOME_DIR)

if OS_WINDOWS:
   OS_NAME         = 'Windows'
   OS_VARIANT      = platform.win32_ver()

   if BTC_HOME_DIR == '':
      BTC_HOME_DIR = os.path.join(USER_HOME_DIR, 'Bitcoin')
   if SUBDIR != '':
      BTC_HOME_DIR = os.path.join(BTC_HOME_DIR, SUBDIR)

   ARMORY_HOME_DIR = os.path.join(USER_HOME_DIR, 'Armory', SUBDIR)
   BLKFILE_DIR     = os.path.join(BTC_HOME_DIR, 'blocks')
   BLKFILE_1stFILE = os.path.join(BLKFILE_DIR, 'blk00000.dat')
elif OS_LINUX:
   import distro
   OS_NAME         = 'Linux'
   OS_VARIANT      = distro.linux_distribution()

   if BTC_HOME_DIR == '':
      BTC_HOME_DIR = os.path.join(USER_HOME_DIR, '.bitcoin')
   if SUBDIR != '':
      BTC_HOME_DIR = os.path.join(BTC_HOME_DIR, SUBDIR)

   ARMORY_HOME_DIR = os.path.join(USER_HOME_DIR, '.armory', SUBDIR)
   BLKFILE_DIR     = os.path.join(BTC_HOME_DIR, 'blocks')
   BLKFILE_1stFILE = os.path.join(BLKFILE_DIR, 'blk00000.dat')
elif OS_MACOSX:
   platform.mac_ver()
   OS_NAME         = 'MacOSX'
   OS_VARIANT      = platform.mac_ver()

   if BTC_HOME_DIR == '':
      BTC_HOME_DIR = os.path.join(USER_HOME_DIR, 'Bitcoin')
   if SUBDIR != '':
      BTC_HOME_DIR = os.path.join(BTC_HOME_DIR, SUBDIR)

   ARMORY_HOME_DIR = os.path.join(USER_HOME_DIR, 'Armory', SUBDIR)
   BLKFILE_DIR     = os.path.join(BTC_HOME_DIR, 'blocks')
   BLKFILE_1stFILE = os.path.join(BLKFILE_DIR, 'blk00000.dat')
else:
   print('***Unknown operating system!')
   print('***Cannot determine default directory locations')

BLOCKCHAINS = {}
BLOCKCHAINS[b'\xf9\xbe\xb4\xd9'] = "Main Network"
BLOCKCHAINS[b'\xfa\xbf\xb5\xda'] = "Regression Test Network"
BLOCKCHAINS[b'\x0b\x11\x09\x07'] = "Test Network (testnet3)"

NETWORKS = {}
NETWORKS[b'\x00'] = "Main Network"
NETWORKS[b'\x05'] = "Main Network"
NETWORKS[b'\x6f'] = "Test Network"
NETWORKS[b'\xc4'] = "Test Network"
NETWORKS[b'\x6f'] = "Regtest Network"
NETWORKS[b'\xc4'] = "Regtest Network"
NETWORKS[b'\x34'] = "Namecoin Network"

# We disable wallet checks on ARM for the sake of resources (unless forced)
DO_WALLET_CHECK = CLI_OPTIONS.forceWalletCheck or \
                  not platform.machine().lower().startswith('arm') and \
                  not CLI_OPTIONS.disableWalletCheck

# Version Handling Code
def getVersionString(vquad, numPieces=4):
   vstr = '%d.%02d' % vquad[:2]
   if (vquad[2] > 0 or vquad[3] > 0) and numPieces>2:
      vstr += '.%d' % vquad[2]
   if vquad[3] > 0 and numPieces>3:
      vstr += '.%d' % vquad[3]
   return vstr

def getVersionInt(vquad, numPieces=4):
   vint  = int(vquad[0] * 1e7)
   vint += int(vquad[1] * 1e5)
   if numPieces>2:
      vint += int(vquad[2] * 1e3)
   if numPieces>3:
      vint += int(vquad[3])
   return vint

def readVersionString(verStr):
   verList = [int(piece) for piece in verStr.split('.')]
   while len(verList)<4:
      verList.append(0)
   return tuple(verList)

def readVersionInt(verInt):
   verStr = str(verInt).rjust(10,'0')
   verList = []
   verList.append( int(verStr[       -3:]) )
   verList.append( int(verStr[    -5:-3 ]) )
   verList.append( int(verStr[ -7:-5    ]) )
   verList.append( int(verStr[:-7       ]) )
   return tuple(verList[::-1])

# Allow user to override default Armory home directory
if not CLI_OPTIONS.datadir==DEFAULT:
   try:
      if not os.path.exists(CLI_OPTIONS.datadir):
         os.makedirs(CLI_OPTIONS.datadir)
      ARMORY_HOME_DIR = CLI_OPTIONS.datadir
   except:
      # If user has no permission to create the directory
      # pass so that the default value remains
      # This condition will be checked after the main
      # constructor completes so that a warning dialog
      # can be displayed
      pass
# Same for the directory that holds the LMDB databases
ARMORY_DB_DIR = os.path.join(ARMORY_HOME_DIR, 'databases')

if not CLI_OPTIONS.armoryDBDir==DEFAULT:
   try:
      if not os.path.exists(CLI_OPTIONS.armoryDBDir):
         os.makedirs(CLI_OPTIONS.armoryDBDir)
      ARMORY_DB_DIR = CLI_OPTIONS.armoryDBDir
   except:
      # If user has no permission to create the directory
      # pass so that the default value remains
      # This condition will be checked after the main
      # constructor completes so that a warning dialog
      # can be displayed
      pass

# Change the log file to use
ARMORY_LOG_FILE = os.path.join(ARMORY_HOME_DIR, 'armorylog.txt')
ARMCPP_LOG_FILE = os.path.join(ARMORY_HOME_DIR, 'armorycpplog.txt')
ARMDB_LOG_FILE = os.path.join(ARMORY_HOME_DIR, 'dbLog.txt')
if not sys.argv[0] in ['ArmoryQt.py', 'ArmoryQt.exe', 'Armory.exe']:
   basename = os.path.basename(sys.argv[0])
   CLI_OPTIONS.logFile = os.path.join(ARMORY_HOME_DIR, '%s.log.txt' % basename)

# Change the settings file to use
if CLI_OPTIONS.settingsPath==DEFAULT:
   CLI_OPTIONS.settingsPath = os.path.join(ARMORY_HOME_DIR, 'ArmorySettings.txt')

# Change the log file to use
if CLI_OPTIONS.logFile==DEFAULT:
   if sys.argv[0] in ['ArmoryQt.py', 'ArmoryQt.exe', 'Armory.exe']:
      CLI_OPTIONS.logFile = os.path.join(ARMORY_HOME_DIR, 'armorylog.txt')
   else:
      basename = os.path.basename(sys.argv[0])
      CLI_OPTIONS.logFile = os.path.join(ARMORY_HOME_DIR, '%s.log.txt' % basename)

SETTINGS_PATH   = CLI_OPTIONS.settingsPath
MULT_LOG_FILE   = os.path.join(ARMORY_HOME_DIR, 'multipliers.txt')
MULTISIG_FILE_NAME   = 'multisigs.txt'
MULTISIG_FILE   = os.path.join(ARMORY_HOME_DIR, MULTISIG_FILE_NAME)

if not CLI_OPTIONS.multisigFile==DEFAULT:
   if not os.path.exists(CLI_OPTIONS.multisigFile):
      print('Multisig file "%s" does not exist!' % CLI_OPTIONS.multisigFile)
   else:
      MULTISIG_FILE  = CLI_OPTIONS.multisigFile

# If this is the first Armory has been run, create directories
if ARMORY_HOME_DIR and not os.path.exists(ARMORY_HOME_DIR):
   os.makedirs(ARMORY_HOME_DIR)

if not os.path.exists(ARMORY_DB_DIR):
   os.makedirs(ARMORY_DB_DIR)

##### MAIN NETWORK IS DEFAULT #####
if not USE_TESTNET and not USE_REGTEST:
   # TODO:  The testnet genesis tx hash can't be the same...?
   BITCOIN_PORT = 8333
   BITCOIN_RPC_PORT = 8332
   ARMORY_RPC_PORT = 8225
   MAGIC_BYTES = b'\xf9\xbe\xb4\xd9'
   GENESIS_BLOCK_HASH_HEX  = '6fe28c0ab6f1b372c1a6a246ae63f74f931e8365e15a089c68d6190000000000'
   GENESIS_BLOCK_HASH      = 'o\xe2\x8c\n\xb6\xf1\xb3r\xc1\xa6\xa2F\xaec\xf7O\x93\x1e\x83e\xe1Z\x08\x9ch\xd6\x19\x00\x00\x00\x00\x00'
   GENESIS_TX_HASH_HEX     = '3ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a'
   GENESIS_TX_HASH         = ';\xa3\xed\xfdz{\x12\xb2z\xc7,>gv\x8fa\x7f\xc8\x1b\xc3\x88\x8aQ2:\x9f\xb8\xaaK\x1e^J'
   ADDRBYTE = b'\x00'
   P2SHBYTE = b'\x05'
   PRIVKEYBYTE = b'\x80'
   BECH32_PREFIX = "bc"

   # This will usually just be used in the GUI to make links for the user
   BLOCKEXPLORE_NAME     = 'blockstream.info'
   BLOCKEXPLORE_URL_TX   = 'https://blockstream.info/tx/%s'
   BLOCKEXPLORE_URL_ADDR = 'https://blockstream.info/address/%s'
else:   
   BITCOIN_PORT = 18444 if USE_REGTEST else 18333
   BITCOIN_RPC_PORT = 18443 if USE_REGTEST else 18332
   ARMORY_RPC_PORT     = 18225
   if USE_TESTNET:
      MAGIC_BYTES  = b'\x0b\x11\x09\x07'
      GENESIS_BLOCK_HASH_HEX  = '43497fd7f826957108f4a30fd9cec3aeba79972084e90ead01ea330900000000'
      GENESIS_BLOCK_HASH      = 'CI\x7f\xd7\xf8&\x95q\x08\xf4\xa3\x0f\xd9\xce\xc3\xae\xbay\x97 \x84\xe9\x0e\xad\x01\xea3\t\x00\x00\x00\x00'
      GENESIS_TX_HASH_HEX     = '3ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a'
      GENESIS_TX_HASH         = ';\xa3\xed\xfdz{\x12\xb2z\xc7,>gv\x8fa\x7f\xc8\x1b\xc3\x88\x8aQ2:\x9f\xb8\xaaK\x1e^J'
      ARMORYDB_DEFAULT_PORT = "19001"

      if usesDefaultDbPort:
         ARMORYDB_PORT = "19001"
   else:
      MAGIC_BYTES  = b'\xfa\xbf\xb5\xda'
      GENESIS_BLOCK_HASH_HEX  = '06226e46111a0b59caaf126043eb5bbf28c34f3a5e332a1fc7b2b73cf188910f'
      GENESIS_BLOCK_HASH      = '\x06\x22\x6e\x46\x11\x1a\x0b\x59\xca\xaf\x12\x60\x43\xeb\x5b\xbf\x28\xc3\x4f\x3a\x5e\x33\x2a\x1f\xc7\xb2\xb7\x3c\xf1\x88\x91\x0f'
      GENESIS_TX_HASH_HEX     = '3ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a'
      GENESIS_TX_HASH         = ';\xa3\xed\xfdz{\x12\xb2z\xc7,>gv\x8fa\x7f\xc8\x1b\xc3\x88\x8aQ2:\x9f\xb8\xaaK\x1e^J'
      ARMORYDB_DEFAULT_PORT = "19002"

      if usesDefaultDbPort:
         ARMORYDB_PORT = "19002"

   ADDRBYTE = b'\x6f'
   P2SHBYTE = b'\xc4'
   PRIVKEYBYTE = b'\xef'

   #
   BLOCKEXPLORE_NAME     = 'blockexplorer.com' if USE_TESTNET else 'Fake regtest explorer'
   BLOCKEXPLORE_URL_TX   = 'https://blockexplorer.com/testnet/tx/%s' if USE_TESTNET else 'https://noexplorer.none/%s'
   BLOCKEXPLORE_URL_ADDR = 'https://blockexplorer.com/testnet/address/%s' if USE_TESTNET else 'https://noexplorer.none/addr/%s'

# These are the same regardless of network
# They are the way data is stored in the database which is network agnostic
SCRADDR_P2PKH_BYTE    = b'\x00'
SCRADDR_P2SH_BYTE     = b'\x05'
SCRADDR_MULTISIG_BYTE = b'\xfe'
SCRADDR_NONSTD_BYTE   = b'\xff'
SCRADDR_P2WPKH_BYTE   = b'\x90'
SCRADDR_P2WSH_BYTE    = b'\x95'
SCRADDR_BYTE_LIST     = [ADDRBYTE, \
                         P2SHBYTE, \
                         SCRADDR_P2WPKH_BYTE, SCRADDR_P2WSH_BYTE, \
                         SCRADDR_MULTISIG_BYTE, \
                         SCRADDR_NONSTD_BYTE]

# Copied from cppForSwig/BtcUtils.h::getTxOutScriptTypeInt(script)
CPP_TXOUT_STDHASH160   = 0
CPP_TXOUT_STDPUBKEY65  = 1
CPP_TXOUT_STDPUBKEY33  = 2
CPP_TXOUT_MULTISIG     = 3
CPP_TXOUT_P2SH         = 4
CPP_TXOUT_NONSTANDARD  = 5
CPP_TXOUT_P2WPKH       = 6
CPP_TXOUT_P2WSH        = 7
CPP_TXOUT_OPRETURN     = 8

CPP_TXOUT_HAS_ADDRSTR  = [CPP_TXOUT_STDHASH160, \
                          CPP_TXOUT_STDPUBKEY65,
                          CPP_TXOUT_STDPUBKEY33,
                          CPP_TXOUT_P2SH,
                          CPP_TXOUT_P2WPKH,
                          CPP_TXOUT_P2WSH]
CPP_TXOUT_STDSINGLESIG = [CPP_TXOUT_STDHASH160, \
                          CPP_TXOUT_STDPUBKEY65,
                          CPP_TXOUT_STDPUBKEY33]
CPP_TXOUT_NESTED_SINGLESIG = [CPP_TXOUT_STDPUBKEY33,
                          CPP_TXOUT_P2WPKH]

CPP_TXOUT_SCRIPT_NAMES = ['']*9
CPP_TXOUT_SCRIPT_NAMES[CPP_TXOUT_STDHASH160]  = 'Standard (PKH)'
CPP_TXOUT_SCRIPT_NAMES[CPP_TXOUT_STDPUBKEY65] = 'Standard (PK65)'
CPP_TXOUT_SCRIPT_NAMES[CPP_TXOUT_STDPUBKEY33] = 'Standard (PK33)'
CPP_TXOUT_SCRIPT_NAMES[CPP_TXOUT_MULTISIG]    = 'Multi-Signature'
CPP_TXOUT_SCRIPT_NAMES[CPP_TXOUT_P2SH]        = 'Standard (P2SH)'
CPP_TXOUT_SCRIPT_NAMES[CPP_TXOUT_NONSTANDARD] = 'Non-Standard'
CPP_TXOUT_SCRIPT_NAMES[CPP_TXOUT_P2WPKH]      = 'SegWit (P2WPKH)'
CPP_TXOUT_SCRIPT_NAMES[CPP_TXOUT_P2WSH]       = 'SegWit (P2WSH)'
CPP_TXOUT_SCRIPT_NAMES[CPP_TXOUT_OPRETURN]    = 'Meta Data (OP_RETURN)'

# Copied from cppForSwig/BtcUtils.h::getTxInScriptTypeInt(script)
CPP_TXIN_STDUNCOMPR    = 0
CPP_TXIN_STDCOMPR      = 1
CPP_TXIN_COINBASE      = 2
CPP_TXIN_SPENDPUBKEY   = 3
CPP_TXIN_SPENDMULTI    = 4
CPP_TXIN_SPENDP2SH     = 5
CPP_TXIN_NONSTANDARD   = 6
CPP_TXIN_WITNESS       = 7
CPP_TXIN_P2WPKH_P2SH   = 8
CPP_TXIN_P2WSH_P2SH    = 9

CPP_TXIN_SCRIPT_NAMES = ['']*10
CPP_TXIN_SCRIPT_NAMES[CPP_TXIN_STDUNCOMPR]  = 'Sig + PubKey65'
CPP_TXIN_SCRIPT_NAMES[CPP_TXIN_STDCOMPR]    = 'Sig + PubKey33'
CPP_TXIN_SCRIPT_NAMES[CPP_TXIN_COINBASE]    = 'Coinbase'
CPP_TXIN_SCRIPT_NAMES[CPP_TXIN_SPENDPUBKEY] = 'Plain Signature'
CPP_TXIN_SCRIPT_NAMES[CPP_TXIN_SPENDMULTI]  = 'Spend Multisig'
CPP_TXIN_SCRIPT_NAMES[CPP_TXIN_SPENDP2SH]   = 'Spend P2SH'
CPP_TXIN_SCRIPT_NAMES[CPP_TXIN_NONSTANDARD] = 'Non-Standard'

################################################################################
if not CLI_OPTIONS.satoshiPort == DEFAULT:
   try:
      BITCOIN_PORT = int(CLI_OPTIONS.satoshiPort)
   except:
      raise TypeError('Invalid port for Bitcoin Core, using ' + str(BITCOIN_PORT))

################################################################################
if not CLI_OPTIONS.satoshiRpcport == DEFAULT:
   try:
      BITCOIN_RPC_PORT = int(CLI_OPTIONS.satoshiRpcport)
   except:
      raise TypeError('Invalid rpc port for Bitcoin Core, using ' + str(BITCOIN_RPC_PORT))

################################################################################
if not CLI_OPTIONS.rpcport == DEFAULT:
   try:
      ARMORY_RPC_PORT = int(CLI_OPTIONS.rpcport)
   except:
      raise TypeError('Invalid RPC port for armoryd ' + str(ARMORY_RPC_PORT))


if sys.argv[0]=='ArmoryQt.py':
   print('********************************************************************************')
   print('Loading Armory Engine:')
   print('   Armory Version:      ', getVersionString(BTCARMORY_VERSION))
   print('   Armory Build:        ', BTCARMORY_BUILD)
   print('   PyBtcWallet  Version:', getVersionString(PYBTCWALLET_VERSION))
   print('Detected Operating system:', OS_NAME)
   print('   OS Variant            :', OS_VARIANT)
   print('   User home-directory   :', USER_HOME_DIR)
   print('   Satoshi BTC directory :', BTC_HOME_DIR)
   print('   Armory home dir       :', ARMORY_HOME_DIR)
   print('   ArmoryDB directory     :', ARMORY_DB_DIR)
   print('   Armory settings file  :', SETTINGS_PATH)
   print('   Armory log file       :', ARMORY_LOG_FILE)
   print('   Do wallet checking    :', DO_WALLET_CHECK)

################################################################################
def ipAddrVersion(ipAddr):
   """
   Function that gets the IP version (4/6) for a given address. If the address
   is invalid, the return value will be 0.
   """
   # Python 3.3+ makes things a bit easier.
   if sys.info.version[0] == 2:
      try:
         socket.inet_pton(socket.AF_INET, ipAddr)
         return 4
      except socket.error:
         try:
            socket.inet_pton(socket.AF_INET6, ipAddr)
            return 6
         except:
            return 0
   else:
      try:
         return ipaddress.ip_address(str(ipAddr)).version
      except ValueError:
         return 0

################################################################################
def launchProcess(cmd, useStartInfo=True, *args, **kwargs):
   LOGINFO('Executing popen: %s', str(cmd))
   if not OS_WINDOWS:
      from subprocess import Popen, PIPE
      return Popen(cmd, stdin=PIPE, stdout=PIPE, stderr=PIPE, *args, **kwargs)
   else:
      from subprocess_win import Popen, PIPE, STARTUPINFO, STARTF_USESHOWWINDOW

      if useStartInfo:
         startinfo = STARTUPINFO()
         startinfo.dwFlags |= STARTF_USESHOWWINDOW
         return Popen(cmd, \
                     *args, \
                     stdin=PIPE, \
                     stdout=PIPE, \
                     stderr=PIPE, \
                     startupinfo=startinfo, \
                     **kwargs)
      else:
         return Popen(cmd, \
                     *args, \
                     stdin=PIPE, \
                     stdout=PIPE, \
                     stderr=PIPE, \
                     **kwargs)

#########  INITIALIZE LOGGING UTILITIES  ##########
#
# Setup logging to write INFO+ to file, and WARNING+ to console
# In debug mode, will write DEBUG+ to file and INFO+ to console
#

# Want to get the line in which an error was triggered, but by wrapping
# the logger function (as I will below), the displayed "file:linenum"
# references the logger function, not the function that called it.
# So I use traceback to find the file and line number two up in the
# stack trace, and return that to be displayed instead of default
# [Is this a hack?  Yes and no.  I see no other way to do this]
def getCallerLine():
   stkTwoUp = traceback.extract_stack()[-3]
   filename,method = stkTwoUp[0], stkTwoUp[1]
   return '%s:%d' % (os.path.basename(filename),method)

# When there's an error in the logging function, it's impossible to find!
# These wrappers will print the full stack so that it's possible to find
# which line triggered the error
def LOGDEBUG(msg, *a):
   try:
      logstr = msg if len(a)==0 else (msg%a)
      callerStr = getCallerLine() + ' - '
      logging.debug(callerStr + str(logstr))
   except TypeError:
      traceback.print_stack()
      raise

def LOGINFO(msg, *a):
   try:
      logstr = msg if len(a)==0 else (msg%a)
      callerStr = getCallerLine() + ' - '
      logging.info(callerStr + str(logstr))
   except TypeError:
      traceback.print_stack()
      raise
def LOGWARN(msg, *a):
   try:
      logstr = msg if len(a)==0 else (msg%a)
      callerStr = getCallerLine() + ' - '
      logging.warn(callerStr + str(logstr))
   except TypeError:
      traceback.print_stack()
      raise
def LOGERROR(msg, *a):
   try:
      logstr = msg if len(a)==0 else (msg%a)
      callerStr = getCallerLine() + ' - '
      logging.error(callerStr + str(logstr))
   except TypeError:
      traceback.print_stack()
      raise
def LOGCRIT(msg, *a):
   try:
      logstr = msg if len(a)==0 else (msg%a)
      callerStr = getCallerLine() + ' - '
      logging.critical(callerStr + str(logstr))
   except TypeError:
      traceback.print_stack()
      raise
def LOGEXCEPT(msg, *a):
   try:
      logstr = msg if len(a)==0 else (msg%a)
      callerStr = getCallerLine() + ' - '
      logging.exception(callerStr + str(logstr))
   except TypeError:
      traceback.print_stack()
      raise

def chopLogFile(filename, size):
   if not os.path.exists(filename):
      print('Log file doesn\'t exist [yet]')
      return

   logFile = open(filename, 'rb')
   logFile.seek(0,2) # move the cursor to the end of the file
   currentSize = logFile.tell()
   if currentSize > size:
      # this makes sure we don't get stuck reading an entire file
      # that is bigger than the available memory.
      # Also have to avoid cutting off the first line if truncating the file.
      if currentSize > int(size)+int(MEGABYTE):
         logFile.seek(-(int(size)+int(MEGABYTE)), 2)
      else:
         logFile.seek(0,0)
      logLines = logFile.readlines()
      logFile.close()

      nBytes,nLines = 0,0
      for line in logLines[::-1]:
         nBytes += len(line)
         nLines += 1
         if nBytes>size:
            break

      logFile = open(filename, 'wb')
      for line in logLines[-nLines:]:
         logFile.write(line)
      logFile.close()

# Cut down the log file to just the most recent 1 MB
chopLogFile(ARMORY_LOG_FILE, MEGABYTE)

# Now set loglevels
DEFAULT_CONSOLE_LOGTHRESH = logging.WARNING
DEFAULT_FILE_LOGTHRESH    = logging.INFO
DEFAULT_PPRINT_LOGLEVEL   = logging.DEBUG

rootLogger = logging.getLogger('')
if CLI_OPTIONS.doDebug or CLI_OPTIONS.netlog or CLI_OPTIONS.mtdebug:
   # Drop it all one level: console will see INFO, file will see DEBUG
   DEFAULT_CONSOLE_LOGTHRESH  -= 20
   DEFAULT_FILE_LOGTHRESH     -= 20

if CLI_OPTIONS.logDisable:
   DEFAULT_CONSOLE_LOGTHRESH  += 100
   DEFAULT_FILE_LOGTHRESH     += 100

DateFormat = '%Y-%m-%d %H:%M:%S'
logging.getLogger('').setLevel(logging.DEBUG)
fileFormatter  = logging.Formatter('%(asctime)s (%(levelname)s) -- %(message)s', \
                                     datefmt=DateFormat)
fileHandler = logging.FileHandler(ARMORY_LOG_FILE)
fileHandler.setLevel(DEFAULT_FILE_LOGTHRESH)
fileHandler.setFormatter(fileFormatter)
logging.getLogger('').addHandler(fileHandler)

consoleFormatter = logging.Formatter('(%(levelname)s) %(message)s')
consoleHandler = logging.StreamHandler()
consoleHandler.setLevel(DEFAULT_CONSOLE_LOGTHRESH)
consoleHandler.setFormatter( consoleFormatter )
logging.getLogger('').addHandler(consoleHandler)

class stringAggregator(object):
   def __init__(self):
      self.theStr = ''
   def getStr(self):
      return self.theStr
   def write(self, theStr):
      self.theStr += theStr

# A method to redirect pprint() calls to the log file
# Need a way to take a pprint-able object, and redirect its output to file
# Do this by swapping out sys.stdout temporarily, execute theObj.pprint()
# then set sys.stdout back to the original.
def LOGPPRINT(theObj, loglevel=DEFAULT_PPRINT_LOGLEVEL):
   sys.stdout = stringAggregator()
   theObj.pprint()
   printedStr = sys.stdout.getStr()
   sys.stdout = sys.__stdout__
   stkOneUp = traceback.extract_stack()[-2]
   filename,method = stkOneUp[0], stkOneUp[1]
   methodStr  = '(PPRINT from %s:%d)\n' % (filename,method)
   logging.log(loglevel, methodStr + printedStr)

cpplogfile = None
if CLI_OPTIONS.logDisable:
   print('Logging is disabled')
   rootLogger.disabled = True

def logexcept_override(_type, value, tback):
   try:
      strList = traceback.format_exception(_type,value,tback)
      logging.error(''.join([s for s in strList]))
   except:
      pass

   # then call the default handler
   sys.__excepthook__(_type, value, tback)

sys.excepthook = logexcept_override

# If there is a rebuild or rescan flag, let's do the right thing.
fileRedownload  = os.path.join(ARMORY_HOME_DIR, 'redownload.flag')
fileRebuild     = os.path.join(ARMORY_HOME_DIR, 'rebuild.flag')
fileRescan      = os.path.join(ARMORY_HOME_DIR, 'rescan.flag')
fileDelSettings = os.path.join(ARMORY_HOME_DIR, 'delsettings.flag')
fileClrMempool  = os.path.join(ARMORY_HOME_DIR, 'clearmempool.flag')
fileRescanBalance  = os.path.join(ARMORY_HOME_DIR, 'rescanbalance.flag')

# Flag to remove everything in Bitcoin dir except wallet.dat (if requested)
if os.path.exists(fileRedownload):
   # Flag to remove *BITCOIN-Core* databases so it will have to re-download
   LOGINFO('Found %s, will delete Bitcoin DBs & redownload' % fileRedownload)

   os.remove(fileRedownload)

   if os.path.exists(fileRebuild):
      os.remove(fileRebuild)

   if os.path.exists(fileRescan):
      os.remove(fileRescan)

   CLI_OPTIONS.redownload = True
   CLI_OPTIONS.rebuild = True

elif os.path.exists(fileRebuild):
   # Flag to remove Armory databases so it will have to rebuild
   LOGINFO('Found %s, will destroy and rebuild databases' % fileRebuild)
   os.remove(fileRebuild)

   if os.path.exists(fileRescan):
      os.remove(fileRescan)

   if os.path.exists(fileRescanBalance):
      os.remove(fileRescanBalance)

   CLI_OPTIONS.rebuild = True

elif os.path.exists(fileRescan):
   LOGINFO('Found %s, will throw out saved history, rescan' % fileRescan)
   os.remove(fileRescan)
   CLI_OPTIONS.rescan = True

   if os.path.exists(fileRescanBalance):
      os.remove(fileRescanBalance)

elif os.path.exists(fileRescanBalance):
   LOGINFO('Found %s, will rescan balances' % fileRescanBalance)
   os.remove(fileRescanBalance)
   CLI_OPTIONS.rescanBalance = True

CLI_OPTIONS.clearMempool = False
if os.path.exists(fileClrMempool):
   # Flag to clear all ZC transactions from database
   LOGINFO('Found %s, will destroy all zero conf transaction in DB' % fileClrMempool)
   os.remove(fileClrMempool)

   CLI_OPTIONS.clearMempool = True

# Separately, we may want to delete the settings file, which couldn't
# be done easily from the GUI, because it frequently gets rewritten to
# file before shutdown is complete.  The best way is to delete it on start.
if os.path.exists(fileDelSettings):
   os.remove(SETTINGS_PATH)
   os.remove(fileDelSettings)


################################################################################
def deleteBitcoindDBs():
   if not os.path.exists(BTC_HOME_DIR):
      LOGERROR('Could not find Bitcoin Core/bitcoind home dir to remove blk data')
      LOGERROR('  Does not exist: %s' % BTC_HOME_DIR)
   else:
      LOGINFO('Found bitcoin home dir, removing blocks and databases')

      # Remove directories
      for btcDir in ['blocks', 'chainstate', 'database']:
         fullPath = os.path.join(BTC_HOME_DIR, btcDir)
         if os.path.exists(fullPath):
            LOGINFO('   Removing dir:  %s' % fullPath)
            shutil.rmtree(fullPath)

      # Remove files
      for btcFile in ['DB_CONFIG', 'db.log', 'debug.log', 'peers.dat']:
         fullPath = os.path.join(BTC_HOME_DIR, btcFile)
         if os.path.exists(fullPath):
            LOGINFO('   Removing file: %s' % fullPath)
            os.remove(fullPath)

#####
if CLI_OPTIONS.redownload:
   deleteBitcoindDBs()
   if os.path.exists(fileRedownload):
      os.remove(fileRedownload)

#####
if CLI_OPTIONS.rebuild and os.path.exists(ARMORY_DB_DIR):
   LOGINFO('Found existing databases dir; removing before rebuild')
   shutil.rmtree(ARMORY_DB_DIR)
   os.mkdir(ARMORY_DB_DIR)

####
if CLI_OPTIONS.useTorSettings:
   LOGWARN('Option --tor was supplied, forcing')
   LOGWARN('--skip-online-check and --skip-stats-report')
   CLI_OPTIONS.skipStatsReport = True
   CLI_OPTIONS.forceOnline = True

################################################################################
# Get system details for logging purposes
class DumbStruct(object): pass
def GetSystemDetails():
   """Checks memory of a given system"""

   out = DumbStruct()

   CPU,COR,X64,MEM = range(4)
   sysParam = [None,None,None,None]
   out.CpuStr = 'UNKNOWN'
   out.Machine  = platform.machine().lower()
   if OS_LINUX:
      # Get total RAM
      #freeStr = subprocess_check_output('free -m', shell=True)
      #totalMemory = freeStr.split(b'\n')[1].split()[1]
      out.Memory = 0 #int(totalMemory) * 1024

      # Get CPU name
      out.CpuStr = 'Unknown'
      #cpuinfo = subprocess_check_output(['cat','/proc/cpuinfo'])
      #for line in cpuinfo.split(b'\n'):
      #   if line.strip().lower().startswith(b'model name'):
      #      out.CpuStr = str(line.split(b':')[1].strip())
      #      break

   elif OS_WINDOWS:
      import ctypes
      class MEMORYSTATUSEX(ctypes.Structure):
         _fields_ = [
            ("dwLength", ctypes.c_ulong),
            ("dwMemoryLoad", ctypes.c_ulong),
            ("ullTotalPhys", ctypes.c_ulonglong),
            ("ullAvailPhys", ctypes.c_ulonglong),
            ("ullTotalPageFile", ctypes.c_ulonglong),
            ("ullAvailPageFile", ctypes.c_ulonglong),
            ("ullTotalVirtual", ctypes.c_ulonglong),
            ("ullAvailVirtual", ctypes.c_ulonglong),
            ("sullAvailExtendedVirtual", ctypes.c_ulonglong),
         ]
         def __init__(self):
            # have to initialize this to the size of MEMORYSTATUSEX
            self.dwLength = ctypes.sizeof(self)
            super(MEMORYSTATUSEX, self).__init__()

      stat = MEMORYSTATUSEX()
      ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(stat))
      out.Memory = stat.ullTotalPhys//1024.
      out.CpuStr = platform.processor()
   elif OS_MACOSX:
      memsizeStr = subprocess_check_output('sysctl hw.memsize', shell=True)
      out.Memory = int(memsizeStr.split(b": ")[1]) // 1024
      out.CpuStr = subprocess_check_output('sysctl -n machdep.cpu.brand_string', shell=True).decode('utf-8')
   else:
      out.CpuStr = 'Unknown'
      raise OSError("Can't get system specs in: %s" % platform.system())

   out.NumCores = multiprocessing.cpu_count()
   if OS_WINDOWS:
      out.IsX64 = platform.machine().lower() == 'amd64'
   else:
      out.IsX64 = platform.machine().lower() == 'x86_64'
   out.Memory = out.Memory // (1024*1024.)

   def getHddSize(adir):
      if OS_WINDOWS:
        free_bytes = ctypes.c_ulonglong(0)
        ctypes.windll.kernel32.GetDiskFreeSpaceExW(ctypes.c_wchar_p(adir), \
                                                   None, None, \
                                                   ctypes.pointer(free_bytes))
        return free_bytes.value
      else:
         s = os.statvfs(adir)
         return s.f_bavail * s.f_frsize
   out.HddAvailA = getHddSize(ARMORY_HOME_DIR) // (1024**3)
   out.HddAvailB = getHddSize(BTC_HOME_DIR)    // (1024**3)
   return out

SystemSpecs = None
try:
   SystemSpecs = GetSystemDetails()
except:
   LOGEXCEPT('Error getting system details:')
   LOGERROR('Skipping.')
   SystemSpecs = DumbStruct()
   SystemSpecs.Memory    = -1
   SystemSpecs.CpuStr    = 'Unknown'
   SystemSpecs.NumCores  = -1
   SystemSpecs.IsX64     = 'Unknown'
   SystemSpecs.Machine   = platform.machine().lower()
   SystemSpecs.HddAvailA = -1
   SystemSpecs.HddAvailB = -1

# OSX 10.12 just had to go and make things complicated....
prefEnc = locale.getpreferredencoding()
if prefEnc == None:
   if os.environ.get('LC_ALL', '').upper() == 'UTF-8':
      prefEnc = 'utf-8'
   else:
      prefEnc = 'Unknown'

LOGINFO('')
LOGINFO('')
LOGINFO('')
LOGINFO('************************************************************')
LOGINFO('Invoked: ' + ' '.join(sys.argv))
LOGINFO('************************************************************')
LOGINFO('Loading Armory Engine:')
LOGINFO('   Armory Version        : ' + getVersionString(BTCARMORY_VERSION))
LOGINFO('   Armory Build:         : ' + str(BTCARMORY_BUILD))
LOGINFO('   PyBtcWallet  Version  : ' + getVersionString(PYBTCWALLET_VERSION))
LOGINFO('Detected Operating system: ' + OS_NAME)
LOGINFO('   OS Variant            : ' + (OS_VARIANT[0] if OS_MACOSX else '-'.join(OS_VARIANT)))
LOGINFO('   User home-directory   : ' + USER_HOME_DIR)
LOGINFO('   Satoshi BTC directory : ' + BTC_HOME_DIR)
LOGINFO('   Armory home dir       : ' + ARMORY_HOME_DIR)
LOGINFO('Detected System Specs    : ')
LOGINFO('   Total Available RAM   : %0.2f GB', SystemSpecs.Memory)
LOGINFO('   CPU ID string         : ' + SystemSpecs.CpuStr)
LOGINFO('   Number of CPU cores   : %d cores', SystemSpecs.NumCores)
LOGINFO('   System is 64-bit      : ' + str(SystemSpecs.IsX64))
LOGINFO('   Preferred Encoding    : ' + prefEnc)
LOGINFO('   Machine Arch          : ' + SystemSpecs.Machine)
LOGINFO('   Available HDD (ARM)   : %d GB' % SystemSpecs.HddAvailA)
LOGINFO('   Available HDD (BTC)   : %d GB' % SystemSpecs.HddAvailB)
LOGINFO('')
LOGINFO('Network Name: ' + NETWORKS[ADDRBYTE])
LOGINFO('Satoshi Port: %d', BITCOIN_PORT)
LOGINFO('Do wlt check: %s', str(DO_WALLET_CHECK))
LOGINFO('Named options/arguments to armoryengine.py:')
for key,val in ast.literal_eval(str(CLI_OPTIONS)).items():
   LOGINFO('    %-16s: %s', key,val)
LOGINFO('Other arguments:')
for val in CLI_ARGS:
   LOGINFO('    %s', val)
LOGINFO('************************************************************')

def GetExecDir():
   """
   Return the path from where armoryengine was imported.  Inspect method
   expects a function or module name, it can actually inspect its own
   name...
   """
   srcfile = inspect.getsourcefile(GetExecDir)
   srcpath = os.path.dirname(srcfile)
   srcpath = os.path.abspath(srcpath)
   if OS_WINDOWS and srcpath.endswith('.zip'):
      srcpath = os.path.dirname(srcpath)

   # Right now we are at the armoryengine dir... walk up one more
   srcpath = os.path.dirname(srcpath)

   LOGINFO('Determined that execution dir is: %s' % srcpath)
   if not os.path.exists(srcpath):
      LOGERROR('Exec dir %s does not exist!' % srcpath)
      LOGERROR('Continuing anyway...' % srcpath)

   return srcpath

################################################################################
def coin2str(nSatoshi, ndec=8, rJust=True, maxZeros=8):
   """
   Converts a raw value (1e-8 BTC) into a formatted string for display

   ndec, guarantees that we get get a least N decimal places in our result

   maxZeros means we will replace zeros with spaces up to M decimal places
   in order to declutter the amount field

   """

   nBtc = float(nSatoshi) / float(ONE_BTC)
   s = ('%%0.%df' % ndec) % nBtc
   s = s.rjust(18, ' ')

   if maxZeros < ndec:
      maxChop = ndec - maxZeros
      nChop = min(len(s) - len(str(s.strip('0'))), maxChop)
      if nChop>0:
         s  = s[:-nChop] + nChop*' '

   if nSatoshi < 10000*ONE_BTC:
      s.lstrip()

   if not rJust:
      s = s.strip(' ')

   s = s.replace('. ', '')

   return s

def coin2strNZ(nSatoshi):
   """ Right-justified, minimum zeros, but with padding for alignment"""
   return coin2str(nSatoshi, 8, True, 0)

def coin2strNZS(nSatoshi):
   """ Right-justified, minimum zeros, stripped """
   return coin2str(nSatoshi, 8, True, 0).strip()

def coin2str_approx(nSatoshi, sigfig=3):
   posVal = nSatoshi
   isNeg = False
   if nSatoshi<0:
      isNeg = True
      posVal *= -1

   nDig = max(round(math.log(posVal+1, 10)-0.5), 0)
   nChop = max(nDig-2, 0 )
   approxVal = round((10**nChop) * round(posVal / (10**nChop)))
   return coin2str( (-1 if isNeg else 1)*approxVal,  maxZeros=0)

def str2coin(theStr, negAllowed=True, maxDec=8, roundHighPrec=True):
   coinStr = str(theStr)
   if len(coinStr.strip())==0:
      raise ValueError

   floatVal = float(coinStr)
   satoshiVal = int(floatVal * ONE_BTC)
   if satoshiVal < 0 and negAllowed == False:
      raise NegativeValueError
   return satoshiVal

################################################################################
def makeAsciiBlock(binStr, headStr='', wid=64, newline='\n'):
   # Convert the raw chunk of binary data
   b64Data = base64.b64encode(binStr)
   sz = len(b64Data)
   firstLine = '=====%s' % headStr
   lines = [firstLine.ljust(wid, '=')]
   lines.extend([b64Data[wid*i:wid*(i+1)].decode('ascii') \
      for i in range(int((sz-1)/wid)+1)])
   lines.append("="*wid)
   return newline.join(lines)

################################################################################
def readAsciiBlock(ablock, headStr=''):
   headStr = ''
   rawData = None

   # Continue only if we actually get data.
   if len(ablock) > 0:
      lines = ablock.strip().split()
      if not lines[0].startswith('=====%s' % headStr) or \
         not lines[-1].startswith('======'):
         LOGERROR('Attempting to unserialize something not an ASCII block')
         return lines[0].strip('='), None

      headStr = lines[0].strip('=')
      rawData = base64.b64decode(''.join(lines[1:-1]))

   return (headStr, rawData)

################################################################################
def replacePlurals(txt, *args):
   """
   Use this like regular string formatting, but with pairs of strings:

      replacePlurals("I have @{one cat|%d cats}@. @{It is|They are}@ cute!", nCat)

   Then you can supply a single number which will select all supplied pairs.
    or one number per @{|}@ object.  If you use with format
   strings (such as above, with "%d") make sure to replace those strings FIRST,
   then call this function.  Otherwise the %d will disappear depending on the
   plurality and cause an error.  Hence why I made the function below:
      formatWithPlurals
   """
   if len(args)==0:
      if ('@{' in txt) and ('}@' in txt):
         raise IndexError('Not enough arguments for plural formatting')
      return txt

   argList = list(args[::-1])
   n = argList[0]
   nRepl = 0
   while '@{' in txt:
      idx0 = txt.find('@{')
      idx1 = txt.find('}@')+2
      sep = txt.find('|', idx0)
      if idx1==1 or sep==-1:
         raise TypeError('Invalid replacement format')

      strOne     = txt[idx0+2:sep]
      strMany    = txt[sep+1:idx1-2]
      strReplace = txt[idx0:idx1]

      if not len(args) == 1:
         try:
            n = argList.pop()
         except IndexError:
            raise IndexError('Not enough arguments for plural formatting')

      txt = txt.replace(strReplace, strOne if n==1 else strMany)
      nRepl += 1

   if (len(args)>1 and len(argList)>0) or (nRepl < len(args)):
      raise TypeError('Too many arguments supplied for plural formatting')

   return txt

################################################################################
def formatWithPlurals(txt, replList=None, pluralList=None):
   """
   Where you would normally supply X extra arguments for either regular string
   formatting or the plural function, you will instead supply a X-element list
   for each one (actually, the two lists are likely to be different sizes).
   """
   # Do the string formatting/replacement first, since the post-pluralized
   # string may remove some of the replacement objects (i.e. if you have
   # "The @{cat|%d cats}@ danced", the %d won't be there if the singular
   # is chosen and replaced before applying the string formatting objects).
   if replList is not None:
      if not isinstance(replList, (list,tuple)):
         replList = [replList]
      txt = txt % tuple(replList)

   if pluralList is not None:
      if not isinstance(pluralList, (list,tuple)):
         pluralList = [pluralList]
      txt = replacePlurals(txt, *pluralList)

   return txt

################################################################################
# A bunch of convenience methods for converting between:
#  -- Raw binary scripts (as seen in the blockchain)
#  -- Address strings (exchanged between people for paying each other)
#  -- ScrAddr strings (A unique identifier used by the DB)
################################################################################


################################################################################
def getAddrByte():
   return '\x6f' if USE_TESTNET or USE_REGTEST else '\x00'

################################################################################
# Convert a 20-byte hash to a "pay-to-public-key-hash" script to be inserted
# into a TxOut script
def hash160_to_p2pkhash_script(binStr20):
   if not len(binStr20)==20:
      raise InvalidHashError('Tried to convert non-20-byte str to p2pkh script')

   from armoryengine.Script import scriptPushData, getOpCode
   outScript = ''.join([  getOpCode('OP_DUP'        ), \
                          getOpCode('OP_HASH160'    ), \
                          scriptPushData(binStr20),
                          getOpCode('OP_EQUALVERIFY'), \
                          getOpCode('OP_CHECKSIG'   )])
   return outScript

################################################################################
# Convert a 20-byte hash to a "pay-to-script-hash" script to be inserted
# into a TxOut script
def hash160_to_p2sh_script(binStr20):
   if not len(binStr20)==20:
      raise InvalidHashError('Tried to convert non-20-byte str to p2sh script')

   from armoryengine.Script import scriptPushData, getOpCode
   outScript = ''.join([  getOpCode('OP_HASH160'),
                          scriptPushData(binStr20),
                          getOpCode('OP_EQUAL')])
   return outScript

################################################################################
# Convert an arbitrary script into a P2SH script
def script_to_p2sh_script(binScript):
   scriptHash = hash160(binScript)
   return hash160_to_p2sh_script(scriptHash)


################################################################################
# Convert a 33-byte or 65-byte hash to a "pay-to-pubkey" script to be inserted
# into a TxOut script
def pubkey_to_p2pk_script(binStr33or65):

   if not len(binStr33or65) in [33, 65]:
      raise KeyDataError('Invalid public key supplied to p2pk script')

   from armoryengine.Script import scriptPushData, getOpCode
   serPubKey = scriptPushData(binStr33or65)
   outScript = serPubKey + getOpCode('OP_CHECKSIG')
   return outScript

################################################################################
# Convert a list of public keys to an OP_CHECKMULTISIG script.  There will be
# use cases where we require the keys to be sorted lexicographically, so we
# will do that by default.  If you require a different order, pre-sort them
# and pass withSort=False.
#
# NOTE:  About the hardcoded bytes in here: the mainnet addrByte and P2SH
#        bytes are hardcoded into DB format.  This means that
#        that any ScrAddr object will use the mainnet prefix bytes, regardless
#        of whether it is in testnet.
def pubkeylist_to_multisig_script(pkList, M, withSort=True):

   if sum([  (0 if len(pk) in [33,65] else 1)   for pk in pkList]) > 0:
      raise KeyDataError('Not all strings in pkList are 33 or 65 bytes!')

   from armoryengine.Script import getOpCode
   opM = getOpCode('OP_%d' % M)
   opN = getOpCode('OP_%d' % len(pkList))

   newPkList = pkList[:] # copy
   if withSort:
      newPkList = sorted(pkList)

   outScript = opM
   for pk in newPkList:
      outScript += int_to_binary(len(pk), widthBytes=1)
      outScript += pk
   outScript += opN
   outScript += getOpCode('OP_CHECKMULTISIG')

   return outScript

################################################################################
# We need to have some methods for casting ASCII<->Unicode<->Preferred
DEFAULT_ENCODING = 'utf-8'

def isASCII(theStr):
   try:
      theStr.encode('ascii')
      return True
   except UnicodeEncodeError:
      return False
   except UnicodeDecodeError:
      return False
   except:
      LOGEXCEPT('What was passed to this function? %s', theStr)
      return False

def toBytes(theStr, theEncoding=DEFAULT_ENCODING):
   if isinstance(theStr, str):
      return theStr.encode(theEncoding)
   elif isinstance(theStr, bytes):
      return theStr
   else:
      try:
         return theStr.encode(theEncoding)
      except:
         raise Exception('toBytes() not been defined for input: %s', str(type(theStr)))

def toUnicode(theStr, theEncoding=DEFAULT_ENCODING):
   if isinstance(theStr, str):
      return theStr
   elif isinstance(theStr, bytes):
      return theStr.decode(theEncoding)
   else:
      try:
         return str(theStr)
      except:
         LOGEXCEPT('toUnicode() not defined for %s', str(type(theStr)))

def toPreferred(theStr):
   if OS_WINDOWS:
      return theStr.encode('utf-8')
   else:
      return toUnicode(theStr).encode(locale.getpreferredencoding())

def lenBytes(theStr, theEncoding=DEFAULT_ENCODING):
   return len(toBytes(theStr, theEncoding))

# Stolen from stackoverflow (google "stackoverflow 1809531")
def unicode_truncate(theStr, length, encoding='utf-8'):
    encoded = theStr.encode(encoding)[:length]
    return encoded.decode(encoding, 'ignore')

# This is a sweet trick for create enum-like dictionaries.
# Either automatically numbers (*args), or name-val pairs (**kwargs)
#http://stackoverflow.com/questions/36932/whats-the-best-way-to-implement-an-enum-in-python
def enum(*sequential, **named):
   enums = dict(zip(sequential, range(len(sequential))), **named)
   return type(str('Enum'), (), enums)

DATATYPE = enum("Binary", 'Base58', 'Hex')
INTERNET_STATUS = enum('Available', 'Unavailable', 'DidNotCheck')

cpplogfile = None
if CLI_OPTIONS.logDisable:
   print('Logging is disabled')
   rootLogger.disabled = True

# Some time methods (RightNow() return local unix timestamp)
RightNow = time.time
def RightNowUTC():
   return time.mktime(time.gmtime(RightNow()))

def RightNowStr(fmt=DEFAULT_DATE_FORMAT):
   return unixTimeToFormatStr(RightNow(), fmt)

# Define all the hashing functions we're going to need.  We don't actually
# use any of the first three directly (sha1, sha256, ripemd160), we only
# use hash256 and hash160 which use the first three to create the ONLY hash
# operations we ever do in the bitcoin network
# UPDATE:  mini-private-key format requires vanilla sha256...
def sha1(bits):
   return hashlib.new('sha1', bits).digest()
def sha256(bits):
   return hashlib.new('sha256', bits).digest()
def sha512(bits):
   return hashlib.new('sha512', bits).digest()
def ripemd160(bits):
   # It turns out that not all python has ripemd160...?
   #return hashlib.new('ripemd160', bits).digest()
   return Cpp.BtcUtils().ripemd160_SWIG(bits)
def hash256(s):
   """ Double-SHA256 """
   return sha256(sha256(s))
def hash160(s):
   """ RIPEMD160( SHA256( binaryStr ) ) """
   from armoryengine.CppBridge import TheBridge
   return TheBridge.utils.getHash160(s)

def HMAC(key, msg, hashfunc=hashlib.sha512, hashsz=None):
   return hmac.HMAC(key, msg, hashfunc).digest()

HMAC256 = lambda key,msg: HMAC(key, msg, hashlib.sha256, 32)
HMAC512 = lambda key,msg: HMAC(key, msg, hashlib.sha512, 64)


################################################################################
def prettyHex(theStr, indent='', withAddr=True, major=8, minor=8):
   """
   This is the same as pprintHex(), but returns the string instead of
   printing it to console.  This is useful for redirecting output to
   files, or doing further modifications to the data before display
   """
   outStr = ''
   sz = len(theStr)
   nchunk = int((sz-1)/minor) + 1
   for i in range(nchunk):
      if i%major==0:
         outStr += '\n'  + indent
         if withAddr:
            locStr = int_to_hex(i*minor/2, widthBytes=2, endOut=BIGENDIAN)
            outStr +=  '0x' + locStr + ':  '
      outStr += theStr[i*minor:(i+1)*minor] + ' '
   return outStr

################################################################################
def pprintHex(theStr, indent='', withAddr=True, major=8, minor=8):
   """
   This method takes in a long hex string and prints it out into rows
   of 64 hex chars, in chunks of 8 hex characters, and with address
   markings on each row.  This means that each row displays 32 bytes,
   which is usually pleasant.

   The format is customizable: you can adjust the indenting of the
   entire block, remove address markings, or change the major/minor
   grouping size (major * minor = hexCharsPerRow)
   """
   print(prettyHex(theStr, indent, withAddr, major, minor))

def pprintDiff(str1, str2, indent=''):
   if not len(str1)==len(str2):
      print('pprintDiff: Strings are different length!')
      return

   byteDiff = []
   for i in range(len(str1)):
      if str1[i]==str2[i]:
         byteDiff.append('-')
      else:
         byteDiff.append('X')

   pprintHex(''.join(byteDiff), indent=indent)

##### Switch endian-ness #####
def hex_switchEndian(s):
   """ Switches the endianness of a hex string (in pairs of hex chars) """
   pairList = [s[i]+s[i+1] for i in range(0,len(s),2)]
   return ''.join(pairList[::-1])
def binary_switchEndian(s):
   """ Switches the endianness of a binary string """
   return s[::-1]

##### INT/HEXSTR #####
def int_to_hex(i, widthBytes=0, endOut=LITTLEENDIAN):
   """
   Convert an integer (int() or long()) to hexadecimal.  Default behavior is
   to use the smallest even number of hex characters necessary, and using
   little-endian.   Use the widthBytes argument to add 0-padding where needed
   if you are expecting constant-length output.
   """
   h = hex(int(i))[2:]
  
   if len(h)%2 == 1:
      h = '0'+h
   if not widthBytes==0:
      nZero = 2*widthBytes - len(h)
      if nZero > 0:
         h = '0'*nZero + h
   if endOut==LITTLEENDIAN:
      h = hex_switchEndian(h)
   return h

def hex_to_int(h, endIn=LITTLEENDIAN):
   """
   Convert hex-string to integer (or long).  Default behavior is to interpret
   hex string as little-endian
   """
   hstr = h.replace(' ','')  # copies data, no references
   if endIn==LITTLEENDIAN:
      hstr = hex_switchEndian(hstr)
   return( int(hstr, 16) )

##### HEXSTR/BINARYSTR #####
def hex_to_binary(h, endIn=LITTLEENDIAN, endOut=LITTLEENDIAN):
   """
   Converts hexadecimal to binary (in a python string).  Endianness is
   only switched if (endIn != endOut)
   """
   bout = h.replace(' ','')  # copies data, no references
   if not endIn==endOut:
      bout = hex_switchEndian(bout)
   return codecs.decode(bout, 'hex_codec')

def binary_to_hex(b, endOut=LITTLEENDIAN, endIn=LITTLEENDIAN):
   """
   Converts binary to hexadecimal.  Endianness is only switched
   if (endIn != endOut)
   """
   hout = b.hex()
   if not endOut==endIn:
      hout = hex_switchEndian(hout)
   return hout

##### Shorthand combo of prettyHex and binary_to_hex intended for use in debugging
def ph(binaryInput):
   return prettyHex(binary_to_hex(binaryInput))

##### INT/BINARYSTR #####
def int_to_binary(i, widthBytes=0, endOut=LITTLEENDIAN):
   """
   Convert integer to binary.  Default behavior is use as few bytes
   as necessary, and to use little-endian.  This can be changed with
   the two optional input arguemnts.
   """
   h = int_to_hex(i,widthBytes)
   return hex_to_binary(h, endOut=endOut)

def binary_to_int(b, endIn=LITTLEENDIAN):
   """
   Converts binary to integer (or long).  Interpret as LE by default
   """
   h = binary_to_hex(b, endIn, LITTLEENDIAN)
   return hex_to_int(h)

##### INT/BITS #####

def int_to_bitset(i, widthBytes=0):
   bitsOut = []
   while i>0:
      i,r = divmod(i,2)
      bitsOut.append(['0','1'][r])
   result = ''.join(bitsOut)
   if widthBytes != 0:
      result = result.ljust(widthBytes*8,'0')
   return result

def bitset_to_int(bitset):
   n = 0
   for i,bit in enumerate(bitset):
      n += (0 if bit=='0' else 1) * 2**i
   return n

EmptyHash = hex_to_binary('00'*32)

###### Typing-friendly Base16 #####
#  Implements "hexadecimal" encoding but using only easy-to-type
#  characters in the alphabet.  Hex usually includes the digits 0-9
#  which can be slow to type, even for good typists.  On the other
#  hand, by changing the alphabet to common, easily distinguishable,
#  lowercase characters, typing such strings will become dramatically
#  faster.  Additionally, some default encodings of QRCodes do not
#  preserve the capitalization of the letters, meaning that Base58
#  is not a feasible options

NORMALCHARS  = '0123 4567 89ab cdef'.replace(' ','')
EASY16CHARS  = 'asdf ghjk wert uion'.replace(' ','')
hex_to_base16_map = {}
base16_to_hex_map = {}
for n,b in zip(NORMALCHARS,EASY16CHARS):
   hex_to_base16_map[n] = b
   base16_to_hex_map[b] = n

def binary_to_easyType16(binstr):
   return ''.join([hex_to_base16_map[c] for c in binary_to_hex(binstr)])

# Treat unrecognized characters as 0, to facilitate possibly later recovery of
# their correct values from the checksum.
def easyType16_to_binary(b16str):
   return hex_to_binary(''.join([base16_to_hex_map.get(c, '0') for c in b16str]))


def makeSixteenBytesEasy(b16):
   if not len(b16)==16:
      raise ValueError('Must supply 16-byte input')
   chk2 = computeChecksum(b16, nBytes=2)
   et18 = binary_to_easyType16(b16 + chk2)
   nineQuads = [et18[i*4:(i+1)*4] for i in range(9)]
   first4  = ' '.join(nineQuads[:4])
   second4 = ' '.join(nineQuads[4:8])
   last1   = nineQuads[8]
   return '  '.join([first4, second4, last1])

def readSixteenEasyBytes(et18):
   b18 = easyType16_to_binary(et18.strip().replace(' ',''))
   if len(b18)!=18:
      raise ValueError('Must supply 18-byte input')
   b16 = b18[:16]
   chk = b18[ 16:]
   if chk=='':
      LOGWARN('Missing checksum when reading EasyType')
      return (b16, 'No_Checksum')
   b16new = verifyChecksum(b16, chk)
   if len(b16new)==0:
      return ('','Error_2+')
   elif not b16new==b16:
      return (b16new,'Fixed_1')
   else:
      return (b16new,None)

##### FLOAT/BTC #####
# https://en.bitcoin.it/wiki/Proper_Money_Handling_(JSON-RPC)
def ubtc_to_floatStr(n):
   return '%d.%08d' % divmod (n, ONE_BTC)
def floatStr_to_ubtc(s):
   return int(round(float(s) * ONE_BTC))
def float_to_btc (f):
   return int (round(f * ONE_BTC))


# From https://en.bitcoin.it/wiki/Proper_Money_Handling_(JSON-RPC)
def JSONtoAmount(value):
   return int(round(float(value) * 1e8))
def AmountToJSON(amount):
   return float(amount / 1e8)


##### And a few useful utilities #####
# Take an incoming list and return a "zipped" list where every two items in the
# list are paired and can be iterated over together.
# http://stackoverflow.com/questions/5389507/iterating-over-every-two-elements-in-a-list
def getDualIterable(inList):
   a = iter(inList)
   return izip(a, a)


def unixTimeToFormatStr(unixTime, formatStr=DEFAULT_DATE_FORMAT):
   """
   Converts a unix time (like those found in block headers) to a
   pleasant, human-readable format
   """
   dtobj = datetime.fromtimestamp(unixTime)
   return dtobj.strftime(formatStr)

def secondsToHumanTime(nSec):
   strPieces = []
   floatSec = float(nSec)
   if floatSec < 0.9*MINUTE:
      strPieces = [floatSec, 'second']
   elif floatSec < 0.9*HOUR:
      strPieces = [floatSec/MINUTE, 'minute']
   elif floatSec < 0.9*DAY:
      strPieces = [floatSec/HOUR, 'hour']
   elif floatSec < 0.9*WEEK:
      strPieces = [floatSec/DAY, 'day']
   elif floatSec < 0.9*MONTH:
      strPieces = [floatSec/WEEK, 'week']
   elif floatSec < 0.9*YEAR:
      strPieces = [floatSec/MONTH, 'month']
   else:
      strPieces = [floatSec/YEAR, 'year']

   #
   if strPieces[0]<1.25:
      return '1 '+strPieces[1]
   elif strPieces[0]<=1.75:
      return '1.5 '+strPieces[1]+'s'
   else:
      return '%d %ss' % (int(strPieces[0]+0.5), strPieces[1])

def bytesToHumanSize(nBytes):
   if nBytes<KILOBYTE:
      return '%d bytes' % nBytes
   elif nBytes<MEGABYTE:
      return '%0.1f kB' % (nBytes/KILOBYTE)
   elif nBytes<GIGABYTE:
      return '%0.1f MB' % (nBytes/MEGABYTE)
   elif nBytes<TERABYTE:
      return '%0.1f GB' % (nBytes/GIGABYTE)
   elif nBytes<PETABYTE:
      return '%0.1f TB' % (nBytes/TERABYTE)
   else:
      return '%0.1f PB' % (nBytes/PETABYTE)


##### HEXSTR/VARINT #####
def packVarInt(n):
   """ Writes 1,3,5 or 9 bytes depending on the size of n """
   if   n < 0xfd:  return pack('B', n)
   elif n < 1<<16: return [b'\xfd'+pack('<H',n), 3]
   elif n < 1<<32: return [b'\xfe'+pack('<I',n), 5]
   else:           return [b'\xff'+pack('<Q',n), 9]

def unpackVarInt(hvi):
   """ Returns a pair: the integer value and number of bytes read """
   code = unpack('<B', hvi[0:1])[0]
   if   code  < 0xfd: return [code, 1]
   elif code == 0xfd: return [unpack('<H',hvi[1:3])[0], 3]
   elif code == 0xfe: return [unpack('<I',hvi[1:5])[0], 5]
   elif code == 0xff: return [unpack('<Q',hvi[1:9])[0], 9]
   else: assert(False)

def fixChecksumError(binaryStr, chksum, hashFunc=hash256):
   """
   Will only try to correct one byte, as that would be the most
   common error case.  Correcting two bytes is feasible, but I'm
   not going to bother implementing it until I need it.  If it's
   not a one-byte error, it's most likely a different problem
   """
   for byte in range(len(binaryStr)):
      binaryArray = [binaryStr[i] for i in range(len(binaryStr))]
      for val in range(256):
         binaryArray[byte] = chr(val)
         if hashFunc(''.join(binaryArray)).startswith(chksum):
            return ''.join(binaryArray)
   return ''

def computeChecksum(binaryStr, nBytes=4, hashFunc=hash256):
   return hashFunc(binaryStr)[:nBytes]

def verifyChecksum(binaryStr, chksum, hashFunc=hash256, fixIfNecessary=True, \
                                                              beQuiet=False):
   """
   Any time we are given a value and its checksum, we can use
   this method to verify it is valid.  If it's not valid, we
   try to correct up to a one-byte error.  Beyond that, we assume
   that the error is caused by something other than RAM/HDD error.

   The return value is:
      -- No error      :  return input
      -- One byte error:  return input with fixed byte
      -- 2+ bytes error:  return ''

   This method will check the CHECKSUM ITSELF for errors, but not correct them.
   However, for PyBtcWallet serialization, if I determine that it is a chksum
   error and simply return the original string, then PyBtcWallet will correct
   the checksum in the file, next time it reserializes the data.
   """
   bin1 = str(binaryStr)
   bin2 = binary_switchEndian(binaryStr)

   if hashFunc(bin1).startswith(chksum):
      return bin1
   elif hashFunc(bin2).startswith(chksum):
      if not beQuiet: LOGWARN( '***Checksum valid for input with reversed endianness')
      if fixIfNecessary:
         return bin2
   elif fixIfNecessary:
      if not beQuiet: LOGWARN('***Checksum error!  Attempting to fix...'),
      fixStr = fixChecksumError(bin1, chksum, hashFunc)
      if len(fixStr)>0:
         if not beQuiet: LOGWARN('fixed!')
         return fixStr
      else:
         # ONE LAST CHECK SPECIFIC TO MY SERIALIZATION SCHEME:
         # If the string was originally all zeros, chksum is hash256('')
         # ...which is a known value, and frequently used in my files
         if chksum==hex_to_binary('5df6e0e2'):
            if not beQuiet: LOGWARN('fixed!')
            return ''

   # ID a checksum byte error...
   origHash = hashFunc(bin1)
   for i in range(len(chksum)):
      chkArray = [chksum[j] for j in range(len(chksum))]
      for ch in range(256):
         chkArray[i] = chr(ch)
         if origHash.startswith(''.join(chkArray)):
            LOGWARN('***Checksum error!  Incorrect byte in checksum!')
            return bin1

   LOGWARN('Checksum fix failed')
   return ''

# Taken directly from rpc.cpp in reference bitcoin client, 0.3.24
def binaryBits_to_difficulty(b):
   """ Converts the 4-byte binary difficulty string to a float """
   i = binary_to_int(b)
   nShift = (i >> 24) & 0xff
   dDiff = float(0x0000ffff) / float(i & 0x00ffffff)
   while nShift < 29:
      dDiff *= 256.0
      nShift += 1
   while nShift > 29:
      dDiff /= 256.0
      nShift -= 1
   return dDiff

# TODO:  I don't actually know how to do this, yet...
def difficulty_to_binaryBits(i):
   pass


# The following params are for the Bitcoin elliptic curves (secp256k1)
SECP256K1_MOD   = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEFFFFFC2F
SECP256K1_ORDER = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141
SECP256K1_B     = 0x0000000000000000000000000000000000000000000000000000000000000007
SECP256K1_A     = 0x0000000000000000000000000000000000000000000000000000000000000000
SECP256K1_GX    = 0x79BE667EF9DCBBAC55A06295CE870B07029BFCDB2DCE28D959F2815B16F81798
SECP256K1_GY    = 0x483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8

################################################################################
################################################################################
# START FINITE FIELD OPERATIONS

class FiniteField(object):
   """
   Create a simple, prime-order FiniteField.  Because this is used only
   to encode data of fixed width, I enforce prime-order by hardcoding
   primes, and you just pick the data width (in bytes).  If your desired
   data width is not here,  simply find a prime number very close to 2^N,
   and add it to the PRIMES map below.

   This will be used for Shamir's Secret Sharing scheme.  Encode your
   data as the coeffient of finite-field polynomial, and store points
   on that polynomial.  The order of the polynomial determines how
   many points are needed to recover the original secret.
   """

   # bytes: primeclosetomaxval
   PRIMES = {   1:  2**8-5,  # mainly for testing
                2:  2**16-39,
                4:  2**32-5,
                8:  2**64-59,
               16:  2**128-797,
               20:  2**160-543,
               24:  2**192-333,
               32:  2**256-357,
               48:  2**384-317,
               64:  2**512-569,
               96:  2**768-825,
              128:  2**1024-105,
              192:  2**1536-3453,
              256:  2**2048-1157  }

   def __init__(self, nbytes):
      if nbytes not in self.PRIMES:
         LOGERROR('No primes available for size=%d bytes', nbytes)
         self.prime = None
         raise FiniteFieldError
      self.prime = self.PRIMES[nbytes]


   def add(self,a,b):
      return (a+b) % self.prime

   def subtract(self,a,b):
      return (a-b) % self.prime

   def mult(self,a,b):
      return (a*b) % self.prime

   def power(self,a,b):
      result = 1
      while(b>0):
         b,x = divmod(b,2)
         result = (result * (a if x else 1)) % self.prime
         a = a*a % self.prime
      return result

   def powinv(self,a):
      """ USE ONLY PRIME MODULUS """
      return self.power(a,self.prime-2)

   def divide(self,a,b):
      """ USE ONLY PRIME MODULUS """
      baddinv = self.powinv(b)
      return self.mult(a,baddinv)

   def mtrxrmrowcol(self,mtrx,r,c):
      if not len(mtrx) == len(mtrx[0]):
         LOGERROR('Must be a square matrix!')
         return []
      sz = len(mtrx)
      return [[mtrx[i][j] for j in range(sz) if not j==c] for i in range(sz) if not i==r]


   ################################################################################
   def mtrxdet(self,mtrx):
      if len(mtrx)==1:
         return mtrx[0][0]

      if not len(mtrx) == len(mtrx[0]):
         LOGERROR('Must be a square matrix!')
         return -1

      result = 0;
      for i in range(len(mtrx)):
         mult     = mtrx[0][i] * (-1 if i%2==1 else 1)
         subdet   = self.mtrxdet(self.mtrxrmrowcol(mtrx,0,i))
         result   = self.add(result, self.mult(mult,subdet))
      return result

   ################################################################################
   def mtrxmultvect(self,mtrx, vect):
      M,N = len(mtrx), len(mtrx[0])
      if not len(mtrx[0])==len(vect):
         LOGERROR('Mtrx and vect are incompatible: %dx%d, %dx1', M, N, len(vect))
      return [ sum([self.mult(mtrx[i][j],vect[j]) for j in range(N)])%self.prime for i in range(M) ]

   ################################################################################
   def mtrxmult(self,m1, m2):
      M1,N1 = len(m1), len(m1[0])
      M2,N2 = len(m2), len(m2[0])
      if not N1==M2:
         LOGERROR('Mtrx and vect are incompatible: %dx%d, %dx%d', M1,N1, M2,N2)
      inner = lambda i,j: sum([self.mult(m1[i][k],m2[k][j]) for k in range(N1)])
      return [ [inner(i,j)%self.prime for j in range(N1)] for i in range(M1) ]

   ################################################################################
   def mtrxadjoint(self,mtrx):
      sz = len(mtrx)
      inner = lambda i,j: self.mtrxdet(self.mtrxrmrowcol(mtrx,i,j))
      return [[((-1 if (i+j)%2==1 else 1)*inner(j,i))%self.prime for j in range(sz)] for i in range(sz)]

   ################################################################################
   def mtrxinv(self,mtrx):
      det = self.mtrxdet(mtrx)
      adj = self.mtrxadjoint(mtrx)
      sz = len(mtrx)
      return [[self.divide(adj[i][j],det) for j in range(sz)] for i in range(sz)]

################################################################################
def SplitSecret(secret, needed, pieces, nbytes=None):
   if not isinstance(secret, basestring):
      secret = secret.toBinStr()

   if nbytes==None:
      nbytes = len(secret)

   ff = FiniteField(nbytes)
   fragments = []

   # Convert secret to an integer
   a = binary_to_int(SecureBinaryData(secret).toBinStr(),BIGENDIAN)
   if not a<ff.prime:
      LOGERROR('Secret must be less than %s', int_to_hex(ff.prime,endOut=BIGENDIAN))
      LOGERROR('             You entered %s', int_to_hex(a,endOut=BIGENDIAN))
      raise FiniteFieldError

   if not pieces>=needed:
      LOGERROR('You must create more pieces than needed to reconstruct!')
      raise FiniteFieldError

   if needed==1 or needed>8:
      LOGERROR('Can split secrets into parts *requiring* at most 8 fragments')
      LOGERROR('You can break it into as many optional fragments as you want')
      raise FiniteFieldError


   # We use randomized coefficients so as to respect SSS security parameters
   othernum = []
   for i in range(pieces+needed-1):
      othernum.append(binary_to_int(SecureBinaryData().GenerateRandom(nbytes).toBinStr()))

   def poly(x):
      polyout = ff.mult(a, ff.power(x,needed-1))
      for i,e in enumerate(range(needed-2,-1,-1)):
         term = ff.mult(othernum[i], ff.power(x,e))
         polyout = ff.add(polyout, term)
      return polyout

   for i in range(pieces):
      x = i+1
      fragments.append( [x, poly(x)] )

   secret,a = None,None
   fragments = [ [int_to_binary(p, nbytes, BIGENDIAN) for p in frag] for frag in fragments]
   return fragments

################################################################################
def ReconstructSecret(fragments, needed, nbytes):

   ff = FiniteField(nbytes)
   pairs = fragments[:needed]
   m = []
   v = []
   for x,y in pairs:
      x = binary_to_int(x, BIGENDIAN)
      y = binary_to_int(y, BIGENDIAN)
      m.append([])
      for i,e in enumerate(range(needed-1,-1,-1)):
         m[-1].append( ff.power(x,e) )
      v.append(y)

   minv = ff.mtrxinv(m)
   outvect = ff.mtrxmultvect(minv,v)
   return int_to_binary(outvect[0], nbytes, BIGENDIAN)

################################################################################
def createTestingSubsets( fragIndices, M, maxTestCount=20):
   """
   Returns (IsRandomized, listOfTuplesOfSizeM)
   """
   numIdx = len(fragIndices)

   if M>numIdx:
      LOGERROR('Insufficent number of fragments')
      raise KeyDataError
   elif M==numIdx:
      LOGINFO('Fragments supplied == needed.  One subset to test (%s-of-N)' % M)
      return ( False, [tuple(fragIndices)] )
   else:
      LOGINFO('Test reconstruct %s-of-N, with %s fragments' % (M, numIdx))
      subs = []

      # Compute the number of possible subsets.  This is stable because we
      # shouldn't ever have more than 12 fragments
      fact = math.factorial
      numCombo = fact(numIdx) / ( fact(M) * fact(numIdx-M) )

      if numCombo <= maxTestCount:
         LOGINFO('Testing all %s combinations...' % numCombo)
         for x in range(2**numIdx):
            bits = int_to_bitset(x)
            if not bits.count('1') == M:
               continue

            subs.append(tuple([fragIndices[i] for i,b in enumerate(bits) if b=='1']))

         return (False, sorted(subs))
      else:
         LOGINFO('#Subsets > %s, will need to randomize' % maxTestCount)
         usedSubsets = set()
         while len(subs) < maxTestCount:
            sample = tuple(sorted(random.sample(fragIndices, M)))
            if not sample in usedSubsets:
               usedSubsets.add(sample)
               subs.append(sample)

         return (True, sorted(subs))

################################################################################
def testReconstructSecrets(fragMap, M, maxTestCount=20):
   # If fragMap has X elements, then it will test all X-choose-M subsets of
   # the fragMap and return the restored secret for each one.  If there's more
   # subsets than maxTestCount, then just do a random sampling of the possible
   # subsets
   fragKeys = [k for k in fragMap.iterkeys()]
   isRandom, subs = createTestingSubsets(fragKeys, M, maxTestCount)
   nBytes = len(fragMap[fragKeys[0]][1])
   LOGINFO('Testing %d-byte fragments' % nBytes)

   testResults = []
   for subset in subs:
      fragSubset = [fragMap[i][:] for i in subset]

      recon = ReconstructSecret(fragSubset, M, nBytes)
      testResults.append((subset, recon))

   return isRandom, testResults

# END FINITE FIELD OPERATIONS
################################################################################
################################################################################


################################################################################
def stripJSONStrChars(inStr):
   '''Function that strips the extra characters from a JSON-encoded string.'''
   # When Python decodes a JSON string, extra characters are added. For example,
   # "Hello" becomes "u'Hello'". We want to get the original string.
   return inStr[2:-1]

################################################################################
def checkAddrType(addrBin):
   """ Gets the network byte of the address.  Returns -1 if chksum fails """
   first21, chk4 = addrBin[:-4], addrBin[-4:]
   chkBytes = hash256(first21)
   return addrBin[0] if (chkBytes[:4] == chk4) else -1

################################################################################
def checkAddrBinValid(addrBin, validPrefixes=None):
   """
   Checks whether this address is valid for the given network
   (set at the top of pybtcengine.py)
   """
   if validPrefixes is None:
      validPrefixes = [ADDRBYTE, P2SHBYTE]

   if not isinstance(validPrefixes, list):
      validPrefixes = [validPrefixes]
      
   prefix = checkAddrType(addrBin)
   return (prefix in validPrefixes)

################################################################################
def checkAddrStrValid(addrStr, validPrefixes=[ADDRBYTE, P2SHBYTE]):
   """ Check that a Base58 address-string is valid on this network """
   if(addrStr == ''):
      return False
   return checkAddrBinValid(base58_to_binary(addrStr), validPrefixes)

################################################################################
def convertKeyDataToAddress(privKey=None, pubKey=None):
   """ Returns a hash160 value """
   if not privKey and not pubKey:
      raise BadAddressError('No key data supplied for conversion')
   elif privKey:
      if isinstance(privKey, str):
         privKey = SecureBinaryData(privKey)

      if not privKey.getSize()==32:
         raise BadAddressError('Invalid private key format!')
      else:
         pubKey = CryptoECDSA().ComputePublicKey(privKey)

   if isinstance(pubKey,str):
      pubKey = SecureBinaryData(pubKey)
   return pubKey.getHash160()

################################################################################
def decodeMiniPrivateKey(keyStr):
   """
   Converts a 22, 26 or 30-character Base58 mini private key into a
   32-byte binary private key.
   """
   if not len(keyStr) in (22,26,30):
      return ''

   keyQ = keyStr + '?'
   theHash = sha256(keyQ)

   if binary_to_hex(theHash[0]) == '01':
      raise KeyDataError('PBKDF2-based mini private keys not supported!')
   elif binary_to_hex(theHash[0]) != '00':
      raise KeyDataError('Invalid mini private key... double check the entry')

   return sha256(keyStr)

################################################################################
def parsePrivateKeyData(theStr):
      hexChars = '01234567890abcdef'
      b58Chars = '123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz'

      hexCount = sum([1 if c in hexChars else 0 for c in theStr.lower()])
      b58Count = sum([1 if c in b58Chars else 0 for c in theStr])
      canBeHex = hexCount==len(theStr)
      canBeB58 = b58Count==len(theStr)

      binEntry = ''
      keyType = ''
      isMini = False
      if canBeB58 and not canBeHex:
         if len(theStr) in (22, 30):
            # Mini-private key format!
            try:
               binEntry = decodeMiniPrivateKey(theStr)
            except KeyDataError:
               raise BadAddressError('Invalid mini-private key string')
            keyType = 'Mini Private Key Format'
            isMini = True
         elif len(theStr) in range(48,53):
            binEntry = base58_to_binary(theStr)
            keyType = 'Plain Base58'
         else:
            raise BadAddressError('Unrecognized key data')
      elif canBeHex:
         binEntry = hex_to_binary(theStr)
         keyType = 'Plain Hex'
      else:
         raise BadAddressError('Unrecognized key data')


      if len(binEntry)==36 or (len(binEntry)==37 and binEntry[0]==PRIVKEYBYTE):
         if len(binEntry)==36:
            keydata = binEntry[:32 ]
            chk     = binEntry[ 32:]
            binEntry = verifyChecksum(keydata, chk)
            if not isMini:
               keyType = 'Raw %s with checksum' % keyType.split(' ')[1]
         else:
            # Assume leading 0x80 byte, and 4 byte checksum
            keydata = binEntry[ :1+32 ]
            chk     = binEntry[  1+32:]
            binEntry = verifyChecksum(keydata, chk)
            binEntry = binEntry[1:]
            if not isMini:
               keyType = 'Standard %s key with checksum' % keyType.split(' ')[1]

         if binEntry=='':
            raise InvalidHashError('Private Key checksum failed!')
      elif len(binEntry) in (33, 37) and binEntry[-1]=='\x01':
         raise CompressedKeyError('Compressed Public keys not supported!')
      return binEntry, keyType

URI_VERSION_STR = '1.0'

################################################################################
# Take in a "bitcoin:" URI string and parse the data out into a dictionary. If
# the URI isn't a Bitcoin URI, return an empty dictionary.
def parseBitcoinURI(uriStr):
   """ Takes a URI string, returns normalized dicitonary with pieces """
   data = {}

   # Split URI into parts. Let Python do the heavy lifting.
   from urlparse import urlparse, parse_qs
   uri = urlparse(uriStr)
   query = parse_qs(uri.query)

   # If query entry has only 1 entry, flatten and remove entry from array.
   for k in query:
      v = query[k]
      if len(v) == 1:
         query[k] = v[0]

   # Now start walking through the parts and get the info out of it.
   if uri.scheme == 'bitcoin':
      data['address'] = uri.path

      # Apply filters to known keys. Do NOT filter based on the "req-"
      # prefix from BIP21. Leave that to code using the dict.
      for k in query:
         v = query[k]
         kl = k.lower()
         if kl == 'amount':
            data['amount'] = str2coin(v) # Convert to Satoshis
         else:
            data[k] = v

   return data

################################################################################
def uriReservedToPercent(theStr):
   """
   Convert from a regular string to a percent-encoded string
   """
   #Must replace '%' first, to avoid recursive (and incorrect) replacement!
   reserved = "%!*'();:@&=+$,/?#[]\" "

   for c in reserved:
      theStr = theStr.replace(c, '%%%s' % int_to_hex(ord(c)))
   return theStr

################################################################################
def uriPercentToReserved(theStr):
   """
   This replacement direction is much easier!
   Convert from a percent-encoded string to a
   """

   parts = theStr.split('%')
   if len(parts)>1:
      for p in parts[1:]:
         parts[0] += chr( hex_to_int(p[:2]) ) + p[2:]
   return parts[0][:]

################################################################################
def createBitcoinURI(addr, amt=None, msg=None):
   uriStr = 'bitcoin:%s' % addr
   if amt or msg:
      uriStr += '?'

   if amt:
      uriStr += 'amount=%s' % coin2str(amt, maxZeros=0).strip()

   if amt and msg:
      uriStr += '&'

   if msg:
      uriStr += 'label=%s' % uriReservedToPercent(msg)

   return uriStr

################################################################################
def createDERSigFromRS(rBin, sBin):
   # Remove all leading zero-bytes
   rBin = rBin.lstrip('\x00')
   sBin = sBin.lstrip('\x00')

   # assure that s is the low value so that tx is not malleable (BIP62)
   sInt = binary_to_int(sBin, BIGENDIAN)
   if sInt >  SECP256K1_ORDER / 2:
      sInt = SECP256K1_ORDER - sInt
      # Compliance with BIP62 requires no extra padding
      sBin = int_to_binary(sInt, endOut=BIGENDIAN)

   if binary_to_int(rBin[0])&128>0:  rBin = '\x00'+rBin
   if binary_to_int(sBin[0])&128>0:  sBin = '\x00'+sBin
   rSize  = int_to_binary(len(rBin))
   sSize  = int_to_binary(len(sBin))
   rsSize = int_to_binary(len(rBin) + len(sBin) + 4)
   sigScript = '\x30' + rsSize + \
               '\x02' + rSize + rBin + \
               '\x02' + sSize + sBin
   return sigScript

################################################################################
def getRSFromDERSig(derSig):
   if not isinstance(derSig, str):
      # In case this is a SecureBinaryData object...
      derSig = derSig.toBinStr()

   codeByte = derSig[0]
   nBytes   = binary_to_int(derSig[1])
   rsStr    = derSig[2:2+nBytes]
   assert(codeByte == '\x30')
   assert(nBytes == len(rsStr))
   # Read r
   codeByte  = rsStr[0]
   rBytes    = binary_to_int(rsStr[1])
   r         = rsStr[2:2+rBytes]
   assert(codeByte == '\x02')
   sStr      = rsStr[2+rBytes:]
   # Read s
   codeByte  = sStr[0]
   sBytes    = binary_to_int(sStr[1])
   s         = sStr[2:2+sBytes]
   assert(codeByte == '\x02')
   # Now we have the (r,s) values of the

   rFinal = r.lstrip('\x00').rjust(32, '\x00')
   sFinal = s.lstrip('\x00').rjust(32, '\x00')
   return rFinal, sFinal

#############################################################################
def notifyOnSurpriseTx(blk0, blk1, wltMap, lboxWltMap, isGui, bdm, notifyQueue, settings ):
   # We usually see transactions as zero-conf first, then they show up in
   # a block. It is a "surprise" when the first time we see it is in a block
   if isGui:
      notifiedAlready = set([ n[1].getTxHash() for n in notifyQueue ])
      notifyIn  = settings.getSettingOrSetDefault('NotifyBtcIn', True)
      notifyOut = settings.getSettingOrSetDefault('NotifyBtcOut', True)

   for blk in range(blk0, blk1):
      sbh = bdm.getMainBlockFromDB(blk)
      for i in range(sbh.getNumTx()):
         cppTx = sbh.getTxCopy(i)
         # Iterate through the Python wallets and create a ledger entry for
         # the transaction. If we haven't already been notified of the
         # transaction, put it on the notification queue.
         for wltID,wlt in wltMap.iteritems():
            le = wlt.cppWallet.calcLedgerEntryForTx(cppTx)
            if isGui and (notifyQueue != None):
               if not le.getTxHash() in notifiedAlready:
                  if (le.getValue()<=0 and notifyOut) or (le.getValue>0 and notifyIn):
                     notifyQueue.append([wltID, le, False])
               else:
                  pass
            else:
               # There should be a log message here.
               pass
         # Iterate through the C++ lockbox wallets and create a ledger entry
         # for the transaction.If we haven't already been notified of the
         # transaction, put it on the notification queue.
         for lbID,cppWlt in lboxWltMap.iteritems():
            le = cppWlt.calcLedgerEntryForTx(cppTx)
            if isGui and (notifyQueue != None):
               if not le.getTxHash() in notifiedAlready:
                  if (le.getValue()<=0 and notifyOut) or \
                     (le.getValue>0 and notifyIn):
                     notifyQueue.append([lbID, le, False])
               else:
                  pass
            else:
               # There should be a log message here.
               pass

################################################################################
class PyBackgroundThread(threading.Thread):
   """
   Wraps a function in a threading.Thread object which will run
   that function in a separate thread.  Calling self.start() will
   return immediately, but will start running that function in
   separate thread.  You can check its progress later by using
   self.isRunning() or self.isFinished().  If the function returns
   a value, use self.getOutput().  Use self.getElapsedSeconds()
   to find out how long it took.
   """

   def __init__(self, *args, **kwargs):
      threading.Thread.__init__(self)

      self.output     = None
      self.startedAt  = UNINITIALIZED
      self.finishedAt = UNINITIALIZED
      self.errorThrown = None
      self.passAsync = None
      self.setDaemon(True)

      if len(args)==0:
         self.func  = lambda: ()
      else:
         if not hasattr(args[0], '__call__'):
            raise TypeError('PyBkgdThread ctor arg1 must be a function')
         else:
            self.setThreadFunction(args[0], *args[1:], **kwargs)

   def setThreadFunction(self, thefunc, *args, **kwargs):
      def funcPartial():
         return thefunc(*args, **kwargs)
      self.func = funcPartial

   def setDaemon(self, yesno):
      if self.isStarted():
         LOGERROR('Must set daemon property before starting thread')
      else:
         super(PyBackgroundThread, self).setDaemon(yesno)

   def isFinished(self):
      return not (self.finishedAt==UNINITIALIZED)

   def isStarted(self):
      return not (self.startedAt==UNINITIALIZED)

   def isRunning(self):
      return (self.isStarted() and not self.isFinished())

   def getElapsedSeconds(self):
      if not self.isFinished():
         LOGERROR('Thread is not finished yet!')
         return None
      else:
         return self.finishedAt - self.startedAt

   def getOutput(self):
      if not self.isFinished():
         if self.isRunning():
            LOGERROR('Cannot get output while thread is running')
         else:
            LOGERROR('Thread was never .start()ed')
         return None

      return self.output

   def didThrowError(self):
      return (self.errorThrown is not None)

   def raiseLastError(self):
      if self.errorThrown is None:
         return
      raise self.errorThrown

   def getErrorType(self):
      if self.errorThrown is None:
         return None
      return type(self.errorThrown)

   def getErrorMsg(self):
      if self.errorThrown is None:
         return ''
      return self.errorThrown.args[0]


   def start(self):
      # The prefunc is blocking.  Probably preparing something
      # that needs to be in place before we start the thread
      self.startedAt = RightNow()
      super(PyBackgroundThread, self).start()

   def run(self):
      # This should not be called manually.  Only call start()
      try:
         self.output = self.func()
      except Exception as e:
         LOGEXCEPT('Error in pybkgdthread: %s', str(e))
         self.errorThrown = e
      self.finishedAt = RightNow()

      if not self.passAsync: return
      if hasattr(self.passAsync, '__call__'):
         self.passAsync()

   def reset(self):
      self.output = None
      self.startedAt  = UNINITIALIZED
      self.finishedAt = UNINITIALIZED
      self.errorThrown = None

   def restart(self):
      self.reset()
      self.start()

# Define a decorator that allows the function to be called asynchronously
def AllowAsync(func):
   def wrappedFunc(*args, **kwargs):
      if not 'async_' in kwargs or kwargs['async_']==False:
         # Run the function normally
         if 'async_' in kwargs:
            del kwargs['async_']
         return func(*args, **kwargs)
      else:
         # Run the function as a background thread
         passAsync = kwargs['async_']
         del kwargs['async_']

         thr = PyBackgroundThread(func, *args, **kwargs)
         thr.passAsync = passAsync
         thr.start()
         return thr

   return wrappedFunc

def emptyFunc(*args, **kwargs):
   return

################################################################################
# Function checks to see if a binary value that's passed in is a valid public
# key. The incoming key may be binary or hex. The return value is a boolean
# indicating whether or not the key is valid.
def isValidPK(inPK, inStr=False):
   retVal = False
   checkVal = '\x00'

   if inStr:
      checkVal = hex_to_binary(inPK)
   else:
      checkVal = inPK
   pkLen = len(checkVal)

   if pkLen == UNCOMP_PK_LEN or pkLen == COMP_PK_LEN:
      # The "proper" way to check the key is to feed it to Crypto++.
      if not CryptoECDSA().VerifyPublicKeyValid(SecureBinaryData(checkVal)):
         LOGWARN('Pub key %s is invalid.' % binary_to_hex(inPK))
      else:
         retVal = True
   else:
      LOGWARN('Pub key %s has an invalid length (%d bytes).' % \
              (len(inPK), binary_to_hex(inPK)))

   return retVal

################################################################################
# Function that extracts IDs from a given text block and returns a list of all
# the IDs. The format should follow the example below, with "12345678" and
# "AbCdEfGh" being the IDs, and "LOCKBOX" being the key. There may be extra
# newline characters. The characters will be ignored.
#
# =====LOCKBOX-12345678=====================================================
# ckhc3hqhhuih7gGGOUT78hweds
# ==========================================================================
# =====LOCKBOX-AbCdEfGh=====================================================
# ckhc3hqhhuih7gGGOUT78hweds
# ==========================================================================
#
# In addition, the incoming block of text must be from a file (using something
# like "with open() as x") or a StringIO/cStringIO object.
def getBlockID(asciiText, key):
   blockList = []

   # Iterate over each line in the text and get the IDs.
   for line in asciiText:
      if key in line:
         stripT = line.replace("=", "").replace(key, "").replace("\n", "")
         blockList.append(stripT)

   return blockList

################################################################################
# Decompress an incoming public key. The incoming key may be binary or hex. The
# final key is in the same form (i.e., binary or hex) as the incoming key.
def decompressPK(inKey, inStr=False):
   outKey = '\x00'
   checkKey = '\x00'

   # Let's support strings and binary data.
   if inStr:
      checkKey = hex_to_binary(inKey)
   else:
      checkKey = inKey
   lenInKey = len(checkKey)

   # WARNING: This function won't verify the input. It just passes the input
   # down the line.
   if lenInKey == UNCOMP_PK_LEN:
       LOGWARN('The public key is already decompressed.')
       outKey = checkKey
   elif lenInKey != COMP_PK_LEN:
       LOGERROR('The public key has an incorrect size (%d bytes).' % lenInKey)
   else:
      if checkKey[0] == '\x02' or checkKey[0] == '\x03':
         cppKeyVal = SecureBinaryData(checkKey)
         outKey = CryptoECDSA().UncompressPoint(cppKeyVal).toBinStr()
         keyStr = binary_to_hex(outKey)
      else:
         LOGERROR('The public key\'s first byte (%s) is incorrectly ' \
                  'formatted.' % binary_to_hex(checkKey[0]))

   # Keep consistency by returning a string key if necessary.
   if inStr:
      outKey = binary_to_hex(outKey)
   return outKey

################################################################################
class BlockComponent(object):

   def copy(self):
      return self.__class__().unserialize(self.serialize())

   def serialize(self):
      raise NotImplementedError

   def unserialize(self):
      raise NotImplementedError

################################################################################
# Function that can be used to send an e-mail to multiple recipients.
def send_email(send_from, server, password, send_to, subject, text):
   # smtp.sendmail() requires a list of recipients. If we didn't get a list,
   # create one, and delimit based on a colon.
   if not type(send_to) == list:
      send_to = send_to.split(":")

   # Split the server info. Also, use a default port in case the user goofed and
   # didn't specify a port.
   server = server.split(":")
   serverAddr = server[0]
   serverPort = 587
   if len(server) > 1:
      serverPort = int(server[1])

   # Some of this may have to be modded to support non-TLS servers.
   msg = MIMEMultipart()
   msg['From'] = send_from
   msg['To'] = COMMASPACE.join(send_to)
   msg['Date'] = formatdate(localtime=True)
   msg['Subject'] = subject
   msg.attach(MIMEText(text))
   mailServer = smtplib.SMTP(serverAddr, serverPort)
   mailServer.ehlo()
   mailServer.starttls()
   mailServer.ehlo()
   mailServer.login(send_from, password)
   mailServer.sendmail(send_from, send_to, msg.as_string())
   mailServer.close()

#############################################################################
def getLastBytesOfFile(filename, nBytes=500*1024):
   if not os.path.exists(filename):
      LOGERROR('File does not exist!')
      return ''

   sz = os.path.getsize(filename)
   with open(filename, 'rb') as fin:
      if sz > nBytes:
         fin.seek(sz - nBytes)
      return fin.read()

# Random method for creating
def touchFile(fname):
   try:
      os.utime(fname, None)
   except:
      f = open(fname, 'a')
      f.flush()
      os.fsync(f.fileno())
      f.close()

################################################################################
def getNameForAddrType(addrType):
   from armoryengine.CppBridge import TheBridge
   return TheBridge.utils.getNameForAddrType(addrType)

################################################################################
def getRandomHexits_NotSecure(count):
   return ''.join([random.choice(BASE16CHARS_NOCAPS) for x in range(count)])

#################
# bridge setup
#################

def getBridgeArgList():
   #gather cli args for bridge
   bridgeArgs = []

   #testnet
   if USE_TESTNET:
      bridgeArgs.append(["testnet", None])

   #db type
   bridgeArgs.append(["db-type", CLI_OPTIONS.db_type])

   #db ip & port
   bridgeArgs.append(["armorydb-ip", ARMORYDB_IP])
   bridgeArgs.append(["armorydb-port", ARMORYDB_PORT])

   #datadir
   bridgeArgs.append(["datadir", '"' + ARMORY_HOME_DIR + '"'])

   #enforce --public for now
   bridgeArgs.append(["public", None])

   #offline
   if CLI_OPTIONS.offline:
      bridgeArgs.append(["offline", None])

   args = []
   for argKey, argVal in bridgeArgs:
      if argVal:
         args.append("--" + argKey + "=" + argVal)
      else:
         args.append("--" + argKey)
   return args
