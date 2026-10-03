#ifndef JUAN_TIMESTAMP_H
#define JUAN_TIMESTAMP_H
#include <chrono>
#include <ratio>

#include "types.h"
namespace juan {
using timestamp_duration = std::chrono::duration<s64, std::milli>;
class Timestamp {
	s64 millis;

  public:
	explicit Timestamp(s64 millis) noexcept;

	[[nodiscard]] s64 as_s64() const noexcept {
		return millis;
	}

	static Timestamp now() noexcept;
};
} // namespace juan

#endif /* ifndef JUAN_TIMESTAMP_H */
