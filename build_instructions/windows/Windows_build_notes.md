# 1. Installing pre-requisites
* MSVC (Visual Studio Community): https://visualstudio.microsoft.com/downloads/
* MSYS2: ONLY DOWNLOAD the installer of MSYS2 from https://www.msys2.org/ and FOLLOW the setup instructions in the current document
* Python 3.12
* Git for Windows: https://gitforwindows.org/

# 2. Installing build tools
As you will be exclusively using **MSYS2 MINGW64**, make sure you have opened that and not the UCRT/MSYS/CLANG terminal.
```
pacman -Syu
pacman -S autoconf automake libtoolize mingw-w64-x86_64-gcc mingw-w64-x86_64-libevent mingw-w64-x86_64-make mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja git
```
Next, create a symlink for the cmake binary in order to use the cmake command in MinGW64:

1. Open command prompt and navigate to `<your MSYS2 installation path>\mingw64\bin`
2. Run `mklink make mingw32-make.exe`

# 3. Installing Python dependencies
```
python -m venv .venv
.venv\Scripts\activate.bat
pip install pyside6 qtpy cffi pycapnp setuptools
```

# 4. Building dependencies
Pick a single folder in which you will download all dependencies as well as BitcoinArmory's source. From there, clone and build the dependencies using MinGW64.
> [!NOTE]
> Be sure to use a separate build directory while building dependencies for LWS, Cap'n Proto, BitcoinArmory and c20p1305_cffi. That way you can always remove it for a clean reset if needed:
> ```
> rm -r build 
> mkdir build 
> cd build 
> ```
1. [libbtc](https://github.com/libbtc/libbtc):
   ```
   git clone https://github.com/libbtc/libbtc.git
   cd libbtc
   sh autogen.sh
   ./configure --disable-wallet --disable-tools --disable-net
   make
   ```
2. [libwebsockets](https://github.com/warmcat/libwebsockets):
   ```
   git clone https://github.com/warmcat/libwebsockets.git
   cd libwebsockets
   git checkout v4.3.3
   mkdir build & cd build
   cmake -G Ninja -DLWS_WITH_SSL=OFF ..
   ninja
   ```
3. [LMDB](https://github.com/LMDB/lmdb):
   ```
   git clone https://github.com/LMDB/lmdb.git
   cd libraries/liblmdb
   make
   ```
4. [Cap'n Proto](https://github.com/capnproto/capnproto):
   ```
   git clone https://github.com/capnproto/capnproto.git
   cd capnproto
   git checkout v1.0.2
   mkdir build & cd build
   cmake -G Ninja ..
   ninja
   ```
# 5. Building BitcoinArmory
```
git clone https://github.com/goatpig/BitcoinArmory
cd BitcoinArmory
mkdir build & cd build
cmake -G Ninja ..
ninja
```
> [!WARNING]
> If you have cloned and built the dependencies to a folder that is NOT shared with BitcoinArmory, this will fail! In order to make it work, build BitcoinArmory using `cmake -G Ninja .. -DTHIRD_PARTY_PATH=path/to/dependencies`.

# 6. Building c20p1305_cffi
Follow the instructions under https://github.com/goatpig/c20p1305_cffi.