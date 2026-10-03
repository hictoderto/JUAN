#include <result.h>

namespace juan {

std::string_view status_str(const Status& status) noexcept {
	using namespace std::literals;
	switch (status) {
	case Status::OK:
		return "OK"sv;
	case Status::DB_ERROR:
		return "database error"sv;
	case Status::OPEN_LOG_ERROR:
		return "error opening log"sv;
	case Status::DB_STMT_PREP_ERROR:
		return "error preparing stmt"sv;
	}
	return "invalid_status"sv;
}
} // namespace juan
