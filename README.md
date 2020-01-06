# Armory

**Created by Alan Reiner on 13 July, 2011**

**Forked by goatpig in February 2016**

[Armory](https://github.com/goatpig/BitcoinArmory) is a full-featured Bitcoin client, offering a dozen innovative features not found in any other client software! Manage multiple wallets (deterministic and watching-only), print paper backups that work forever, import or sweep private keys, and keep your savings in a computer that never touches the internet, while still being able to manage incoming payments, and create outgoing payments with the help of a USB key.

Multi-signature transactions are accommodated under-the-hood about 80%, and will be completed and integrated into the UI soon.

**Armory has no independent networking components built in.** Instead, it relies on on the Satoshi client to securely connect to peers, validate blockchain data, and broadcast transactions for us.  Although it was initially planned to cut the umbilical cord to the Satoshi client and implement independent networking, it has turned out to be an inconvenience worth having. Reimplementing all the networking code would be fraught with bugs, security holes, and possible blockchain forking.  The reliance on Bitcoin Core right now is actually making Armory more secure!

## Donations

*Will post an address eventually for donations*

## Building Armory From Source

### Dependencies

* GNU Compiler Collection
 Linux:   Install package `g++`

* SWIG
 Linux:   Install package `swig`
 Windows: [Download](http://www.swig.org/download.html)
 MSVS: Copy swigwin-2.x directory next to cryptopp as `swigwin`

* Python 2.6/2.7
 Linux:   Install package `python-dev`
 Windows: [Download](https://www.python.org/getit/)

* PyQt 4 (for Python 2.X)
 Linux:   Install packages `libqtcore4`, `libqt4-dev`, `python-qt4`, and `pyqt4-dev-tools`
 Windows: [Download](https://riverbankcomputing.com/software/pyqt/download)

* py2exe
 (OPTIONAL - if you want to make a standalone executable in Windows)
 Windows: [Download](http://www.py2exe.org/)

* libwebsockets
 Linux:   install latest version from source
 Windows: handled automatically with vcpkg, see below

* Google Protocol Buffers (protobuf)
 Linux:   Install the `protobuf` package.
 Windows: handled automatically with vcpkg, see below

### CMake options

| **Option**                  | **Description**                                                                          | **Default**                    |
|-----------------------------|------------------------------------------------------------------------------------------|--------------------------------|
| WITH_HOST_CPU_FEATURES      | use -march=native and supported cpu feature flags, gcc only                              | ON                             |
| WITH_CRYPTOPP               | use Crypto++ library for cryptography functions                                          | OFF                            |
| WITH_CLIENT                 | build Python client                                                                      | AUTO                           |
| WITH_GUI                    | build GUI support using Qt4 for the Python client                                        | AUTO                           |
| ENABLE_TESTS                | build the test binaries                                                                  | OFF                            |
| LIBBTC_WITH_WALLET          | enable libbtc wallet                                                                     | OFF                            |
| LIBBTC_WITH_TESTS           | enable libbtc tests                                                                      | OFF                            |
| LIBBTC_WITH_TOOLS           | build libbtc tools binaries                                                              | OFF                            |
| LIBBTC_RANDOM_DEVICE        | device to use for random numbers                                                         | /dev/urandom                   |
| SECP256K1_ENABLE_ASM        | enable asm routines in the secp256k1 library                                             | ON                             |
| SECP256K1_USE_LIBGMP        | use libgmp for numeric routines in the secp256k1 library                                 | AUTO                           |
| SECP256K1_MODULE_ECDH       | enable the ecdh module in the secp256k1 library                                          | OFF                            |
| SECP256K1_MODULE_SCHNORR    | enable the schnorr module in the secp256k1 library                                       | OFF                            |
| SECP256K1_ECMULT_STATIC_PRECOMPUTATION | use a statically generated ecmult table for the secp256k1 library             | OFF                            |
| SECP256K1_ENDOMORPHISM      | use endomorphism optiomization for the secp256k1 library                                 | OFF                            |
| SECP256K1_WITH_FIELD        | field for the secp256k1 library, can be '32bit', '64bit' or 'AUTO'                       | AUTO                           |
| SECP256K1_WITH_SCALAR       | scalar for the secp256k1 library, can be '32bit', '64bit' or 'AUTO'                      | AUTO                           |
| VCPKG_TARGET_TRIPLET        | see below                                                                                | not set                        |

### CMake Windows/vcpkg Build Type

When building on windows, set the cmake variable `VCPKG_TARGET_TRIPLET` to
`x64-windows` or `x86-windows` depending on whether the build is for 64 bit or
32 bit. You must be in the appropriate Visual Studio environment as well.

All vcpkg supported triplets should work, and this variable can be used to
activate vcpkg support on other platforms.

When building with the Visual Studio IDE, the build products will be located
under `C:\Users\<your-user>\CMakeBuilds`.

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
