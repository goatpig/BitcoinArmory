---
layout: page
title: Frequently Asked Questions
permalink: /docs/faq
id: faq
---

* Table of Contents
{:toc}

## What is Bitcoin?

Bitcoin is a decentralized peer-to-peer digital currency. Using cryptography and peer-to-peer networking, Bitcoin allows people to securely and nearly instantly transfer money to other people at a very low cost. The decentralized consensus mechanism ensures that everyone using Bitcoin follows the same rules. This prevents Bitcoin from being controlled by a central authority and prevents the counterfeiting of money.

Bitcoin is also a trustless system. There is no central authority to trust, in fact, no full node on the network trusts any other full node. Instead each node will verify every single block and transaction to ensure that the data it receives conforms to its own rules, which must be the rules that everyone else follows in order for that node to be a part of the network.

For more information, please visit [https://bitcoin.org](https://bitcoin.org) and [https://www.weusecoins.com/](https://www.weusecoins.com/).

## What is Armory?

Armory is a Bitcoin wallet. It stores and protects the private keys necessary for you to spend Bitcoin. It keeps track of all of the Bitcoin that you have sent and received and allows you to spend Bitcoin with ease. Armory's primary focus is for absolute security. The cryptographic schemes were chosen for their robustness and resistance to attack. The ability to use airgapped storage and cold storage allow for the best security we could think of, physical separation. Overall, Armory is designed to be the most secure Bitcoin wallet ever.

## What command-line options does Armory have?

The Armory command line arguments are as follows:

~~~
  -h, --help            show this help message and exit
  --settings=SETTINGSPATH
                        load Armory with a specific settings file
  --datadir=DATADIR     Change the directory that Armory calls home
  --satoshi-datadir=SATOSHIHOME
                        The Bitcoin-Core/bitcoind home directory
  --satoshi-port=SATOSHIPORT
                        For Bitcoin-Core instances operating on a non-standard
                        port
  --satoshi-rpcport=SATOSHIRPCPORT
                        RPC port Bitcoin-Core instances operating on a non-
                        standard port
  --dbdir=ARMORYDBDIR   Location to store blocks database (defaults to
                        --datadir)
  --rpcport=RPCPORT     RPC port for running armoryd.py
  --testnet             Use the testnet protocol
  --regtest             Use the Regression Test Network protocol
  --offline             Force Armory to run in offline mode
  --nettimeout=NETTIMEOUT
                        Timeout for detecting internet connection at startup
  --interport=INTERPORT
                        Port for inter-process communication between Armory
                        instances
  --debug               Increase amount of debugging output
  --nologging           Disable all logging
  --netlog              Log networking messages sent and received by Armory
  --logfile=LOGFILE     Specify a non-default location to send logging
                        information
  --mtdebug             Log multi-threaded call sequences
  --skip-online-check   Go into online mode, even if internet connection isn't
                        detected
  --skip-stats-report   Does announcement checking without any OS/version
                        reporting (for ATI statistics)
  --skip-announce-check
                        Do not query for Armory announcements
  --tor                 Enable common settings for when Armory connects
                        through Tor
  --keypool=KEYPOOL     Default number of addresses to lookahead in Armory
                        wallets
  --redownload          Delete Bitcoin-Core/bitcoind databases; redownload
  --rebuild             Rebuild blockchain database and rescan
  --rescan              Rescan existing blockchain DB
  --rescanBalance       Rescan balance
  --test-announce       Only used for developers needing to test announcement
                        code with non-offline keys
  --nospendzeroconfchange
                        All zero-conf funds will be unspendable, including
                        sent-to-self coins
  --multisigfile=MULTISIGFILE
                        File to store information about multi-signature
                        transactions
  --force-wallet-check  Force the wallet sanity check on startup
  --disable-modules     Disable looking for modules in the execution directory
  --disable-conf-permis
                        Disable forcing permissions on bitcoin.conf
  --disable-detsign     Disable Transaction Deterministic Signing (RFC 6979)
  --enable-detsign      Enable Transaction Deterministic Signing (RFC 6979) -
                        Enabled by default
  --armorydb-ip=ARMORYDB_IP
                        Set remote DB IP (default: 127.0.0.1)
  --armorydb-port=ARMORYDB_PORT
                        Set remote DB port (default: 9050)
  --ram-usage=RAM_USAGE
                        Set maximum ram during scans, as 128MB increments.
                        Defaults to 4
  --thread-count=THREAD_COUNT
                        Set max thread count during builds and scans. Defaults
                        to CPU total thread count
  --db-type=DB_TYPE     Set db mode, defaults to DB_FULL
  --port=PORT           Unit Test Argument - Do not consume
  --verbosity=VERBOSITY
                        Unit Test Argument - Do not consume
  --coverage_output_dir=COVERAGEOUTPUTDIR
                        Unit Test Argument - Do not consume
  --coverage_include=COVERAGEINCLUDE
                        Unit Test Argument - Do not consume
  --language=LANG       Set the language for the client to display in. Use the 
                        ISO 639-1 language code to choose a language. Options are 
                        da, de, en, es, el, fr, he, hr, id, ru, sv. Default is en.
~~~

## What if Armory stops being developed?

Because Armory is a desktop client, so long as you have a copy of the software and your wallet files, you will be able to spend your Bitcoin. Armory does not rely on any centralized service in order to spend Bitcoin. It will continue to function as it used to so long as there have not been many major consensus changes. Even if there are Armory may still work as it has no networking components and relies on Bitcoin Core for networking and consensus.

## How often should I backup?

You should backup your wallet frequently, but only one backup is truly necessary. This is because Armory uses a deterministic wallet; all of the addresses are derived from a specific root algorithmically. This algorithm ensures that the same addresses are derived every time for a given root. This means that you only need to have one backup and you will be able to recover all of the addresses that you have every used from that wallet.

## What Bitcoin Improvement Proposals (BIPs) does Armory support?

Armory currently supports:

 - BIPs 11, 13, and 16 for Pay-to-Script-Hash addresses and multisignature scripts
 - BIP 14 for the protocol version and user agent string
 - BIP 21 for the Bitcoin URI scheme
 - BIP 31 for the pong network message
 - BIPs 62 and 66 for Low-S and Strict DER signatures
 - BIPs 141, 143, and 144 for Segregated Witness (Except for deployment)

There is planned support for:
 - BIP 32 for Hierarchical Determinisitc wallets

## Will Armory work with full nodes other than Bitcoin Core?

If the full node is based on Bitcoin Core, then yes. Otherwise, most likely not. Armory directly reads from the block data files that Bitcoin Core and its forks produces. It also relies on the p2p network messages and the JSON-RPC server in order to communicate with Bitcoin Core. If the full node software does not use the same block data file format used by Bitcoin Core or does not support the same JSON-RPC functions, then it will not be compatible with Armory.
