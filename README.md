# Armory

**Created by Alan Reiner on 13 July, 2011**

**Forked by goatpig in February 2016**

[Armory](https://github.com/goatpig/BitcoinArmory) is a full-featured Bitcoin client, offering a dozen innovative features not found in any other client software! Manage multiple wallets (deterministic and watching-only), print paper backups that work forever, import or sweep private keys, and keep your savings in a computer that never touches the internet, while still being able to manage incoming payments, and create outgoing payments with the help of a USB key.

Multi-signature transactions are accommodated under-the-hood about 80%, and will be completed and integrated into the UI soon.

**Armory has no independent networking components built in.** Instead, it relies on on the Satoshi client to securely connect to peers, validate blockchain data, and broadcast transactions for us.  Although it was initially planned to cut the umbilical cord to the Satoshi client and implement independent networking, it has turned out to be an inconvenience worth having. Reimplementing all the networking code would be fraught with bugs, security holes, and possible blockchain forking.  The reliance on Bitcoin Core right now is actually making Armory more secure!

## Development

Current release target is 0.97. Follow development on projects page: https://github.com/users/goatpig/projects/1/views/1

## Building Armory From Source

[Instructions for Windows](windowsbuild/Windows_build_notes.md)

`build.bat Debug|Release`

[Instructions for macOS](osxbuild/macOS_build_notes.md)
[Instructions for Ubuntu and Arch Linux](linuxbuild/Linux_build_notes.md)

```
mkdir build
cd build
cmake ..
make -j<N>
```

### Dependencies

* GNU Compiler Collection
 Linux:   Install package `g++`

* Crypto++
 Linux:   Install package `libcrypto++-dev`
 Windows: [Download](https://www.cryptopp.com/#download)

* SWIG
 Linux:   Install package `swig`
 Windows: [Download](http://www.swig.org/download.html)  
 MSVS: Copy swigwin-2.x directory next to cryptopp as `swigwin`

* Python 2.6/2.7
 Linux:   Install package `python-dev`
 Windows: [Download](https://www.python.org/getit/)

* Python Twisted -- asynchronous networking
 Linux:   Install package `python-twisted`
 Windows: [Download](https://twistedmatrix.com/trac/wiki/Downloads)

* PyQt 4 (for Python 2.X)
 Linux:   Install packages `libqtcore4`, `libqt4-dev`, `python-qt4`, and `pyqt4-dev-tools`
 Windows: [Download](https://riverbankcomputing.com/software/pyqt/download)

* py2exe
 (OPTIONAL - if you want to make a standalone executable in Windows)
 Windows: [Download](http://www.py2exe.org/)

* LMDB - database engine, modified to suit Armory's use cases
[LMDB page](http://symas.com/mdb/) - No need for external installs by Armory users

* libwebsockets
 Linux:   Final instructions TBA.
 Windows: Follow the "Windows binary build" directions [here](https://github.com/warmcat/libwebsockets/blob/master/README.md).

* Google Protocol Buffers (protobuf)
 Linux:   Install the `protobuf` package.
 Windows: Follow the "C++ Installation - Windows" directions [here](https://github.com/google/protobuf/blob/master/src/README.md), downloading only the `protoc` binary.

* macOS
 [Instructions for downloading, verifying, and running Armory on macOS](README_macOS.md).

## Sample Code

Armory contains tens of thousands of lines of code, between the C++ and python libraries.  This can be very confusing for someone unfamiliar with the code (you).  Below I have attempted to illustrate the CONOPS (concept of operations) that the library was designed for, so you know how to use it in your own development activities.  There is a TON of sample code in the following:

* C++ -   [BlockUtilsTest.cpp](cppForSwig/BlockUtilsTest.cpp)
* Python -   [Unit Tests](pytest/), [sample_armory_code.py](extras/sample_armory_code.py)

## License

Distributed partially under the GNU Affero General Public License (AGPL v3)  
and the MIT License
See [LICENSE file](LICENSE)

## Copyright

Copyright (C) 2011-2015, Armory Technologies, Inc.
Copyright (C) 2016-2018, goatpig
