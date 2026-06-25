import os
from cx_Freeze import setup

PYTHON_INSTALL_DIR = os.path.dirname(os.path.dirname(os.__file__))

build_exe_options = {
    "zip_include_packages" : [
        "cffi"
    ],
    "packages" : [
        "asyncio",
        "capnp",
    ],
    "include_files" : [
        ("cppForSwig/capnp", "cppForSwig/capnp"),
        ("build/CppBridge.exe", "build/CppBridge.exe")
        ("build/ArmoryDB.exe", "build/ArmoryDB.exe")
    ],
    "includes" : [
        "_cffi_backend"
    ]
}

setup(
    name="ArmoryQt",
    version="0.97",
    description="BitcoinArmory Qt Client",
    options={"build_exe" : build_exe_options},
    executables=[{"script" : "ArmoryQt.py", "base" : "console"}]
)

