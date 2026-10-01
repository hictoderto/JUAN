#ifndef JUAN_DB_SESSION_H
#define JUAN_DB_SESSION_H
#include "session.h"
#include "database.h"

namespace juan {
	struct DBSession {
		Session session;
		DB db;
	};
}
#endif /* ifndef JUAN_DB_SESSION_H */
