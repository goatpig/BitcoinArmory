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

## What do I need in order to run Armory?
That depends on your setup. In order to run Armory, look at the [README](https://github.com/goatpig/BitcoinArmory/blob/master/README.md), along with a [macOS-specific README](https://github.com/goatpig/BitcoinArmory/blob/master/README_macOS.md). These will mention the software you need to run Armory and how to install it. Instructions will also be included for those who wish to compile Armory.

A special note must be made regarding [Bitcoin Core](https://bitcoincore.org/). If you're using an online/offline setup or an online-only setup, you *must* install Bitcoin Core on the same online machine as Armory. This is *mandatory*. The online portion of Armory relies on Core in multiple ways. In particular, Core downloads the blockchain that Armory uses, and Core broadcasts Armory's transactions onto the Bitcoin network. As of Jan. 2018, the blockchain is ~185 GB large, with the blockchain size growing ~4-5 GB every month. Keep that in mind when you install Armory, especially if your ISP has monthly bandwidth caps.

In addition, if you wish to send coins or check whether your wallets have received coins, Armory and Core *must run at the same time*. Armory can run on its own on offline and online machines. However, running Core alongside Armory is *mandatory* if you wish to actually see your (updated) balance.

## O MAN O D000000D BITCOINZ GUNNA MK ME RICH AF A BUDY SED EYE SHULD UZE ARMORY IM DOWNLODIN IT NOW GETIN MAH MAZRATEE 2MORO...
**Slow down.** Armory is an advanced piece of software that expects its user to understand the fundamentals of Bitcoin, along with understand the requirements for Armory to work properly. The Armory devs are happy that people want to use Armory. The devs also *will not* babysit people who don't know what they're doing. People who show a basic understanding of how Armory works will receive far more help than people who just show up and say, "I cant see my coins imlosing money,Armory sux." Please save everybody the headache by doing your homework before using Armory.

## Okay. What's a safe way to learn how Armory works?
There are some tutorial videos on YouTube. Armory Technologies, Inc. (the business entity that maintained Armory from 2013-2016) uploaded some videos to YouTube [here](https://www.youtube.com/channel/UCrtq0Igz3uUVXHsvBtQgELA). The tutorials should still be useful but current devs make no promises regarding the accuracy of the info in the videos as it relates to Armory starting with version 0.94 (i.e., the first "community" or "post-ATI" version of Armory).

Arguably, there's no better teacher than actually doing something. So, we recommend that you experiment with Armory and Core. When experimenting, do *not* use "mainnet" coins (i.e., the coins worth fiat currency)! That's extremely hazardous behavior. Get some "testnet" coins from a [faucet](https://testnet.manu.backend.hamburg/faucet) and activate Armory in testnet mode. The testnet coins are worth nothing and are intended for developers and for users experimenting with Bitcoin. If something goes wrong, you lose nothing of value other than a bit of your time.

Once Core is run in testnet mode and is synchronized with the Bitcoin test network, you can run Armory in testnet mode and use Armory to control your testnet coins. People on various IRC channels may be able to make testnet transactions with you if you need to practice making transactions. Once you are comfortable with using Armory to send and receive testnet coins in your preferred method, you can feel secure moving over to mainnet.

## Uh oh. When I run Bitcoin Core in testnet mode, it complains about "unknown new rules".
Ignore that warning. Awhile back, some "altcoin" developers used testnet to "fork" the network. To this day, Core throws up warnings related to the fork. Said warnings can be ignored, and as Jan. 2018, said warnings won't appear on mainnet.

## What Bitcoin Improvement Proposals (BIPs) does Armory support?
Armory currently supports:

 - BIPs [11](https://github.com/bitcoin/bips/blob/master/bip-0011.mediawiki), [13](https://github.com/bitcoin/bips/blob/master/bip-0013.mediawiki), and [16](https://github.com/bitcoin/bips/blob/master/bip-0016.mediawiki) for Pay-to-Script-Hash (P2SH) addresses and P2SH multisignature scripts.
 - BIP [14](https://github.com/bitcoin/bips/blob/master/bip-0014.mediawiki) for the protocol version and user agent string.
 - BIP [21](https://github.com/bitcoin/bips/blob/master/bip-0021.mediawiki) for the Bitcoin URI scheme.
 - BIP [31](https://github.com/bitcoin/bips/blob/master/bip-0031.mediawiki) for the *pong* network message.
 - BIPs [62](https://github.com/bitcoin/bips/blob/master/bip-0062.mediawiki) and [66](https://github.com/bitcoin/bips/blob/master/bip-0066.mediawiki) for [low-S](https://github.com/bitcoin/bips/blob/master/bip-0062.mediawiki#Low_S_values_in_signatures) and strict [DER](http://luca.ntop.org/Teaching/Appunti/asn1.html) signatures.
 - BIP [125](https://github.com/bitcoin/bips/blob/master/bip-0125.mediawiki) for replace-by-fee transaction support.
 - BIPs [141](https://github.com/bitcoin/bips/blob/master/bip-0141.mediawiki), [143](https://github.com/bitcoin/bips/blob/master/bip-0143.mediawiki), and [144](https://github.com/bitcoin/bips/blob/master/bip-0144.mediawiki) for Segregated Witness (except for deployment).

There is planned support for:

 - BIP [32](https://github.com/bitcoin/bips/blob/master/bip-0032.mediawiki) for Hierarchical Deterministic wallets.

## What command-line options does Armory have?
As of v0.96 (**CONFIRM THIS**), the Armory command line arguments are as follows:

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
Because Armory is a desktop client, so long as you have a copy of the software and your wallet files, you will be able to spend your Bitcoin. Armory does not rely on any centralized service in order to spend Bitcoin. It will continue to function as intended to so long as there have not been many major consensus changes to the Bitcoin network, and the underlying tools required by library can run correctly on the target PC. Even if there are network changes, Armory may still work as it has no networking components and relies on Bitcoin Core for networking and consensus.

Note that one area where Armory will have issues if not properly updated is reading the blockchain. When [Segregated Witness](https://bitcoincore.org/en/2016/01/26/segwit-benefits/) was added to the Bitcoin network, the data written to blockchain files by Core changed slightly. This change rendered any pre-0.95 version of Armory, including all versions of Armory maintained by ATI, unable to function properly in online mode. Armory would crash and potentially leave the user confused. If users are starting Armory for the first time in a long time, users are highly encouraged, if at all possible, to upgrade to the latest version known to be safe to use.

Finally, because Armory is open source software, anyone may fork the software and maintain their own version at any time. Even if goatpig no longer develops Armory, check around and make sure somebody else hasn't picked up the ball.

## Why's Armory bothering me about backups? What's the big deal?
(FINISH)

## How often should I back up my Armory wallet?
You should backup your wallet frequently, but only one backup is truly necessary. This is because Armory uses a deterministic wallet. All of the addresses generated by the wallet are derived from a specific root algorithmically. This algorithm ensures that the same addresses are derived every time for a given root. This means that you only need to have one backup and you will be able to recover all of the addresses that you have every used from that wallet.

## What's this whole offline/online thing? How does it work?
The typical Bitcoin wallet operates in what could be called "online mode." The wallet requires some sort of connection to the Bitcoin network in order to observe the wallet balance and to conduct transactions. This is potentially problematic for many reasons. Hackers can access the computer and wreak all kinds of havoc. Even connecting to a network via wi-fi network via a [pineapple box](https://www.wifipineapple.com/) could potentially cause your keys to be stolen when the pineapple box finds your keys and uses a keylogger to steal the password you use to authorize transactions.

Armory offers users several potent alternatives to storing everything on an online computer. On top of creating backups that can then be stored securely while the original copies are destroyed, Armory offers "cold storage," also known as "offline mode." Backing up a bit, Bitcoin relies, among other things, on [public key cryptography](https://en.wikipedia.org/wiki/Public-key_cryptography). Here, a user has two intertwined keys: A public key and a private key. The user doesn't care (in theory, at least) if anybody has the public key but cares very much if others have access to the private key. With Bitcoin, public keys are used as part of the addresses where coins are sent, and private keys are used to [sign](https://en.wikipedia.org/wiki/Digital_signature) transactions that assign bitcoins to particular public keys (addresses). Because of this, anybody with the private key associated with a given Bitcoin balance can send the coins wherever they want. A password can be used to protect the private key but this is simply another hurdle for determined hackers and criminals to clear, via [technical means](https://bitcointalk.org/index.php?topic=311000.msg3345309#msg3345309) or [coercion](https://xkcd.com/538/).

Back to cold storage, the idea is to take the private key and separate it from the public key. The key is stored such using tools and computers that never go near *any* network and are accessed as little as possible. While it's not completely impossible to steal the keys, attackers have much more work to do.

Armory supports cold storage with a special offline mode. When creating a wallet in Armory, an offline wallet and online wallet can be created. The online wallet is stored on a networked PC in order to monitor the wallet balance while the offline wallet contains the private keys and is accessed using a PC the user considers to be more secure. When the user wishes to make a transaction, the user creates a transaction using the online wallet. Because the online wallet can't sign the transaction, the transaction can't be broadcast on the Bitcoin network. Therefore, the user must transfer the transaction to the offline machine, where the private keys can be used to sign the transaction. The signed transaction is then transported back to the online machine for broadcast on the Bitcoin network. Methods for transporting the transaction are up to the user. Methods include but are not limited to USB drives, burned CDs, and audio transmission of the data.

Note that, even with private keys in cold storage, Armory users will still want to exercise caution with their online machines. While it is impossible to use the watch-only wallet to sign transactions, users will still have their wallet balances exposed. The Armory developers offer no protection from thugs who have discovered that you use Armory to control 10,000 bitcoins!

## What are multi-signature transactions?
Bitcoin is a programmable asset. Among other things, there's no requirement that bitcoins be controlled by only one entity. It is possible to require multiple entities to sign off on a transaction before the transaction is considered valid.

A popular example involves third-party arbitrators. A buyer, a seller, and the arbitrator can create a 2-of-3 "lockbox" the requires two of the three parties to agree that coins will be moved from the buyer to the seller. In the event of a dispute, the arbitrator can listen to both sides and decide whether or not to provide the second signature for the transaction moving the coins to the seller.

Armory fully supports multi-signature transactions via lockboxes. Users can create lockboxes, which are generated using public keys. The lockbox file is sent to each party so that they can provide a public key. Once the lockbox has been finalized and distributed to all relevant parties (including auditors and other relevant parties who can't sign transactions but wish to monitor lockbox activity), anybody can initiate a transaction. If the lockbox requires only one party to sign a transaction (e.g., a married couple using a 1-of-2 lockbox), the transaction may be sent immediately. If more than one signer is required, the partially signed transaction can be sent to another lockbox member so that another signature can be added. This process is repeated until enough signatures have been collected, at which point the transaction can be broadcast on the Bitcoin network.

Armory supports lockboxes with up to seven signatories (i.e., 7-of-7 lockboxes). Once v2.0 wallets are created, Armory may support up to fifteen signatories (**CONFIRM THIS**). In addition, lockboxes are compatible with cold storage. Signatories may store their private keys offline and sign the lockbox transaction on an offline machine.

## HELP! I sent coins to Armory, and they're not showing up in my balance!
Relax. If you sent the coins to an address in an Armory wallet you control, your coins are on the blockchain and are safe. As long as you don't delete your wallet or destroy your backups (paper and/or digital), and hackers haven't planted coin-stealing malware on your computer, you'll be fine.

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

## How does Armory handle transaction fees?
Unlike many wallets, Armory is quite flexible regarding fees. Basically, you control your destiny, but Armory will attempt to lend a hand. Armory currently offers three methods.

- Fee recommendations from Bitcoin Core (the [*estimatefee*](https://bitcoin.org/en/developer-reference#estimatefee) RPC method before Core 0.15, and the [*estimatesmartfee*](https://bitcointechtalk.com/an-introduction-to-bitcoin-core-fee-estimation-27920880ad0) RPC method as of 0.15). Core will analyze the current state of the network and attempt to provote an accurate fee estimate for getting your transaction onto the blockchain in a reasonable amount of time. Starting with Core 0.15, you can choose whether or not you want an aggressive or conservative fee estimate, and give a targeted number of blocks before your transaction will be placed on the blockchain.
- You may set a fee for each byte in the transaction. There are sites from companies like [earn.com](https://bitcoinfees.earn.com/) that can help you determine what you feel is an appropriate fee.
- Set a flat fee for the transaction.

Note that there's no foolproof way to optimize the fee you pay for your transaction. Viewing historical data is fraught with complications, including the fact that a lot of wallets and wallet providers set their fees unnecessarily high. Sky high fees will tilt the fee data and, in the long run, make high fees for all become a self-fulfilling prophecy.

Here are some tips for optimizing your fees.

- If possible, conduct a transaction on the weekend, especially on a Sunday. Historically, Sundays have seen minimal transaction activity, leading to less competition for block space.
- If possible, set a fee such that the transaction probably won't make it onto the blockchain for a day or two. This is more feasible for people sweeping their wallets or conducting other transactions where timely settlement isn't a high priority.
- Use Segregated Witness. On average, fees for transactions from SegWit-enabled coins are ~¼ lower than other coins.
- Minimize the number of [unspent transaction outputs](https://www.r3.com/blog/2017/07/18/what-is-a-utxo/) (UTXOs) used to pay for your transaction. Armory automatically manages your UTXOs via "Coin Control" and attempts to optimize UTXO usage. However, like many other things in Armory, you can exercise control if you wish.

If you set the fee too low and it won't confirm within your preferred timeline, don't worry. You can use the [replace-by-fee](https://bitcoin.org/en/glossary/rbf) feature to resend the transaction with a higher fee.

## I'm tired of Armory! I want out!!!
If all else fails and you don't want to use Armory any more, you have a couple of options.

- If you can access your coins and don't mind a potential loss of privacy, you can just have all of the coins sent to an address controlled by an alternate wallet you wish to use.
- If you can't access your coins, you can export the private keys into a wallet you trust. (FINISH)

## Will Armory support Lightning?
To be determined. Lightning is fascinating technology. Lightning also functions differently compared to Core. It may be possible for Armory to work with a Lightning node in a manner similar to a Core node. This way, Armory doesn't have to be redundant and re-implement various features required by the Lightning network.

In theory, Armory can already supply the coins for the initial channel opening, and an address for the channel closing. The coins may need to come from a SegWit wallet (**CONFIRM THIS**) but channel openings & closing are simple Bitcoin transactions. If the Lightning nodes can talk to Armory, it may be possible to provide further functionality. If not, support in Armory is highly doubtful. There's also an open question regarding whether or not Armory can or should monitor the network for events that mandate a channel closing (e.g., an illegal spend attempt by a counterparty).

## Will Armory work with full nodes other than Bitcoin Core?
If the full node is based on Bitcoin Core, it's possible but not guaranteed. Armory directly reads from the block data files that Core (and Core forks) produce. Core also relies on P2P network messages and a JSON-RPC server in order to communicate with Core. If the full node software does not use the same block data file format used by Core or does not support the same P2P or JSON-RPC functions, the node won't be compatible with Armory. Even then, if the node uses a different transaction signing algorithm or makes other fundamental changes to the network, Armory may not support said changes.

## Does Armory support Bitcoin Cash?
As of Jan. 2018, sort of. Armory does have the Cash signing algorithm and is, as of Jan. 2018, able to send and receive coins on the Bitcoin Cash network. However, goatpig, the maintainer of Armory, has made it clear that his support of Cash on Armory is essentially limited to allowing users to move their coins to another wallet ASAP. Anybody expecting to use Armory with Cash long-term runs the risk of being unable to access their coins in the event of a fork (soft or hard) on the Cash network.

## How can I use Armory and Bitcoin Cash?
[This forum thread](https://bitcointalk.org/index.php?topic=2070058.0) started by goatpig explains how to safely move your coins. If you've already used the UTXOs on the Core network (i.e., spent all the coins or moved all the coins to other addresses), you don't have to worry about [replay attacks](https://bitcoin.stackexchange.com/a/61213). If you haven't, follow goatpig's directions for ensuring that you're not susceptible to replay attacks.

## Will Armory support Bitcoin Gold?
Yes, but only to the same basic extent as Cash (i.e., users can move their coins to another wallet, preferably ASAP). The same basic directions for Cash apply here, although Gold has no replay attack protections and requires extra vigilance when moving coins.

## Will Armory support Scamcoin, the newest and MOST TOTALLY AMAZINGER BESTEST "airdrop" altcoin that forks off of Bitcoin Core's blockchain?
Probably not. Never say never but diminishing returns means these coins are the cryptocurrency equivalent of [penny stocks](https://www.investopedia.com/terms/p/pennystock.asp), assuming they're not outright scams. It's simply not worth the time of the Armory devs to support the coins, especially if the coin devs can't be bothered to explain their changes to Armory devs. Patches for Armory may be submitted but there's no guarantee they'll be accepted, especially if they alter the GUI or make other fundamental changes to how Armory works.

## Will Armory support Ethereum?
Probably not, but again, never say never. The answer would depend in large part on the amount of technical changes necessary in order to support the Ethereum network, along with the Ethereum network supporting Armory features whenever possible.

## Can I fork Armory? I want a version that supports an altcoin, adds features not meant for the mainline, etc.
Sure. Armory is open source. As long as your project follows the appropriate licenses, do what you want! For example, there's a fork that [supports Namecoin](https://github.com/josephbisch/BitcoinArmory/tree/namecoin). Depending on the nature of your project, you're also welcome to submit patches to the Armory baseline.

## Will Armory add Random Feature X?
Maybe? Devs work on what they want to work on. Want a feature and can't wait for it? Write a patch and submit it.

## What's with the multiple websites and repositories for Armory?
Armory is currently maintained as a community project by goatpig, a former developer at Armory Technologies, Inc. (the business entity that maintained Armory from 2013-2016). He has [a new repo](https://github.com/goatpig/BitcoinArmory) and [web site](https://btcarmory.com/). These are what should be considered the most up-to-date sources for Armory info and materials.

The [original Armory repo](https://github.com/etotheipi/BitcoinArmory) is no longer maintained. It was created by Alan Reiner (Armory's founder) and is now considered abandoned, as Alan moved on in Feb. 2016. The [original web site](https://www.bitcoinarmory.com/) is the intellectual property of ATI and is maintained by principals at ATI. Despite ATI no longer maintaining Armory, the website does see occasional updates. People who wish to create pull requests may do so [here](https://github.com/sunnankar/bitcoinarmory). Note that goatpig and other Armory devs have no control over *any* repo controlled by ATI.

## I'm having a problem with Armory and want to talk to someone! Where should I go?
[This page](https://btcarmory.com/contact/) has the most up-to-date info. Devs may visit other websites (e.g., Reddit) or IRC channels but there are no guarantees.

Issues and patches may also be discussed on [goatpig's Armory repo](https://github.com/goatpig/BitcoinArmory). Note that the issues and patches aren't checked on a regular schedule. In addition, patch authors will almost certainly be asked to patch against a branch other than the master branch. If in doubt (and the patch is of sufficient quality), patch against the master branch, and you should be given further instructions later.
