# 1. Build tools

You will need to prepare your build system by installing the following software:
* build-essentials (debian based distros) or base-devel (arch based distros)
* python3 and cffi (highly recommend you make use of venv and pip)
* cmake
* autotools & libtool

# 2. System Dependencies

Armory expects the following libraries to be installed in the system:
* capnproto
* lmdb

# 3. Building Dependencies

The following dependencies you have to build from source.
> [!NOTE]
> Armory looks for built dependencies in its parent folder by default:
> ```
>    parent
>        |- libbtc
>        |- libwebsocekts
>        |- BitcoinArmory
> ```
> It is recommended you follow this structure to avoid difficulties. If you choose to clone the dependencies elsewhere, you have to give the custom paths to the configure script, as follows (use absolute paths):
> ./configure --with-own-libbtc=*/path/to/libbtc* --with-own-lws=*/path/to/lws*

1. [libbtc] (https://github.com/libbtc/libbtc):
    ```
    git clone https://github.com/libbtc/libbtc
    cd libbtc
    sh autogen.sh
    CFLAGS="-fPIC -g" ./configure --disable-wallet --disable-tools --disable-net
    make
    ```

2. [libwebsockets] (https://github.com/warmcat/libwebsockets):
    ```
    git clone https://github.com/warmcat/libwebsockets
    cd libwebsockets
    mkdir build & cd build
    cmake -DLWS_WITH_SSL=OFF ..
    make
    ```

> [!NOTE]
> you can install system LWS but it comes build with openssl TLS support, at which point you will have to battle the Makefile to feed it libssl and libcrypto (openssl dependencies). Armory does not use openssl at all, so I recommend users build LWS from source in order disable TLS, which avoids grandfathering in openssl.

# 4. Building Armory

3. Setup and build Armory:
    ```
    sh autogen.sh
    mkdir build & cd build
    ../configure
    make
    ```

4. [c20p1305_cffi] (https://github.com/goatpig/c20p1305_cffi.git)
    To start ArmoryQt.py, you will need to build the c20p1305_cffi python package.
    Clone the repo and follow the build instructions: https://github.com/goatpig/c20p1305_cffi.git