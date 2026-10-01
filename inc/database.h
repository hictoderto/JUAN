#ifndef JUAN_DATABASE_H
#define JUAN_DATABASE_H
#include <filesystem>

#include "logger.h"
#include "passkey.h"
#include "result.h"
#include "types.h"

struct sqlite3;

namespace juan {
using db_conn    = sqlite3*;
using sqlite_res = int;
class DB {

	sqlite_res cleanup();

  public:
	db_conn db;
	DB(const DB&) = delete;
	DB(DB&&) noexcept;
	DB& operator=(const DB&) = delete;
	DB& operator=(DB&&) noexcept;
	DB(db_conn, passkey<DB>) noexcept;
	~DB();

	static Result<DB> connect(const path& db_file, Logger& logger) noexcept;
};
using path = std::filesystem::path;
} // namespace juan

#endif /* ifndef JUAN_DATABASE_H */
