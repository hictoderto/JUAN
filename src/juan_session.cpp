#include "database.h"
#include "logger.h"
#include "passkey.h"
#include "result.h"
#include "session.h"
#include "timestamp.h"
#include <expected>
#include <juan_session.h>
#include <memory>

namespace juan {
constexpr str_vw create_session_table =
    "CREATE TABLE IF NOT EXISTS session (id INTEGER PRIMARY KEY, started_at "
    "INTEGER, ended_at INTEGER) STRICT";
constexpr str_vw insert_session =
    "INSERT INTO session (id,started_at) VALUES (NULL,:start) RETURNING id";

JuanSession::JuanSession(shared_ptr<Logger>&& logger, Session&& session,
                         DB&& db, passkey<JuanSession>) noexcept
    : logger{std::move(logger)}, session{std::move(session)},
      db{std::move(db)} {
}

Result<JuanSession> JuanSession::init(shared_ptr<Logger> logger,
                                      DB&& db) noexcept {
	using std::unexpected;
	auto ensure_session_res = db.prepare_stmt(create_session_table, {});
	if (!ensure_session_res) {
		logger->error("Failed to prepare session table stmt");
		return unexpected{Status::DB_STMT_PREP_ERROR};
	}
	if (ensure_session_res->step() != SQL_DONE) {
		logger->error("Failed to create session table");
		return unexpected{Status::DB_ERROR};
	}

	auto insert_session_res = db.prepare_stmt(insert_session, {});
	if (!insert_session_res) {
		logger->error("Failed to prepare session insert stmt");
		return unexpected{Status::DB_STMT_PREP_ERROR};
	}
	auto& insert_stmt = *insert_session_res;
	Session session;
	session.started_at = Timestamp::now();
	int start_idx      = insert_stmt.param_idx_by_name(":start");
	if (!start_idx) {
		logger->error("Error binding session stmt parameter");
		return unexpected{Status::DB_ERROR};
	}
	if (insert_stmt.bind(start_idx, session.started_at->as_s64()) != SQL_OK) {
		logger->error("Error binding session stmt parameter");
		return unexpected{Status::DB_ERROR};
	}
	if (insert_stmt.step() != SQL_ROW) {
		logger->error("Error getting session id");
		return unexpected{Status::DB_ERROR};
	}
	session.id = insert_stmt.column_as_s64(0);
	if (insert_stmt.step() != SQL_DONE) {
		logger->error("Error getting session id");
		return unexpected{Status::DB_ERROR};
	}

	return Result<JuanSession>{std::in_place, std::move(logger),
	                           std::move(session), std::move(db),
	                           passkey<JuanSession>{}};
}

const Session& JuanSession::get_session() const noexcept {
	return session;
}
} // namespace juan
