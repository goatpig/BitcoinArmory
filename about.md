---
layout: page
title: About
permalink: /about/
id: about
---

Armory is a full-featured Bitcoin client, offering a dozen innovative features not found in any other client software! Manage multiple wallets (deterministic and watching-only), print paper backups that work forever, import or sweep private keys, and keep your savings in a computer that never touches the internet, while still being able to manage incoming payments, and create outgoing payments with the help of a USB key.

**Armory has no independent networking components built in.** Instead, it relies on on the Satoshi client to securely connect to peers, validate blockchain data, and broadcast transactions for us. Although it was initially planned to cut the umbilical cord to the Satoshi client and implement independent networking, it has turned out to be an inconvenience worth having. Reimplementing all the networking code would be fraught with bugs, security holes, and possible blockchain forking. The reliance on Bitcoin-Qt right now is actually making Armory more secure!

Credits
-------
**Open Source Project:** 

 - Farhod Fathpour (@goatpig) - Open Source Project Lead 
 - Jimmy Song (@jimmysong) - Contributor

[Current Source code repository](https://github.com/goatpig/BitcoinArmory)

**Armory Technologies Incorporated (No longer participating):** 

 - Alan Reiner (@etotheipi) - Project Creator and Former Project Lead 
 - Andy Ofiesh (@AndyOfiesh) - Former Lead Developer 
 - Charles Samuels (@njaard) - Former Developer
 - Douglas Roark (@droark) - Former Developer
 - Jimmy Song (@jimmysong) - Former Developer

[Original Source code repository](https://github.com/etotheipi/BitcoinArmory)

**The following libraries:**

 - [Crypto++](https://www.cryptopp.com/)
 - [SWIG](http://www.swig.org/)
 - [Python](https://www.python.org/)
 - [Python Twisted](https://twistedmatrix.com/)
 - [PyQt](https://wiki.python.org/moin/PyQt)
