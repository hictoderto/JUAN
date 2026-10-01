#include <timestamp.h>

namespace juan {
Timestamp::Timestamp(s64 millis) noexcept : millis{millis} {
}
Timestamp Timestamp::now() noexcept {
	return Timestamp{std::chrono::duration_cast<timestamp_duration>(
	                     std::chrono::system_clock::now().time_since_epoch())
	                     .count()};
}
} // namespace juan
