#ifndef JUAN_LOGGER_H
#define JUAN_LOGGER_H
#include <format>
#include <fstream>
#include <iterator>
#include <variant>

#include "passkey.h"
#include "result.h"
#include "types.h"

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

class OFSOutput {
	ofs output_file;

  public:
	OFSOutput(ofs&& output_file, passkey<OFSOutput>) noexcept;

	OFSOutput(const OFSOutput&) = delete;
	OFSOutput(OFSOutput&&) noexcept;
	OFSOutput& operator=(const OFSOutput&) = delete;
	OFSOutput& operator=(OFSOutput&&) noexcept;
	~OFSOutput();

	template <typename... Args>
	void format_to(std::format_string<Args...> fmt, Args&&... args) {
		std::ostreambuf_iterator out_it(output_file);
		std::format_to(out_it, fmt, std::forward<Args>(args)...);
	}
	void flush();
	static Result<OFSOutput> open(const path& file) noexcept;
};

class VoidOutput {
  public:
	template <typename... Args>
	void format_to(std::format_string<Args...>, Args&&...) {
	}
	void flush();
};

using OutputVariant = std::variant<VoidOutput, OFSOutput>;

class Logger {
	OutputVariant output;
	LogLevel level;

  public:
	Logger(VoidOutput&& logger, LogLevel level);
	Logger(OFSOutput&& logger, LogLevel level);
	template <typename... Args>
	void format_to(std::format_string<Args...> fmt, Args&&... args) {
		std::visit(
		    [fmt, &args...](auto& output) {
			    output.format_to(fmt, std::forward<Args>(args)...);
		    },
		    output);
	}

	void flush();

	template <LogLevel lvl, typename... Args>
	void log(std::format_string<Args...> fmt, Args&&... args) {
		if constexpr (lvl <= max_log_level) {
			if (lvl <= level) {
				format_to(log_level_str(lvl));
				format_to(": ");
				format_to(fmt, std::forward<Args>(args)...);
				format_to("\n");
				if constexpr (lvl <= max_flush_level) {
					flush();
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
};

} // namespace juan
#endif /* ifndef JUAN_LOGGER_H */
