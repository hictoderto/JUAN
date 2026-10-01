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
OFSOutput::OFSOutput(ofs&& output_file, passkey<OFSOutput>) noexcept
    : output_file(std::move(output_file)) {
}

OFSOutput::OFSOutput(OFSOutput&&) noexcept            = default;
OFSOutput& OFSOutput::operator=(OFSOutput&&) noexcept = default;
OFSOutput::~OFSOutput()                               = default;

void OFSOutput::flush() {
	output_file.flush();
}

Result<OFSOutput> OFSOutput::open(const path& file) noexcept {
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

		return Result<OFSOutput>{std::in_place, std::move(stream),
		                         passkey<OFSOutput>{}};
	} catch (...) {
		return unexpected{Status::OPEN_LOG_ERROR};
	}
}

void VoidOutput::flush() {
}

Logger::Logger(VoidOutput&& output, LogLevel level)
    : output{std::move(output)}, level{level} {
}
Logger::Logger(OFSOutput&& output, LogLevel level)
    : output{std::move(output)}, level{level} {
}

void Logger::flush() {
	std::visit([](auto& output) { output.flush(); }, output);
}
} // namespace juan
