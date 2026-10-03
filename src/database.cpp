#include "passkey.h"
#include <expected>
#include <sqlite3.h>

#include <database.h>
#include <logger.h>
#include <result.h>
#include <string_view>
#include <utility>

namespace juan {

void Statement::cleanup() noexcept {
	sqlite3_finalize(stmt);
}

Statement::Statement(sql_stmt stmt, passkey<Statement>) noexcept : stmt{stmt} {
}

Statement::Statement(Statement&& rhs) noexcept
    : stmt{std::exchange(rhs.stmt, nullptr)} {
}

Statement& Statement::operator=(Statement&& rhs) noexcept {
	if (stmt) {
		cleanup();
	}
	stmt = std::exchange(rhs.stmt, nullptr);
	return *this;
}

Statement::~Statement() {
	cleanup();
}

Result<Statement> Statement::prepare(db_conn db, str_vw sql,
                                     stmt_prep_flags flags, Logger& logger,
                                     passkey<DB>) noexcept {
	using std::unexpected;
	sql_stmt stmt{};
	sqlite_res res =
	    sqlite3_prepare_v3(db, sql.cbegin(), static_cast<int>(sql.length()),
	                       flags, &stmt, nullptr);
	if (res != SQLITE_OK) {
		return unexpected{Status::DB_STMT_PREP_ERROR};

		logger.error("Failed to prepare statement with error \"{}\"",
		             sqlite3_errmsg(db));
		if (stmt) {
			sqlite3_finalize(stmt);
		}
		return unexpected{Status::DB_STMT_PREP_ERROR};
	}
	return Result<Statement>{std::in_place, stmt, passkey<Statement>{}};
}

sqlite_res Statement::step() noexcept {
	return sqlite3_step(stmt);
}

sqlite_res Statement::reset() noexcept {
	return sqlite3_reset(stmt);
}

int Statement::param_idx_by_name(const char* name) noexcept {
	return sqlite3_bind_parameter_index(stmt, name);
}

sqlite_res DB::cleanup() noexcept {
	sqlite_res res = sqlite3_close(db);
	if (res != SQLITE_OK) {
		logger->warn("Database cleanup failed with error \"{}\"",
		             sqlite3_errmsg(db));
	}
	return res;
}

sqlite_res Statement::bind(int idx, s64 val) noexcept {
	return sqlite3_bind_int64(stmt, idx, val);
}

s64 Statement::column_as_s64(int idx) noexcept {
	return sqlite3_column_int64(stmt, idx);
}

DB::DB(DB&& rhs) noexcept
    : db{std::exchange(rhs.db, nullptr)}, logger{std::move(rhs.logger)} {
}
DB& DB::operator=(DB&& rhs) noexcept {
	if (db) {
		cleanup();
	}
	db     = std::exchange(rhs.db, nullptr);
	logger = std::move(rhs.logger);
	return *this;
}

DB::DB(db_conn db, shared_ptr<Logger>&& logger, passkey<DB>) noexcept
    : db{db}, logger(std::move(logger)) {
}

DB::~DB() {
	cleanup();
}

Result<DB> DB::connect(const path& db_file,
                       shared_ptr<Logger> logger) noexcept {
	using std::unexpected;
	db_conn db{};
	auto res = sqlite3_open_v2(db_file.c_str(), &db,
	                           SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
	                               SQLITE_OPEN_EXRESCODE,
	                           nullptr);
	if (res != SQLITE_OK) {
		logger->error("Failed to open datababase connection with error \"{}\"",
		              db ? sqlite3_errmsg(db) : sqlite3_errstr(res));
		if (db) {
			sqlite3_close(db);
		}
		return unexpected{Status::DB_ERROR};
	}
	return Result<DB>{std::in_place, db, std::move(logger), passkey<DB>{}};
}

Result<Statement> DB::prepare_stmt(str_vw sql, stmt_prep_flags flags) noexcept {
	return Statement::prepare(db, sql, flags, *logger, passkey<DB>{});
}

static_assert(SQL_OK == SQLITE_OK);
static_assert(SQL_ROW == SQLITE_ROW);
static_assert(SQL_DONE == SQLITE_DONE);

} // namespace juan
