# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/home/nlaptop/Projects/JUAN/deps/SQLite/sqlite_amalgamation_3530400-src")
  file(MAKE_DIRECTORY "/home/nlaptop/Projects/JUAN/deps/SQLite/sqlite_amalgamation_3530400-src")
endif()
file(MAKE_DIRECTORY
  "/home/nlaptop/Projects/JUAN/deps/SQLite/sqlite_amalgamation_3530400-build"
  "/home/nlaptop/Projects/JUAN/deps/SQLite/sqlite_amalgamation_3530400-subbuild/sqlite_amalgamation_3530400-populate-prefix"
  "/home/nlaptop/Projects/JUAN/deps/SQLite/sqlite_amalgamation_3530400-subbuild/sqlite_amalgamation_3530400-populate-prefix/tmp"
  "/home/nlaptop/Projects/JUAN/deps/SQLite/sqlite_amalgamation_3530400-subbuild/sqlite_amalgamation_3530400-populate-prefix/src/sqlite_amalgamation_3530400-populate-stamp"
  "/home/nlaptop/Projects/JUAN/deps/SQLite/sqlite_amalgamation_3530400-subbuild/sqlite_amalgamation_3530400-populate-prefix/src"
  "/home/nlaptop/Projects/JUAN/deps/SQLite/sqlite_amalgamation_3530400-subbuild/sqlite_amalgamation_3530400-populate-prefix/src/sqlite_amalgamation_3530400-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/nlaptop/Projects/JUAN/deps/SQLite/sqlite_amalgamation_3530400-subbuild/sqlite_amalgamation_3530400-populate-prefix/src/sqlite_amalgamation_3530400-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/nlaptop/Projects/JUAN/deps/SQLite/sqlite_amalgamation_3530400-subbuild/sqlite_amalgamation_3530400-populate-prefix/src/sqlite_amalgamation_3530400-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
