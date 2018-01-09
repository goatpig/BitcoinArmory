---
layout: page
title: About
permalink: /about/
id: about
---

Armory is a full-featured Bitcoin client, offering a dozen innovative features not found in any other client software! Manage multiple wallets (deterministic and watching-only), print paper backups that work forever, import or sweep private keys, and keep your savings in a computer that never touches the internet, while still being able to manage incoming payments, and create outgoing payments with the help of a USB key.

**Armory has no independent networking components built in.** Instead, it relies on on the Satoshi client to securely connect to peers, validate blockchain data, and broadcast transactions for us. Although it was initially planned to cut the umbilical cord to the Satoshi client and implement independent networking, it has turned out to be an inconvenience worth having. Reimplementing all the networking code would be fraught with bugs, security holes, and possible blockchain forking. The reliance on Bitcoin Core actually makes Armory more secure!

Credits
-------
**Open Source Project:** 

 - Farhod Fathpour ([@goatpig](https://github.com/goatpig)) - Open Source Project Lead 
 - Andrew Chow ([@achow101](https://github.com/achow101)) - Developer, website maintainer
 - Douglas Roark ([@droark](https://github.com/droark)) - Developer, macOS build maintainer

[Current source code repository](https://github.com/goatpig/BitcoinArmory)

**Armory Technologies, Inc. (No longer participating):** 

 - Alan Reiner ([@etotheipi](https://github.com/etotheipi)) - Project Creator and Former Project Lead 
 - Andy Ofiesh ([@AndyOfiesh](https://github.com/andyofiesh)) - Former Lead Developer 
 - Farhod Fathpour ([@goatpig](https://github.com/goatpig)) - Former Developer 
 - Charles Samuels ([@njaard](https://github.com/njaard)) - Former Developer
 - Douglas Roark ([@droark](https://github.com/droark)) - Former Developer
 - Jimmy Song ([@jimmysong](https://github.com/jimmysong)) - Former Developer

[Original source code repository](https://github.com/etotheipi/BitcoinArmory)

**The following libraries are used by Armory:**

 - [Crypto++](https://www.cryptopp.com/)  (Customized version with RFC 6979 support)
 - [psutil](https://pypi.python.org/pypi/psutil)
 - [PyQt](https://sourceforge.net/projects/pyqt/)
 - [Python](https://www.python.org/)
 - [setuptools](https://pypi.python.org/pypi/setuptools)
 - [Twisted](https://twistedmatrix.com/)  (*armoryd* only)

**The following tools are used to compile Armory:**

 - [Automake](https://www.gnu.org/software/automake/)
 - [libtool](https://www.gnu.org/software/libtool/)
 - [sip](https://sourceforge.net/projects/pyqt/files/sip/)  (macOS only)
 - [SWIG](http://swig.org/)
 