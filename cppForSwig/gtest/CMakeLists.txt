set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(TEST_SOURCES
    TestUtils.cpp
    MockedNode.cpp
)

set(GTEST_LIBS
    gtest
)

################################################################################
## BIP151RekeyTest
add_executable(BIP151RekeyTest
    BIP151RekeyTest.cpp
    ${TEST_SOURCES}
)
set_target_properties(BIP151RekeyTest
    PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}
)
target_include_directories(BIP151RekeyTest
    ${LIBARMORYCOMMON_INCLUDE_DIRECTORIES}
)
target_link_libraries(BIP151RekeyTest
    ArmoryCommon
    ${LIBWEBSOCKETS_LIB}
    ${OS_SPECIFIC_LIBS}
    ${GTEST_LIBS}
)

################################################################################
## ContainerTests
#add_executable(ContainerTests
#    ContainerTests.cpp
#    ${TEST_SOURCES}
#)
#set_target_properties(ContainerTests
#    PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}
#)
#target_include_directories(ContainerTests
#    ${LIBARMORYCOMMON_INCLUDE_DIRECTORIES}
#)
#target_link_libraries(ContainerTests
#    ${GTEST_LIB}
#    ArmoryCommon
#    ${OS_SPECIFIC_LIBS}
#)

################################################################################
## CppBlockUtilsTests
add_executable(CppBlockUtilsTests
    CppBlockUtilsTests.cpp
    ${TEST_SOURCES}
)
set_target_properties(CppBlockUtilsTests
    PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}
)
target_include_directories(CppBlockUtilsTests
    ${LIBARMORYCOMMON_INCLUDE_DIRECTORIES}
)
target_link_libraries(CppBlockUtilsTests
    ArmoryCommon
    ${LIBWEBSOCKETS_LIB}
    ${OS_SPECIFIC_LIBS}
    ${GTEST_LIBS}
)

################################################################################
## DB1kIterTest
# Seems to be outdated - re-enable when fixed
#add_executable(DB1kIterTest
#    DB1kIterTest.cpp
#    ${TEST_SOURCES}
#)
#set_target_properties(DB1kIterTest
#    PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}
#)
#target_include_directories(DB1kIterTest
#    ${LIBARMORYCOMMON_INCLUDE_DIRECTORIES}
#)
#target_link_libraries(DB1kIterTest
#    ${GTEST_LIB}
#    ArmoryCommon
#)

################################################################################
## SignerTests
add_executable(SignerTests
    SignerTests.cpp
    ${TEST_SOURCES}
)
set_target_properties(SignerTests
    PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}
)
target_include_directories(SignerTests
    ${LIBARMORYCOMMON_INCLUDE_DIRECTORIES}
)
target_link_libraries(SignerTests
    ArmoryCommon
    ${LIBWEBSOCKETS_LIB}
    ${OS_SPECIFIC_LIBS}
    ${GTEST_LIBS}
)

################################################################################
## SupernodeTests
add_executable(SupernodeTests
    SupernodeTests.cpp
    ${TEST_SOURCES}
)
set_target_properties(SupernodeTests
    PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}
)
target_include_directories(SupernodeTests
    ${LIBARMORYCOMMON_INCLUDE_DIRECTORIES}
)
target_link_libraries(SupernodeTests
    ArmoryCommon
    ${LIBWEBSOCKETS_LIB}
    ${OS_SPECIFIC_LIBS}
    ${GTEST_LIBS}
)

################################################################################
## UtilsTests
add_executable(UtilsTests
    UtilsTests.cpp
    ${TEST_SOURCES}
)
set_target_properties(UtilsTests
    PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}
)
target_include_directories(UtilsTests
    ${LIBARMORYCOMMON_INCLUDE_DIRECTORIES}
)
target_link_libraries(UtilsTests
    ArmoryCommon
    ${LIBWEBSOCKETS_LIB}
    ${OS_SPECIFIC_LIBS}
    ${GTEST_LIBS}
)

################################################################################
## WalletTests
add_executable(WalletTests
    WalletTests.cpp
    ${TEST_SOURCES}
    ${LIBBTC_INCLUDE_DIR}/src/trezor-crypto/bip39.c
    ${LIBBTC_INCLUDE_DIR}/src/trezor-crypto/pbkdf2.c
)
set_target_properties(WalletTests
    PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}
)
target_include_directories(WalletTests
    ${LIBARMORYCOMMON_INCLUDE_DIRECTORIES}
)
target_link_libraries(WalletTests
    ArmoryCommon
    ${LIBWEBSOCKETS_LIB}
    ${OS_SPECIFIC_LIBS}
    ${GTEST_LIBS}
)

################################################################################
## ZeroConfTests
add_executable(ZeroConfTests
    ZeroConfTests.cpp
    ${TEST_SOURCES}
)
set_target_properties(ZeroConfTests
    PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}
)
target_include_directories(ZeroConfTests
    ${LIBARMORYCOMMON_INCLUDE_DIRECTORIES}
)
target_link_libraries(ZeroConfTests
    ArmoryCommon
    ${LIBWEBSOCKETS_LIB}
    ${OS_SPECIFIC_LIBS}
    ${GTEST_LIBS}
)

################################################################################
## BridgeTests
add_executable(BridgeTests
    BridgeTests.cpp
    ${TEST_SOURCES}
)
set_target_properties(BridgeTests
    PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}
)
target_include_directories(BridgeTests
    ${LIBARMORYCOMMON_INCLUDE_DIRECTORIES}
)
target_link_libraries(BridgeTests
    ArmoryCommon
    BridgeLib
    ${LIBWEBSOCKETS_LIB}
    ${OS_SPECIFIC_LIBS}
    ${GTEST_LIBS}
)
