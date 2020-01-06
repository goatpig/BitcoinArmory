set(GTEST_COMPILE_DEFINITIONS
    PUBLIC -DUNIT_TESTS
)

if(APPLE)
    list(APPEND GTEST_COMPILE_DEFINITIONS
        PRIVATE -DGTEST_USE_OWN_TR1_TUPLE=1
    )
endif()

file(COPY input_files DESTINATION ${PROJECT_BINARY_DIR})

add_library(gtest
    TestUtils.cpp
    NodeUnitTest.cpp
    gtest-all.cc
)

target_link_libraries(gtest
    ArmoryCLI
)

add_executable(ContainerTests
    ContainerTests.cpp
)

target_link_libraries(ContainerTests
    gtest
)

set_target_properties(ContainerTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

add_executable(BIP151RekeyTest
    BIP151RekeyTest.cpp
)

target_link_libraries(BIP151RekeyTest
    gtest
)

set_target_properties(BIP151RekeyTest PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

add_executable(SupernodeTests
    SupernodeTests.cpp
)

target_link_libraries(SupernodeTests
    gtest
)

set_target_properties(SupernodeTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

add_executable(CppBlockUtilsTests
    CppBlockUtilsTests.cpp
)

target_link_libraries(CppBlockUtilsTests
    gtest
)

set_target_properties(CppBlockUtilsTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

add_executable(WalletTests
    WalletTests.cpp
)

target_link_libraries(WalletTests
    gtest
)

set_target_properties(WalletTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

add_executable(SignerTests
    SignerTests.cpp
)

target_link_libraries(SignerTests
    gtest
)

set_target_properties(SignerTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
