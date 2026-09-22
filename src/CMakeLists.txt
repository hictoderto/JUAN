cmake_minimum_required(VERSION 3.15)
project(JUAN CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT DEFINED BUILD_TESTS)
	if (CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
		set(BUILD_TESTS TRUE)
	endif()
endif()

if(BUILD_TESTS)

include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/releases/download/v1.16.0/googletest-1.16.0.tar.gz
	DOWNLOAD_EXTRACT_TIMESTAMP true
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()
include(GoogleTest)

add_executable(JuanTests tests/juan_tests.cpp)
target_link_libraries(JuanTests GTest::gtest_main)
target_compile_options(JuanTests PUBLIC -Wall -Wextra -pedantic)

endif()

add_executable(juan main.cpp)

