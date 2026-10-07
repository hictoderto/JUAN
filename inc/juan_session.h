#ifndef JUAN_DB_SESSION_H
#define JUAN_DB_SESSION_H
#include "database.h"
#include "logger.h"
#include "session.h"

#include "passkey.h"
#include "types.h"
#include <memory>

namespace juan {
class JuanSession {
	shared_ptr<Logger> logger;
	Session session;
	DB db;

  public:
	JuanSession(shared_ptr<Logger>&& logger, Session&& session, DB&& db,
	            passkey<JuanSession>) noexcept;
	static Result<JuanSession> init(shared_ptr<Logger> logger,
	                                DB&& db) noexcept;
	[[nodiscard]] const Session& get_session() const noexcept;
};
} // namespace juan
#endif /* ifndef JUAN_DB_SESSION_H */
