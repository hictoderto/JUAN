#ifndef JUAN_LOGGER_H
#define JUAN_LOGGER_H

#include "passkey.h"
#include <format>
#include <fstream>
#include <iterator>
#include <result.h>
#include <types.h>

namespace juan {
using ofs = std::ofstream;

enum struct LogLevel : u8 {
	ERROR   = 0,
	WARNING = 1,
	INFO    = 2,
	DEBUG   = 3,
};
#ifndef JUAN_MAX_LOG_LEVEL
#define JUAN_MAX_LOG_LEVEL INFO
#endif

constexpr LogLevel max_log_level   = LogLevel::JUAN_MAX_LOG_LEVEL;
constexpr LogLevel max_flush_level = LogLevel::ERROR;

constexpr std::string_view log_level_str(LogLevel level) noexcept {
	using namespace std::literals;
	switch (level) {
	case LogLevel::ERROR:
		return "[ERROR]"sv;
	case LogLevel::WARNING:
		return "!WARNING!"sv;
	case LogLevel::INFO:
		return "(INFO)"sv;
	case LogLevel::DEBUG:
		return "(DEBUG)"sv;
	}
	return "[UNKNOWN]"sv;
}

class Logger {
	ofs output_file;
	LogLevel level;

  public:
	Logger(ofs&& output_file, LogLevel level, passkey<Logger>) noexcept;

	Logger(const Logger&) = delete;
	Logger(Logger&&) noexcept;
	Logger& operator=(const Logger&) = delete;
	Logger& operator=(Logger&&) noexcept;
	~Logger();

	template <LogLevel lvl, typename... Args>
	void log(std::format_string<Args...> fmt, Args&&... args) {
		using std::format_to;
		if constexpr (lvl <= max_log_level) {
			if (lvl <= level) {
				std::ostreambuf_iterator out_it(output_file);
				format_to(out_it, log_level_str(lvl));
				format_to(out_it, ": ");
				format_to(out_it, fmt, std::forward<Args>(args)...);
				format_to(out_it, "\n");
				if constexpr (lvl <= max_flush_level) {
					output_file.flush();
				}
			}
		}
	}

	template <typename... Args>
	void error(std::format_string<Args...> fmt, Args&&... args) {
		log<LogLevel::ERROR>(fmt, std::forward<Args>(args)...);
	}
	template <typename... Args>
	void warn(std::format_string<Args...> fmt, Args&&... args) {
		log<LogLevel::WARNING>(fmt, std::forward<Args>(args)...);
	}
	template <typename... Args>
	void info(std::format_string<Args...> fmt, Args&&... args) {
		log<LogLevel::INFO>(fmt, std::forward<Args>(args)...);
	}
	template <typename... Args>
	void debug(std::format_string<Args...> fmt, Args&&... args) {
		log<LogLevel::DEBUG>(fmt, std::forward<Args>(args)...);
	}

	static Result<Logger> open(const path& file, LogLevel level) noexcept;
};

} // namespace juan
#endif /* ifndef JUAN_LOGGER_H */
