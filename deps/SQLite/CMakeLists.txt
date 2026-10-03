if (NOT SQLITE_DIR)
include(FetchContent)
if (NOT sqlite_amalgamation_3530400_POPULATED)
	FetchContent_Populate(sqlite_amalgamation_3530400
	URL https://sqlite.org/2026/sqlite-amalgamation-3530400.zip URL_HASH SHA3_256=628a44cfe82c66aed1ccbbe85a562d2e33ebe64b3288981ed76285612227934e 
	DOWNLOAD_EXTRACT_TIMESTAMP true
	)
endif()
	set(SQLITE_DIR ${sqlite_amalgamation_3530400_SOURCE_DIR} CACHE PATH "The path to que SQLite amalgamation source")
endif()
add_library(sqlite3 STATIC ${SQLITE_DIR}/sqlite3.c)
target_include_directories(sqlite3 PUBLIC ${SQLITE_DIR})

