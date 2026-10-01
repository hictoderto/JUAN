#ifndef JUAN_RESULT_H
#define JUAN_RESULT_H
#include "types.h"
#include <expected>
#include <optional>
#include <string_view>

namespace juan {
enum struct Status { OK = 0, DB_ERROR, OPEN_LOG_ERROR };

std::string_view status_str(const Status& status) noexcept;
template <typename T> using Result = std::expected<T, Status>;

template <typename T> constexpr opt<T> to_opt(Result<T>&& res) {
	if (res) {
		return opt<T>(*std::move(res));
	} else {
		return std::nullopt;
	}
}

template <typename T> constexpr opt<T> to_opt(const Result<T>& res) {
	if (res) {
		return opt<T>(*res);
	} else {
		return std::nullopt;
	}
}

} // namespace juan
#endif /* ifndef JUAN_RESULT_H */
