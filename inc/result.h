#ifndef JUAN_RESULT_H
#define JUAN_RESULT_H
#include <expected>
#include <string_view>

namespace juan {
enum struct Status { OK = 0, DB_ERROR, OPEN_LOG_ERROR };

std::string_view status_str(const Status& status) noexcept;
template <typename T> using Result = std::expected<T, Status>;
} // namespace juan
#endif /* ifndef JUAN_RESULT_H */
