cmake_minimum_required(VERSION 3.16)

project(EP LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(EP main.cpp)

include(GNUInstallDirs)
install(TARGETS EP
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

include(FetchContent)

FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
)

set(CMAKE_POLICY_DEFAULT_CMP<0077> NEW)

include_directories(${gtest_SOURCE_DIR}/include ${gtest_SOURCE_DIR})

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

set(gtest_disable_pthreads on)

FetchContent_MakeAvailable(googletest)

enable_testing()

target_link_libraries(EP PRIVATE gtest_main)

include(GoogleTest)

gtest_discover_tests(EP)
