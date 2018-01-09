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

## What Bitcoin Improvement Proposals (BIPs) does Armory supports?

Armory currently supports:

 - BIPs 11, 13, and 16 for Pay-to-Script-Hash addresses and multisignature scripts
 - BIP 14 for the protocol version and user agent string
 - BIP 21 for the Bitcoin URI scheme
 - BIP 31 for the pong network message
 - BIPs 62 and 66 for Low-S and Strict DER signatures
 - BIPs 141, 143, and 144 for Segregated Witness (Except for deployment)

There is planned support for:
 - BIP 32 for Heirarchical Determinisitc wallets

## Will Armory work with full nodes other than Bitcoin Core?

If the full node is based on Bitcoin Core, then yes. Otherwise, most likely not. Armory directly reads from the block data files that Bitcoin Core and its forks produces. It also relies on the p2p network messages and the JSON-RPC server in order to communicate with Bitcoin Core. If the full node software does not use the same block data file format used by Bitcoin Core or does not support the same JSON-RPC functions, then it will not be compatible with Armory.

## What do I need in order to run Armory?
That depends on your setup. In order to run Armory, look at the [README](https://github.com/goatpig/BitcoinArmory/blob/master/README.md), along with a [macOS-specific README](https://github.com/goatpig/BitcoinArmory/blob/master/README_macOS.md). These will mention the software you need to run Armory and how to install it. Instructions will also be included for those who wish to compile Armory.

A special note must be made regarding [Bitcoin Core](https://bitcoincore.org/). If you're using an online/offline setup or an online-only setup, you *must* install Bitcoin Core on the same online machine as Armory. This is *mandatory*. The online portion of Armory relies on Core in multiple ways. In particular, Core downloads the blockchain that Armory uses, and Core broadcasts Armory's transactions onto the Bitcoin Core network. As of the beginning of 2018, the blockchain is ~185 GB large. Keep that in mind when you install Armory, especially if your ISP has monthly bandwidth caps.

In addition, if you wish to send coins or check whether your wallets have received coins, Armory and Core *must run at the same time*. Armory can run on its own. However, running Core alongside Armory is *mandatory* if you wish to actually see your (updated) balance.

## OH MAN OH MAN BITCOINZ GUNNA MKE ME RICH AF A BUDY SED EYE SHULD UZE ARMORY IM DOWNLODIN IT NOW MUNEE MUNEE CASH MUNEE...
Slow down. Armory is a wallet that expects its user to understand the fundamentals of Bitcoin, along with understand the requirements in order for Armory to work properly. The Armory devs are happy that people want to use Armory. The devs also do *not* want to babysit people who don't know what they're doing. People who show a basic understanding of how Armory works will receive far more help than people who show up and just say, "I cant see my coins, im losing money,Armory sux."

## Okay. What's a safe way to learn how Armory works?
If you insist on experimenting with random wallets, do *not* use "mainnet" coins (i.e., the coins worth fiat currency)! That's extremely hazardous behavior. Get some "testnet" coins from a [faucet](https://testnet.manu.backend.hamburg/faucet) and activate Armory in testnet mode. The testnet coins are worth nothing and are intended for developers and for users experimenting with Bitcoin. If something goes wrong, you lose nothing other than a bit of your time. Once Bitcoin Core is run in testnet mode and is synchronized with the Bitcoin Core test network, you can run Armory in testnet mode and use Armory to control your testnet coins.

## Are there any Armory tutorials?
Yes, on YouTube. These tutorials may be a bit dated but should show you some basics regarding how to use Armory. If anybody wishes to create tutorials, or at least suggest tutorials they found particularly helpful, please make a suggestion!

## Why's Armory bothering me about backups? What's the big deal?
(FINISH)

## What's this whole offline/online thing? How does it work?
(FINISH)

## HELP! I sent coins to Armory, and they're not showing up in my balance!
Relax. If you sent the coins to an address in an Armory wallet you control, your coins are on the blockchain and are safe. As long as you don't delete your wallet or destroy your backups (paper and/or digital), and sketchy hackers haven't planted malware on your computer, you'll be fine.

Now then, chances are that you raced off, installed Armory, and had coins sent to it without installing Bitcoin Core, or at least letting Core fully sync. Please ensure that Core is fully synced before proceeding. You can tell if Core is synced by running Core, letting it download all the available blocks, and checking the number of blocks available. You can check it using the following methods.

- bitcoin-qt (aka the Bitcoin Core GUI): Hovering your mouse over the checkmark in the lower-right corner will tell you the number of blocks.
- bitcoind: Query bitcoind with the `getblockcount` command.

While Core is running, start Armory in online mode (i.e., the default mode). Once Armory has synced, look in the lower-right corner. There should be green text that says "Connected (XXXXXX blocks)", where XXXXXX equals the number of blocks Armory thinks exist. The numbers produced by Core and Armory should be equal.

If Core is fully synced and Armory shows a different number of blocks, here are some more things you can do.

- Disable Core auto-management in Armory. The feature is a bit problematic, and users have occasionally believed Core is running when it really isn't. Running Core yourself ensures these kinds of mistakes don't happen.
- In the taskbar, choose "Help" -> "Rebuild and Rescan Databases".
- (FINISH)

## Armory said my transaction broadcast timed out!
First off, it's important to understand a bit about how Armory works underneath the hood. Armory pretends to be a "full node" in the eyes of Bitcoin Core. Armory queries Core for blocks and receives blocks as Core receives them. In addition, Armory sends transactions via Core. This is why Armory requires Core. In a sense, Armory acts as a high-security wrapper around Core.

When Armory sends a transaction to Core, a response is expected from Core. Armory has a timer and will throw up an error message if the timer expires. However, for various reasons, Core may not be able to beat the timer. Keep an eye for at least a minute on Armory's transaction list on Armory's splash screen. It is possible that Core will accept the transaction after the timer expires. If so, Armory will add the transaction to your transaction list.

If the transaction isn't accepted at all by Core, and you can't see the transaction on a [block explorer](https://www.blocktrail.com/BTC/), 

## I'm tired of Armory! I want out!!!
If all else fails and you don't want to use Armory any more, you have a couple of options.

- If you can access your coins and don't mind a potential loss of privacy, you can just have all of the coins sent to an address controlled by an alternate wallet you wish to use.
- If you can't access your coins, you can export the private keys into a wallet you trust. (FINISH)

## Will Armory support Lightning?
To be determined. (Caveat: I don't know goatpig's feelings, so the following is my opinion only for now.) Lightning is fascinating technology. Lightning also functions differently compared to Core. It may be possible for Armory to work with a Lightning node in a manner similar to a Core node. This way, Armory doesn't have to be redundant and re-implement various features required by the Lightning network.

In theory, Armory can already supply the coins for the initial channel opening, and an address for the channel closing. The coins may need to come from a SegWit wallet (CONFIRM) but channel openings & closing are simple Bitcoin transactions. If the Lightning nodes can talk to Armory, it may be possible to provide further functionality. If not, support in Armory is highly doubtful. There's also an open question regarding whether or not Armory can or should monitor the network for events that mandate a channel closing.

## Will Armory support Bitcoin Cash?
Sort of. Armory does have the Cash signing algorithm and is, as of the beginning of 2018, able to send and receive coins on the Bitcoin Cash network. However, goatpig, the maintainer of Armory, has made it clear that his support of Cash on Armory is essentially limited to allowing users to move their coins to another wallet ASAP. Anybody expecting to use Armory with Cash long-term runs the risk of being unable to access their coins in the event of a fork (soft or hard) on the Cash network.

## How can I use Armory and Bitcoin Cash?
[This forum thread](https://bitcointalk.org/index.php?topic=2070058.0) started by goatpig explains how to safely move your coins. If you've already used the UTXOs on the Core network (i.e., spent all the coins or moved all the coins to other addresses), you don't have to worry about [replay attacks](https://bitcoin.stackexchange.com/a/61213). If you haven't, follow goatpig's directions for ensuring that you're not susceptible to replay attacks.

## Will Armory support Bitcoin Gold?
Yes, but only to the same basic extent as Cash (i.e., users can move their coins to another wallet, preferably ASAP). The same basic directions for Cash apply here, although Gold has no replay attack protections and requires extra vigilance when moving coins.

## Will Armory support Scamcoin, the newest and MOST TOTALLY AMAZINGER BESTEST "airdrop" altcoin that forks off of Bitcoin Core's blockchain?
Probably not. Never say never but diminishing returns means these coins are the virtual equivalent of [penny stocks](https://www.investopedia.com/terms/p/pennystock.asp), assuming they're not outright scams. It's simply not worth the time of the Armory devs to support the coins, especially if the coin devs can't be bothered to explain their changes to Armory devs. Patches for Armory may be submitted but there's no guarantee they'll be accepted, especially if they alter the GUI or make other fundamental changes to how Armory works.

## Will Armory support Ethereum?
Probably not, but again, never say never. The answer would depend in large part on the amount of technical changes necessary in order to support the Ethereum network, along with the Ethereum network supporting Armory features whenever possible.

## Can I fork Armory? I want a version that supports an altcoin, adds features not meant for the mainline, etc.
Sure. Armory is open source. As long as your project follows the appropriate licenses, do what you want! For example, there's a fork that [supports Namecoin](https://github.com/josephbisch/BitcoinArmory/tree/namecoin). Depending on the nature of your project, you're also welcome to submit patches.

## Will Armory add Random Feature X?
Maybe? Devs work on what they want to work on. Want a feature? Write a patch and submit it.

## What's with the multiple web sites and repositories?
Armory is now maintained as a community project by goatpig, a former developer at Armory Technologies, Inc. (the business entity that maintained Armory from 2013-2016). He has [a new repo](https://github.com/goatpig/BitcoinArmory) and [web site](https://btcarmory.com/). These are what should be considered the most up-to-date sources for Armory info and materials.

The [original Armory repo](https://github.com/etotheipi/BitcoinArmory) is no longer maintained. It was created by Alan Reiner (Armory's founder) and is now considered abandoned, as Alan moved on in Feb. 2016. The [original web site](https://www.bitcoinarmory.com/) is the intellectual property of ATI and is maintained by principals at ATI. As of the beginning of 2018, the site carries only "major" releases (e.g., 0.96), and not "minor" updates (e.g., 0.96.1) that may contain important bug fixes and new features.

## I'm having a problem with Armory and want to talk to someone! Where should I go?
[This page](https://btcarmory.com/contact/) should have the most up-to-date info. Devs may visit other sites (e.g., Reddit) or IRC channels but there are no guarantees.

Issues and patches may also be submitted on [goatpig's Armory repo](https://github.com/goatpig/BitcoinArmory). Note that the issues and patches aren't checked on a regular schedule. In addition, patch authors will almost certainly be asked to patch against a branch other than the master branch. If in doubt (and the patch is of sufficient quality), patch against the master branch, and you should be given further instructions later.
