#!/usr/bin/env python
# Copyright (c) 2016 Jonas Schnelli
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

import sys, os
from subprocess import call

valgrind = True;
commands = []
commands.append(["-v", 0])
commands.append(["-foobar", 1])
commands.append(["-c genkey", 0])
commands.append(["-c genkey --testnet", 0])
commands.append(["-c genkey --regtest", 0])
commands.append(["", 1])
commands.append(["-c hdprintkey", 1])
commands.append(["-c hdprintkey -p xpub6MR9tbm8V5pGFTQ9hTATxd4kPgdKKqU75ED8s3rddrSknLHgZy1H4Wh596jgoYNH7WNcKEVM1wfKD2pTSdj5Hm7CMJwwyRjHYPQCT2LJXwm", 0])
commands.append(["-c hdprintkey -p tprv8ZgxMBicQKsPegfnEE6sgR64tuPn72fX965MeazaJC72Sfi5JfqLrCnQmA9vTJTCxfDpiq2jWBSLc8L2Uy497ij5iT4KDvXYZRWxCNWPugm", 1])
commands.append(["-c hdprintkey -p tprv8ZgxMBicQKsPegfnEE6sgR64tuPn72fX965MeazaJC72Sfi5JfqLrCnQmA9vTJTCxfDpiq2jWBSLc8L2Uy497ij5iT4KDvXYZRWxCNWPugm --testnet", 0])
commands.append(["-c pubfrompriv -p L15mEfW7s13utgsTrziK52z6HC1jEZbp3R9ma7qPfwCphhtJFmjp", 0]) #successfull WIF to pub
commands.append(["-c pubfrompriv", 1]) #missing required argument
commands.append(["-c pubfrompriv -p L15mEfW7s13utgsTrziK52z6HC1jEZbp3R9", 1]) #invalid WIF key
commands.append(["-c addrfrompub -p 02b905509e4c9bd9b2fc87c95a6e6897f70ee9fd8bd2f1d9dc9a270b62ec11f47e", 1])
commands.append(["-c addrfrompub -k 02b905509e4c9bd9b2fc87c95a6e6897f70ee9fd8bd2f1d9dc9a270b62ec11f47e", 0])
commands.append(["-c hdgenmaster", 0])
commands.append(["-c hdderive -p xpub6MR9tbm8V5pGFTQ9hTATxd4kPgdKKqU75ED8s3rddrSknLHgZy1H4Wh596jgoYNH7WNcKEVM1wfKD2pTSdj5Hm7CMJwwyRjHYPQCT2LJXwm -m m/100h/10h/100/10", 1]) #hardened keypath with pubkey
commands.append(["-c hdderive -p xprv9s21ZrQH143K3jC7xiaZ4EWrJdwJgtrEBmbVBpnoLNo91RCkdzkviG2GjgmN7xaDSDgPihJWu7JRGVcLUSoJdW8fHhGSpjQGUMoU2e8KjBY -m m/100h/10h/100/10", 0])
commands.append(["-c hdderive -p xprv9s21ZrQH143K3jC7xiaZ4EWrJdwJgtrEBmbVBpnoLNo91RCkdzkviG2GjgmN7xaDSDgPihJWu7JRGVcLUSoJdW8fHhGSpjQGUMoU2e8KjBY -m n/100h/10h/100/10", 1]) #wrong keypath prefix
commands.append(["-c hdderive -p xpub6MR9tbm8V5pGFTQ9hTATxd4kPgdKKqU75ED8s3rddrSknLHgZy1H4Wh596jgoYNH7WNcKEVM1wfKD2pTSdj5Hm7CMJwwyRjHYPQCT2LJXwm -m m/100/10/100/10", 0])
commands.append(["-c hdderive", 1]) #missing key
commands.append(["-c hdderive -p xpub6MR9tbm8V5pGFTQ9hTATxd4kPgdKKqU75ED8s3rddrSknLHgZy1H4Wh596jgoYNH7WNcKEVM1wfKD2pTSdj5Hm7CMJwwyRjHYPQCT2LJXwm", 1]) #missing keypath

commands2 = []
commands2.append(["0200000001554fb2f97f8fe299bf01004c70ec1930bc3c51fe162d1f81b18089e4f7cae470000000006a47304402207f5af3a9724be2946741e15b89bd2c989c9c20a0dfb519cb14b4efdaad945dc502206507ec7a3ba91be7794312961294c7a09a7bc693d918ab5a93712ff8576995fc012103caef57fae78ec425f5ff99d805fddd2417f3bfa7c7b0ec3b6b860cf6cc0e1d99ffffffff0100c63e05000000001976a91415e7469e21938db38e943abd7a2c1073c00e0edd88ac00000000", 0])
commands2.append(["-v", 1])
commands2.append(["?", 1])
commands2.append(["-t -s 10 -d 0200000001554fb2f9", 1])
commands2.append(["-t -s 5 -i 127.0.0.1:18444 0200000001554fb2f97f8fe299bf01004c70ec1930bc3c51fe162d1f81b18089e4f7cae470000000006a47304402207f5af3a9724be2946741e15b89bd2c989c9c20a0dfb519cb14b4efdaad945dc502206507ec7a3ba91be7794312961294c7a09a7bc693d918ab5a93712ff8576995fc012103caef57fae78ec425f5ff99d805fddd2417f3bfa7c7b0ec3b6b860cf6cc0e1d99ffffffff0100c63e05000000001976a91415e7469e21938db38e943abd7a2c1073c00e0edd88ac00000000", 0])


baseCommand = "./bitcointool"
baseCommand2 = "./bitcoin-send-tx"
if valgrind == True:
    baseCommand = "valgrind --leak-check=full "+baseCommand
    baseCommand2 = "valgrind --leak-check=full "+baseCommand2

errored = False
for cmd in commands:
    retcode = call(baseCommand+" "+cmd[0], shell=True)
    if retcode != cmd[1]:
        print("ERROR during "+cmd[0])
        sys.exit(os.EX_DATAERR)

errored = False
for cmd in commands2:
    retcode = call(baseCommand2+" "+cmd[0], shell=True)
    if retcode != cmd[1]:
        print("ERROR during "+cmd[0])
        sys.exit(os.EX_DATAERR)
        
sys.exit(os.EX_OK)
