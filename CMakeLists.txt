
cmake_minimum_required(VERSION 3.15)
project(JUAN C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ==========================================
# DEPENDENCIAS DEL PROYECTO
# ==========================================

add_subdirectory(deps)

if(NOT DEFINED BUILD_TESTS)
    if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
        set(BUILD_TESTS TRUE)
    endif()
endif()

# ==========================================
# GNU READLINE
# ==========================================

include(ExternalProject)

set(READLINE_INSTALL_DIR "${CMAKE_BINARY_DIR}/readline")

ExternalProject_Add(readline_external
    URL https://ftp.gnu.org/gnu/readline/readline-8.3.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE

    PREFIX "${CMAKE_BINARY_DIR}/readline-build"

    CONFIGURE_COMMAND
        <SOURCE_DIR>/configure
        --prefix=${READLINE_INSTALL_DIR}
        --enable-static
        --disable-shared

    BUILD_IN_SOURCE TRUE

    BUILD_COMMAND make all
    INSTALL_COMMAND make install

    BUILD_BYPRODUCTS
        "${READLINE_INSTALL_DIR}/lib/libreadline.a"
        "${READLINE_INSTALL_DIR}/lib/libhistory.a"
)

add_library(readline STATIC IMPORTED GLOBAL)

set_target_properties(readline PROPERTIES
    IMPORTED_LOCATION "${READLINE_INSTALL_DIR}/lib/libreadline.a"
)

add_dependencies(readline readline_external)

# ==========================================
# EJECUTABLE PRINCIPAL
# ==========================================

add_executable(juan
    src/main.cpp
    src/database.cpp
    src/result.cpp
    src/logger.cpp
    src/timestamp.cpp
    src/juan_session.cpp
)

add_dependencies(juan readline_external)

target_compile_options(juan PRIVATE
    -Wall
    -Wextra
    -pedantic
)

target_include_directories(juan PRIVATE
    inc
    "${READLINE_INSTALL_DIR}/include"
)

target_link_libraries(juan PRIVATE
    sqlite3
    readline
    "${READLINE_INSTALL_DIR}/lib/libhistory.a"
    ncurses
)

# ==========================================
# PRUEBAS
# ==========================================

if(BUILD_TESTS)

    include(FetchContent)

    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/releases/download/v1.16.0/googletest-1.16.0.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )

    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    enable_testing()
    include(GoogleTest)

    add_executable(JuanTests
        src/tests/test_juan.cpp
        src/timestamp.cpp
    )

    target_link_libraries(JuanTests PRIVATE
        GTest::gtest_main
    )

    target_compile_options(JuanTests PRIVATE
        -Wall
        -Wextra
        -pedantic
    )

    target_include_directories(JuanTests PRIVATE
        inc
    )

    gtest_discover_tests(JuanTests)

endif()
