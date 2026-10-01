# Building Armory on macOS

# 1. Build tools

You will need:
* the Xcode Command Line Tools: `xcode-select --install`
* [Homebrew](https://brew.sh)
* autotools, libtool, pkg-config and cmake: `brew install autoconf automake libtool pkgconf cmake`
* python3 with cffi, and setuptools (cffi needs it to compile modules on Python 3.12 and later, and a new venv no longer includes it). Use a venv, and use the same one for building c20p1305 (step 5) and running ArmoryQt (step 6):
    ```
    python3 -m venv armory-venv
    source armory-venv/bin/activate
    pip install cffi setuptools
    ```

# 2. System Dependencies

Armory expects the following libraries to be installed in the system:
```
brew install capnp lmdb
```

# 3. Building Dependencies

The following dependencies you have to build from source.
> [!NOTE]
> Clone them next to BitcoinArmory, in the same parent folder (the c20p1305 build in step 5 expects this layout):
> ```
>    parent
>        |- libbtc
>        |- libwebsockets
>        |- BitcoinArmory
> ```
> Step 4 passes their absolute paths to the configure script.

1. [libbtc](https://github.com/libbtc/libbtc):
    ```
    git clone https://github.com/libbtc/libbtc
    cd libbtc
    sh autogen.sh
    CFLAGS="-fPIC -g" ./configure --disable-wallet --disable-tools --disable-net --disable-shared
    make
    ```

2. [libwebsockets](https://github.com/warmcat/libwebsockets):
    ```
    git clone https://github.com/warmcat/libwebsockets
    cd libwebsockets
    git checkout v4.5.8
    mkdir build
    cmake -DLWS_WITH_SSL=OFF -DLWS_WITHOUT_TESTAPPS=ON -B build
    cd build
    make
    ```

# 4. Building Armory

Pass the configure script the absolute paths of the libbtc and libwebsockets builds from step 3: without `--with-own-libbtc` it stops, and without `--with-own-lws` it would pick Homebrew's libwebsockets package, if installed, instead of the v4.5.8 build. Homebrew also installs headers and libraries under `$(brew --prefix)` (`/opt/homebrew` on Apple Silicon), which the compiler does not search by default, so pass that too:
```
sh autogen.sh
mkdir build && cd build
../configure --with-own-libbtc="$(cd ../.. && pwd)/libbtc" --with-own-lws="$(cd ../.. && pwd)/libwebsockets/build" CPPFLAGS="-I$(brew --prefix)/include" LDFLAGS="-L$(brew --prefix)/lib"
make
```

### Unit tests

Add `--enable-tests` to the configure line above (this needs `brew install googletest`). The test binaries end up in `build/cppForSwig/gtest`.

> [!NOTE]
> macOS limits each process to 256 open files by default. Some tests run the database in DB_FULL mode (in CppBlockUtilsTests and ZeroConfTests, at least) and need more, otherwise the BDM fails with `Too many open files`. Raise the limit in the terminal you run them from:
> ```
> ulimit -n 4096
> ```

# 5. Building c20p1305
> [!NOTE]
> you do not need to rebuild this package every time you build Armory, you only need to make sure the resulting .so is in your armoryengine folder

To start ArmoryQt.py, you will need to build the [c20p1305_cffi](https://github.com/goatpig/c20p1305_cffi) python package. It uses the libbtc built in step 3, and expects to be cloned next to it (in the same parent folder). Run it from the venv: the resulting .so only loads in the Python version that built it.
```
git clone https://github.com/goatpig/c20p1305_cffi
cd c20p1305_cffi
mkdir build && cd build
cmake ..
make
cd ../cffi
python c20p1305_cffi.py
cp c20p1305.cpython-*-darwin.so ../../BitcoinArmory/armoryengine/
```

# 6. Running ArmoryQt

ArmoryQt uses Qt through QtPy. On macOS, install PySide6 as the Qt binding, along with the other Python packages it imports:
```
pip install PySide6 QtPy psutil pycapnp
```
Then, from the BitcoinArmory folder (ArmoryQt looks for `build/CppBridge` relative to the current folder):
```
python ArmoryQt.py
```

> [!NOTE]
> ArmoryDB only goes online once the node reports a `verificationprogress` of 1. On an idle regtest node that value drops as time passes without new blocks, so mine a block right before starting Armory.
