#ifndef JUAN_TYPES_H
#define JUAN_TYPES_H

#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include <filesystem>

namespace juan {
using s8        = std::int8_t;
using u8        = std::uint8_t;
using s64       = std::int64_t;
using timestamp = s64;
using str       = std::string;
template <typename T, typename A = std::allocator<T>>
using vec                       = std::vector<T, A>;
template <typename T> using opt = std::optional<T>;
using path = std::filesystem::path;

} // namespace juan
#endif /* ifndef JUAN_TYPES_H */
