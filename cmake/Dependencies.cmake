include(FetchContent)

message(STATUS "Fetching Catch2...")
FetchContent_Declare(
    Catch2
    URL https://github.com/catchorg/Catch2/archive/refs/tags/v3.8.0.tar.gz
    FIND_PACKAGE_ARGS
)
FetchContent_MakeAvailable(Catch2)

message(STATUS "Fetching GTest...")
FetchContent_Declare(
        GTest
        # Using manual commit hash due to https://github.com/google/googletest/issues/4762
        # TODO: fix on next gtest release
        # URL https://github.com/google/googletest/releases/download/v1.17.0/googletest-1.17.0.tar.gz
        URL https://github.com/google/googletest/archive/fa8438ae6b70c57010177de47a9f13d7041a6328.zip
        SYSTEM
)
FetchContent_MakeAvailable(GTest)
