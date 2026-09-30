#include "passkey.h"
#include "result.h"
#include "types.h"

#include <expected>
#include <filesystem>
#include <ios>
#include <logger.h>
#include <system_error>
#include <utility>

namespace juan {
Logger::Logger(ofs&& output_file, LogLevel level, passkey<Logger>) noexcept
    : output_file(std::move(output_file)), level{level} {
}

Logger::Logger(Logger&&) noexcept            = default;
Logger& Logger::operator=(Logger&&) noexcept = default;
Logger::~Logger()                            = default;

Result<Logger> Logger::open(const path& file, LogLevel level) noexcept {
	using namespace std::filesystem;
	using std::error_code;
	using std::unexpected;
	auto old_file = path{file} += ".old";
	error_code ec{};
	rename(file, old_file, ec);
	if (ec && ec != std::errc::no_such_file_or_directory) {
		return unexpected{Status::OPEN_LOG_ERROR};
	}
	try {
		using std::ios_base;
		ofs stream(file, ios_base::out | ios_base::noreplace);
		if (!stream)
			return unexpected{Status::OPEN_LOG_ERROR};

		return Result<Logger>{std::in_place, std::move(stream), level,
		                      passkey<Logger>{}};
	} catch (...) {
		return unexpected{Status::OPEN_LOG_ERROR};
	}
}
} // namespace juan
