cmake_minimum_required(VERSION 3.15)
project(juan_tests)

find_package(GTest REQUIRED)

enable_testing()

add_executable(run_tests
    test_job.cpp
    #Agregar aqui archivos a probar
)

target_link_libraries(run_tests PRIVATE GTest::gtest_main)
target_include_directories(run_tests PRIVATE ../inc)

include(GoogleTest)
gtest_discover_tests(run_tests)