#ifndef JUAN_DATABASE_H
#define JUAN_DATABASE_H
#include <filesystem>

#include "logger.h"
#include "passkey.h"
#include "result.h"
#include "types.h"

struct sqlite3;
struct sqlite3_stmt;

namespace juan {
using db_conn         = sqlite3*;
using sql_stmt        = sqlite3_stmt*;
using sqlite_res      = int;
using stmt_prep_flags = unsigned int;

constexpr int SQL_OK   = 0;
constexpr int SQL_ROW  = 100;
constexpr int SQL_DONE = 101;

class DB;
class Statement {

	void cleanup() noexcept;

  public:
	sql_stmt stmt;
	Statement(const Statement&)            = delete;
	Statement& operator=(const Statement&) = default;
	Statement(Statement&&) noexcept;
	Statement& operator=(Statement&&) noexcept;
	Statement(sql_stmt stmt, passkey<Statement>) noexcept;

	static Result<Statement> prepare(db_conn db, str_vw sql,
	                                 stmt_prep_flags flags, Logger& logger,
	                                 passkey<DB>) noexcept;
	sqlite_res step() noexcept;
	sqlite_res reset() noexcept;
	int param_idx_by_name(const char* name) noexcept;
	sqlite_res bind(int idx, s64 val) noexcept;
	s64 column_as_s64(int idx) noexcept;

	~Statement();
};

class DB {
  public:
		//TODO: make private
	db_conn db;

  private:
	shared_ptr<Logger> logger;

	sqlite_res cleanup() noexcept;

  public:
	DB(const DB&) = delete;
	DB(DB&&) noexcept;
	DB& operator=(const DB&) = delete;
	DB& operator=(DB&&) noexcept;
	DB(db_conn, shared_ptr<Logger>&&, passkey<DB>) noexcept;
	~DB();

	static Result<DB> connect(const path& db_file,
	                          shared_ptr<Logger> logger) noexcept;
	Result<Statement> prepare_stmt(str_vw sql, stmt_prep_flags flags) noexcept;
};
} // namespace juan

#endif /* ifndef JUAN_DATABASE_H */
