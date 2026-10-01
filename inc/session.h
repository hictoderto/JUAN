#ifndef JUAN_SESSION_H
#define JUAN_SESSION_H

#include "timestamp.h"
#include "types.h"

namespace juan {
	struct Session {
		s64 id{};
		opt<Timestamp> started_at;
		opt<Timestamp> finished_ad;
	};
}
#endif /* ifndef JUAN_SESSION_H */

